-- Human AI (armed stalker, bandit, soldier): patrol -> chase -> shoot -> search -> return home.
-- While patrolling it sometimes rests at a free sit point nearby (campfire seats).
-- Fights whoever its faction is hostile to (the player, other factions; assets/factions.cfg);
-- neutral ones only fight back when attacked. Members of the same Group (Properties > Character)
-- call each other for help. When an anomaly storm is coming it runs for the nearest roof and
-- waits there until the storm fades. Attach to an entity of type "enemy" or "npc" in the editor.

-- Tunables: shown in the editor (Properties > Script parameters) for every
-- entity using this script. Read them through `props`.
properties = {
    { "health",          100,  "Hit points (the AK-74M does 26 per hit)" },
    { "walk_speed",      1.6,  "m/s while patrolling or returning" },
    { "run_speed",       4.0,  "m/s while chasing (player walks 5, sprints 8.5)" },
    { "view_range",      30.0, "How far it can see the player, m" },
    { "view_angle",      120,  "Field of view, degrees (full cone)" },
    { "hear_range",      4.0,  "Notices the player behind it within this distance, m" },
    { "shoot_range",     20.0, "Stops and opens fire within this distance, m" },
    { "shot_damage",     8,    "Damage of one hit to the player" },
    { "shot_interval",   0.9,  "Seconds between shots" },
    { "accuracy",        0.6,  "Hit chance at 10 m (falls off with distance)" },
    { "melee_range",     1.5,  "Hits with the fist instead when this close, m" },
    { "melee_damage",    15,   "Damage of a punch" },
    { "sound_shot",      "sounds/weapons/pistol_shot.sound", "Sound of every shot (path in assets/, empty = silent)" },
    { "search_time",     5.0,  "Seconds spent searching the last known position" },
    { "alert_range",     40.0, "Calls the members of its group within this distance when it starts a fight, m" },
    { "alife",           "",   "A-life: \"roam\" = its group travels between A-life points while the player is away; empty = stays" },
    { "shelter_range",   80.0, "Anomaly storm: looks this far for a roof to hide under, m (0 = stays out)" },
    { "patrol_radius",   4.0,  "Wanders this far from its start position, m" },
    { "fire_avoid",      1.0,  "Fire on the ground (incendiary bottles): runs out of it and keeps this far from its edge, m (0 = ignores fire)" },
    { "rest_chance",     0.25, "Chance per patrol leg to sit at a free sit point within 3x the patrol radius" },
    { "rest_time",       30.0, "Seconds sitting before patrolling again" },
    { "trade",           0,    "1 = a trader: E (or a dialog's `do trade`) opens buying, selling and repairs" },
    { "stock",           "",   "Trader's goods: \"medkit 5; ammo 5.45x39 300; weapon weapons/smg.weapon 1; armor jacket 1\" (prices in assets/items.cfg)" },
    { "markup",          1.3,  "Trader sells for the price times this" },
    { "buy_rate",        0.45, "Trader buys for the price times this (worn things less)" },
    { "repair",          1,    "1 = the trader repairs weapons and armor" },
    -- Animation clips of the character model (Properties > Character model).
    { "anim_idle",       "Idle_Loop",          "Clip while standing" },
    { "anim_sit",        "Sitting_Idle_Loop",  "Clip while resting at a sit point" },
    { "anim_walk",       "Walk_Loop",          "Clip while patrolling, searching, returning" },
    { "anim_run",        "Jog_Fwd_Loop",       "Clip while chasing" },
    { "anim_aim",        "Pistol_Aim_Neutral", "Clip while aiming at the player" },
    { "anim_shoot",      "Pistol_Shoot",       "Clip played on every shot" },
    { "anim_attack",     "Punch_Jab",          "Clip played on every punch" },
    { "anim_hit",        "Hit_Chest",          "Clip played when hurt" },
    { "anim_death",      "Death01",            "Clip played once when killed" },
    { "anim_speed",      1.0,                  "Playback speed multiplier for all clips" },
    -- With an animation graph (Properties > Character > Animation graph) the clips above are
    -- ignored except anim_attack: the graph's parameters are driven instead.
    { "sound_step",      "sounds/player/step_dirt.sound", "Footstep sound on the graph's footstep events (empty = silent)" },
}

local state, timer, home, last_seen, patrol_target, next_attack
local target            -- what it fights: "player", another entity or nil
local grudges = {}      -- handles of entities that attacked it (fought even when not hostile)
local look_timer = 0    -- next look around for enemies
local walked = 0        -- seconds walking to the search spot
local hurt_flash = 0
local reacting = 0      -- seconds left of the hit reaction clip
local moving = false   -- set by walk_to, picks walk/run vs. idle animation
local seat, seated      -- sit point entity while resting, true once sitting on it
local rest_for = 0      -- seconds this rest lasts
local graph             -- the character has an animation graph (decided on the first update)
local anim_speed = 0    -- smoothed movement speed fed to the graph, m/s
local punching = false  -- an explicit punch clip overrides the graph until it ends
local shelter_at        -- anomaly storm: the spot under a roof it hides at
local storm_check = 0   -- next look at the storm
local fire_check = 0    -- next look for fire on the ground
local flee_time = 0     -- seconds left running out of a fire
local flee_spot         -- where it runs to, out of the flames

local function distance(a, b) return (b - a):flat():length() end

-- Steer around walls: probe ahead, then to the sides, and take the first free direction.
local function steer(self, dir)
    local origin = self:eye() - vec3(0, self.height * 0.5, 0)
    for _, angle in ipairs({0, 35, -35, 70, -70, 110, -110}) do
        local r = math.rad(angle)
        local d = vec3(dir.x * math.cos(r) - dir.z * math.sin(r), 0, dir.x * math.sin(r) + dir.z * math.cos(r))
        local hit = game.raycast(origin, d, 1.2)
        if not hit then return d end
    end
    return dir
end

local function walk_to(self, target, speed)
    if (target - self.position):flat():length() < 0.3 then return true end
    moving = true
    if game.nav_ready() then
        -- Navmesh path around walls and buildings (re-planned as the target moves).
        return self:move_to(target, speed)
    end
    -- No navmesh (disabled or still building): feel the way with rays.
    local dir = steer(self, (target - self.position):flat():normalized())
    self:move(dir, speed)
    self:face(self.position + dir)
    return false
end

-- A target is "player" or an entity.
local function t_alive(t)
    if t == "player" then return game.player_health() > 0 end
    return t ~= nil and t.alive
end
local function t_eye(t) if t == "player" then return game.player_eye() end return t:eye() end
local function t_pos(t) if t == "player" then return game.player_position() end return t.position end
local function t_name(t) if t == "player" then return "!player" end return t.name end
local function same(a, b)
    if a == "player" or b == "player" then return a == b end
    return a ~= nil and b ~= nil and a.handle == b.handle
end

-- Whether it would fight t: its faction is hostile to t's, or t attacked it.
local function enemy_of(self, t)
    if t ~= "player" and grudges[t.handle] then return true end
    return game.attitude(self, t) == "hostile"
end

-- In the view cone and range (or close enough to hear) with nothing in between.
local function sees(self, point)
    local eye = self:eye()
    local to = (point - eye)
    local dist = to:length()
    if dist < props.hear_range then return game.can_see(eye, point) end
    if dist > props.view_range then return false end
    local cos_half = math.cos(math.rad(props.view_angle * 0.5))
    if self:forward():dot(to:flat():normalized()) < cos_half then return false end
    return game.can_see(eye, point)
end

-- The nearest living enemy it can see (the player or a character of a hostile faction).
local function look_for_enemy(self)
    local best, best_d
    local function consider(t)
        if not t_alive(t) or not enemy_of(self, t) or not sees(self, t_eye(t)) then return end
        local d = distance(self.position, t_pos(t))
        if not best or d < best_d then best, best_d = t, d end
    end
    consider("player")
    for _, e in ipairs(game.characters_near(self.position, props.view_range)) do
        if e.handle ~= self.handle then consider(e) end
    end
    return best
end

-- Calls the members of its group (same Group property) to the fight.
local function alert_group(self, t)
    if self.group == "" or t == nil then return end
    for _, m in ipairs(game.characters_near(self.position, props.alert_range)) do
        if m.handle ~= self.handle and m.group == self.group then game.fire(m.name, "Alert", t_name(t)) end
    end
end

local function pick_patrol_target()
    local p = game.random_nav_point(home, props.patrol_radius)
    -- a reachable spot (the navmesh may pick one anywhere in a large polygon touching the circle)
    if p and distance(home, p) <= props.patrol_radius + 0.5 then return p end
    if props.patrol_radius <= 0.5 then return home end
    local a = math.random() * math.pi * 2
    local r = props.patrol_radius * (0.4 + 0.6 * math.random())
    return home + vec3(math.cos(a) * r, 0, math.sin(a) * r)
end

local function release_seat(self)
    if seat then seat:set_property("occupied", nil) end
    seat, seated = nil, false
end

local function set_state(self, s)
    if state ~= s then self:log(state and (state .. " -> " .. s) or s) end
    if state == "rest" and s ~= "rest" then release_seat(self) end
    -- Entity I/O: the moment it goes after the player (connect e.g. to an alarm in the editor).
    if s == "chase" and state ~= "chase" and state ~= "attack" and state ~= "search" then self:fire_output("OnSpotted") end
    if s == "search" then walked = 0 end
    state, timer = s, 0
end

-- A free sit point near home, claimed for this entity (nil if none).
local function claim_seat(self)
    local s = game.find_nearest("sit_point", self.position, props.patrol_radius * 3, "occupied")
    if s then
        s:set_property("occupied", self.name)
        seat, seated = s, false
    end
    return s
end

function on_start(self)
    self.health = props.health
    self:enable_character()
    home = self.position
    patrol_target = pick_patrol_target()
    next_attack = 0
    set_state(self, "patrol")
end

-- Saved games: what the AI was doing (the engine keeps health, place, facing and a grudge
-- against the player).
function on_save(self)
    local held = {}
    for _, e in ipairs(game.characters_near(self.position, 1e6)) do
        if grudges[e.handle] then held[#held + 1] = e.name end
    end
    return { state = state, timer = timer, home = home, last_seen = last_seen, patrol_target = patrol_target,
             seat = seat and seat.name, seated = seated, rest_for = rest_for,
             target = target == "player" and "!player" or (target and target.name), grudges = held }
end

function on_load(self, d)
    if not d then return end
    state, timer = d.state or "patrol", d.timer or 0
    home = d.home or home
    last_seen, patrol_target = d.last_seen, d.patrol_target or patrol_target
    seat, seated, rest_for = d.seat and game.find(d.seat), d.seated, d.rest_for or props.rest_time
    if seat then seat:set_property("occupied", self.name) elseif state == "rest" then state = "patrol" end
    if state == "shelter" then state = "return" end -- looks for its roof again if the storm is still on
    grudges = {}
    for _, name in ipairs(d.grudges or {}) do
        local e = game.find(name)
        if e then grudges[e.handle] = true end
    end
    if d.target == "!player" then target = "player" elseif d.target then target = game.find(d.target) end
    next_attack = game.time() + 0.5 -- no shot in the first instant after loading
end

-- Getting hurt reveals the attacker, even from behind: it fights back and calls its group.
-- attacker: "player", an entity or nil (anomaly, fall, trap).
function on_damage(self, amount, attacker)
    hurt_flash = 0.1
    self.flash = 0.12
    if attacker ~= nil and not same(attacker, self) then
        if attacker ~= "player" then grudges[attacker.handle] = true end
        if not t_alive(target) or state ~= "attack" then target = attacker end
        last_seen = t_pos(attacker)
        alert_group(self, attacker)
    end
    if graph then
        if self.alive then self:anim_trigger("hit"); reacting = 0.35 end
    elseif self.alive and self:play(props.anim_hit, { loop = false, restart = true, fade = 0.05, speed = props.anim_speed }) then
        reacting = 0.35
    end
    -- Hurt by the Zone itself (anomaly, fall, trap) and fighting nobody: nothing to chase.
    if target and state ~= "attack" then set_state(self, "chase") end
end

-- Entity I/O inputs (Outputs table of another entity, or game.fire): "Alert" sends it after
-- the player (an alarm, a tripwire) or after the entity named in the parameter (its group
-- calling for help); "GoTo" "x y z" moves its home there (A-life squads travelling between
-- points); "Calm" sends it back home.
function on_input(self, input, param)
    if not self.alive then return end
    if input == "Alert" then
        local t = "player"
        if param ~= nil and param ~= "" and param ~= "!player" then t = game.find(param) end
        if not t_alive(t) or same(t, self) then return end
        -- Its group is fighting them: it does too, even if it was neutral.
        if t == "player" then self.hates_player = true else grudges[t.handle] = true end
        if state == "attack" and t_alive(target) then return end
        target = t
        last_seen = t_pos(t)
        if state ~= "chase" then set_state(self, "chase") end
    elseif input == "GoTo" then
        -- A-life (roaming squad): its new place is "x y z"; it walks there unless fighting.
        local x, y, z = string.match(param or "", "(%S+)%s+(%S+)%s+(%S+)")
        if x then
            home = vec3(tonumber(x), tonumber(y), tonumber(z))
            patrol_target = home
            if state ~= "attack" and state ~= "chase" and state ~= "search" then set_state(self, "return") end
        end
    elseif input == "Calm" then
        target = nil
        grudges = {}
        self.hates_player = false
        set_state(self, "return")
    end
end

function on_death(self)
    self:log("destroyed")
    release_seat(self)
    if graph then
        self:use_graph()
        self:set_anim("dead", true)
    else
        self:play(props.anim_death, { loop = false, fade = 0.1, speed = props.anim_speed })
    end
end

-- Animation graph: movement speed, sitting and aiming as parameters; footsteps from clip events.
local function animate_graph(self, dt)
    if punching then
        if not self:animation_done() then return end
        self:use_graph()
        punching = false
    end
    local want = 0
    if moving then want = (state == "chase" or state == "shelter") and props.run_speed or props.walk_speed end
    anim_speed = anim_speed + (want - anim_speed) * math.min(1, dt * 8)
    self:set_anim("speed", anim_speed)
    self:set_anim("sit", state == "rest" and seated == true)
    self:set_anim("aim", state == "attack")
    self:set_anim("grip", state == "attack" and 1 or 0)
    if state == "attack" and t_alive(target) then
        local eye, aim = self:eye(), t_eye(target)
        local pitch = math.atan(aim.y - eye.y, math.max(distance(eye, aim), 0.5))
        self:set_anim("aim_pitch", math.max(-1, math.min(1, pitch / math.rad(45))))
    end
    if props.sound_step ~= "" and self:anim_event("footstep") then
        game.play_sound(props.sound_step, self.position, 0.6)
    end
end

-- Chooses the clip for the current state; no-op for entities without a character model.
local function animate(self, dt)
    if graph == nil then graph = self:set_anim("speed", 0) end
    if graph then return animate_graph(self, dt) end
    local speed = props.anim_speed
    if reacting > 0 then return end
    if state == "attack" then
        local one_shot = self.animation == props.anim_shoot or self.animation == props.anim_attack
        if not one_shot or self:animation_done() then
            if not self:play(props.anim_aim, { speed = speed }) then self:play(props.anim_idle, { speed = speed }) end
        end
    elseif moving then
        self:play((state == "chase" or state == "shelter") and props.anim_run or props.anim_walk, { speed = speed })
    elseif state == "rest" and seated then
        if not self:play(props.anim_sit, { speed = speed }) then self:play(props.anim_idle, { speed = speed }) end
    else
        self:play(props.anim_idle, { speed = speed })
    end
end

function on_update(self, dt)
    timer = timer + dt
    hurt_flash = math.max(0, hurt_flash - dt)
    reacting = math.max(0, reacting - dt)
    moving = false
    look_timer = look_timer - dt

    -- Who to fight: the current target while it is in sight, else the nearest enemy it sees
    -- (looked for a few times a second, not every step).
    if not t_alive(target) then target = nil end
    local visible = target ~= nil and sees(self, t_eye(target))
    if not visible and look_timer <= 0 then
        look_timer = 0.25 + 0.1 * math.random()
        local found = look_for_enemy(self)
        if found then
            local calm = state ~= "chase" and state ~= "attack" and state ~= "search"
            target, visible = found, true
            if calm then alert_group(self, found) end
        end
    end
    -- A shot heard while calm: go and look where it came from.
    if not visible and (state == "patrol" or state == "rest" or state == "return") then
        local heard = game.noise_near(self.position)
        if heard then
            last_seen = heard
            set_state(self, "search")
        end
    end
    if visible then
        last_seen = t_pos(target)
        if state ~= "chase" and state ~= "attack" then set_state(self, "chase") end
    elseif state == "chase" or state == "attack" then
        set_state(self, target and "search" or "return")
    end
    local goal = target and t_pos(target) or last_seen

    -- Fire on the ground (incendiary bottles): out of the flames first, whatever it was doing.
    fire_check = fire_check - dt
    if fire_check <= 0 and props.fire_avoid > 0 then
        fire_check = 0.2
        local at, r = game.fire_near(self.position, props.fire_avoid)
        if at then
            local away = (self.position - at):flat()
            if away:length() < 0.1 then away = vec3(math.random() - 0.5, 0, math.random() - 0.5) end
            flee_spot = at + away:normalized() * (r + props.fire_avoid + 1.5)
            if flee_time <= 0 then release_seat(self) end
            flee_time = 2.0
        end
    end
    if flee_time > 0 then
        flee_time = flee_time - dt
        if walk_to(self, flee_spot, props.run_speed) then flee_time = 0 end
        if state == "rest" then set_state(self, "patrol") end
        animate(self, dt)
        return
    end

    -- Anomaly storm: when warned, run under the nearest roof (unless fighting) and stay there.
    storm_check = storm_check - dt
    if storm_check <= 0 then
        storm_check = 1 + 0.5 * math.random()
        local phase = game.storm()
        local danger = phase == "warning" or phase == "storm"
        if danger and state ~= "shelter" and state ~= "chase" and state ~= "attack" and props.shelter_range > 0 then
            if game.sheltered(self:eye()) then
                shelter_at = self.position
            else
                shelter_at = game.shelter_near(self.position, props.shelter_range)
                -- spread out a little inside (several NPCs at one roof)
                if shelter_at then
                    local p = game.random_nav_point(shelter_at, 1.5)
                    if p and distance(p, shelter_at) < 2 and game.sheltered(p + vec3(0, 1.9, 0)) then shelter_at = p end
                end
            end
            if shelter_at then set_state(self, "shelter") end
        elseif not danger and state == "shelter" then
            shelter_at = nil
            set_state(self, "return")
        end
    end

    if state == "shelter" then
        if shelter_at and walk_to(self, shelter_at, props.run_speed) then
            shelter_at = self.position -- arrived: waits out the storm
        end
    elseif state == "patrol" then
        if walk_to(self, patrol_target, props.walk_speed) or timer > 6 then
            patrol_target = pick_patrol_target()
            timer = 0
            if math.random() < props.rest_chance and claim_seat(self) then set_state(self, "rest") end
        end
    elseif state == "rest" then
        if not seated then
            if walk_to(self, seat.position, props.walk_speed) then
                seated, timer = true, 0
                rest_for = props.rest_time * (0.7 + 0.6 * math.random())
                self.position = seat.position
                self.yaw = seat.yaw
            elseif timer > 20 then -- cannot get there
                set_state(self, "patrol")
            end
        elseif timer > rest_for then
            set_state(self, "patrol")
        end
    elseif state == "chase" then
        if distance(self.position, goal) <= props.shoot_range then
            set_state(self, "attack")
            next_attack = math.max(next_attack, game.time() + 0.5) -- time to raise the gun
        else
            walk_to(self, goal, props.run_speed)
        end
    elseif state == "attack" then
        self:face(goal)
        local dist = distance(self.position, goal)
        if dist > props.shoot_range * 1.2 then
            set_state(self, "chase")
        elseif reacting <= 0 and game.time() >= next_attack then
            local function hit(amount)
                if target == "player" then game.damage_player(amount) else game.damage(target, amount, self) end
            end
            if dist <= props.melee_range then
                hit(props.melee_damage)
                next_attack = game.time() + 1.0
                if self:play(props.anim_attack, { loop = false, restart = true, fade = 0.1, speed = props.anim_speed }) then punching = graph end
            else
                -- Hit chance falls off with distance; misses still make noise and keep the player moving.
                local chance = math.min(0.95, math.max(0.05, props.accuracy * 10 / math.max(dist, 1)))
                if math.random() < chance then hit(props.shot_damage) end
                game.make_noise(self:eye(), 60) -- others hear the shot (crows, NPCs)
                if props.sound_shot ~= "" then
                    local p = self.position
                    game.play_sound(props.sound_shot, vec3(p.x, p.y + 1.4, p.z))
                end
                next_attack = game.time() + props.shot_interval * (0.8 + 0.4 * math.random())
                if graph then self:anim_trigger("shoot")
                else self:play(props.anim_shoot, { loop = false, restart = true, fade = 0.05, speed = props.anim_speed }) end
            end
        end
    elseif state == "search" then
        if not last_seen or walk_to(self, last_seen, props.walk_speed) then
            -- Look around on the spot.
            self.yaw = self.yaw + 90 * dt
        else
            walked = walked + dt -- still on the way there: the search starts on arrival (gives up after 40 s)
            if walked < 40 then timer = 0 end
        end
        if timer > props.search_time then target = nil; set_state(self, "return") end
    elseif state == "return" then
        if walk_to(self, home, props.walk_speed) then set_state(self, "patrol") end
    end

    animate(self, dt)

    -- ai_debug 1 (console): name, faction, state, health and the view cone above every NPC.
    if game.cvar("ai_debug") == "1" then
        local eye = self:eye()
        local seen = visible
        local who = target == "player" and "player" or (target and target.name) or "-"
        game.debug_text(eye + vec3(0, 0.45, 0), string.format("%s [%s%s] %s -> %s  %d hp", self.name, self.faction,
                        self.group ~= "" and ("/" .. self.group) or "", state, who, math.floor(self.health)),
                        seen and vec3(1, 0.3, 0.2) or vec3(0.85, 0.85, 0.85))
        local f = self:forward()
        local half = math.rad(props.view_angle * 0.5)
        for _, a in ipairs({-half, half}) do
            local d = vec3(f.x * math.cos(a) - f.z * math.sin(a), 0, f.x * math.sin(a) + f.z * math.cos(a))
            game.debug_line(eye, eye + d * props.view_range, vec3(0.9, 0.8, 0.2))
        end
        if seen then game.debug_line(eye, t_eye(target), vec3(1, 0.2, 0.1)) end
    end

    -- Color shows the state while developing: calm grey, alerted orange, attacking red.
    if hurt_flash > 0 then self.color = vec3(1, 1, 1)
    elseif state == "attack" then self.color = vec3(0.9, 0.15, 0.1)
    elseif state == "chase" or state == "search" then self.color = vec3(0.95, 0.55, 0.1)
    else self.color = vec3(0.55, 0.5, 0.5) end
end
