-- Enemy AI (armed human): patrol -> chase -> shoot -> search -> return home.
-- Attach to an entity of type "enemy" in the editor.

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
    { "search_time",     5.0,  "Seconds spent searching the last known position" },
    { "patrol_radius",   4.0,  "Wanders this far from its start position, m" },
    -- Animation clips of the character model (Properties > Character model).
    { "anim_idle",       "Idle_Loop",          "Clip while standing" },
    { "anim_walk",       "Walk_Loop",          "Clip while patrolling, searching, returning" },
    { "anim_run",        "Jog_Fwd_Loop",       "Clip while chasing" },
    { "anim_aim",        "Pistol_Aim_Neutral", "Clip while aiming at the player" },
    { "anim_shoot",      "Pistol_Shoot",       "Clip played on every shot" },
    { "anim_attack",     "Punch_Jab",          "Clip played on every punch" },
    { "anim_hit",        "Hit_Chest",          "Clip played when hurt" },
    { "anim_death",      "Death01",            "Clip played once when killed" },
    { "anim_speed",      1.0,                  "Playback speed multiplier for all clips" },
}

local state, timer, home, last_seen, patrol_target, next_attack
local hurt_flash = 0
local reacting = 0      -- seconds left of the hit reaction clip
local moving = false   -- set by walk_to, picks walk/run vs. idle animation

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
    local dir = (target - self.position):flat()
    if dir:length() < 0.2 then return true end
    moving = true
    dir = steer(self, dir:normalized())
    self:move(dir, speed)
    self:face(self.position + dir)
    return false
end

local function sees_player(self)
    local eye = self:eye()
    local player = game.player_eye()
    local to = (player - eye)
    local dist = to:length()
    if dist < props.hear_range then return game.can_see(eye, player) end
    if dist > props.view_range then return false end
    local cos_half = math.cos(math.rad(props.view_angle * 0.5))
    if self:forward():dot(to:flat():normalized()) < cos_half then return false end
    return game.can_see(eye, player)
end

local function pick_patrol_target()
    local a = math.random() * math.pi * 2
    local r = props.patrol_radius * (0.4 + 0.6 * math.random())
    return home + vec3(math.cos(a) * r, 0, math.sin(a) * r)
end

local function set_state(self, s)
    if state ~= s then self:log(state and (state .. " -> " .. s) or s) end
    state, timer = s, 0
end

function on_start(self)
    self.health = props.health
    self:enable_character()
    home = self.position
    patrol_target = pick_patrol_target()
    next_attack = 0
    set_state(self, "patrol")
end

-- Getting shot reveals where the player is, even from behind.
function on_damage(self, amount)
    hurt_flash = 0.1
    self.flash = 0.12
    last_seen = game.player_position()
    if self.alive and self:play(props.anim_hit, { loop = false, restart = true, fade = 0.05, speed = props.anim_speed }) then
        reacting = 0.35
    end
    if state ~= "attack" then set_state(self, "chase") end
end

function on_death(self)
    self:log("destroyed")
    self:play(props.anim_death, { loop = false, fade = 0.1, speed = props.anim_speed })
end

-- Chooses the clip for the current state; no-op for entities without a character model.
local function animate(self)
    local speed = props.anim_speed
    if reacting > 0 then return end
    if state == "attack" then
        local one_shot = self.animation == props.anim_shoot or self.animation == props.anim_attack
        if not one_shot or self:animation_done() then
            if not self:play(props.anim_aim, { speed = speed }) then self:play(props.anim_idle, { speed = speed }) end
        end
    elseif moving then
        self:play(state == "chase" and props.anim_run or props.anim_walk, { speed = speed })
    else
        self:play(props.anim_idle, { speed = speed })
    end
end

function on_update(self, dt)
    timer = timer + dt
    hurt_flash = math.max(0, hurt_flash - dt)
    reacting = math.max(0, reacting - dt)
    moving = false
    local player = game.player_position()

    if sees_player(self) then
        last_seen = player
        if state ~= "chase" and state ~= "attack" then set_state(self, "chase") end
    elseif state == "chase" or state == "attack" then
        set_state(self, "search")
    end

    if state == "patrol" then
        if walk_to(self, patrol_target, props.walk_speed) or timer > 6 then
            patrol_target = pick_patrol_target()
            timer = 0
        end
    elseif state == "chase" then
        if distance(self.position, player) <= props.shoot_range then
            set_state(self, "attack")
            next_attack = math.max(next_attack, game.time() + 0.5) -- time to raise the gun
        else
            walk_to(self, player, props.run_speed)
        end
    elseif state == "attack" then
        self:face(player)
        local dist = distance(self.position, player)
        if dist > props.shoot_range * 1.2 then
            set_state(self, "chase")
        elseif reacting <= 0 and game.time() >= next_attack then
            if dist <= props.melee_range then
                game.damage_player(props.melee_damage)
                next_attack = game.time() + 1.0
                self:play(props.anim_attack, { loop = false, restart = true, fade = 0.1, speed = props.anim_speed })
            else
                -- Hit chance falls off with distance; misses still make noise and keep the player moving.
                local chance = math.min(0.95, math.max(0.05, props.accuracy * 10 / math.max(dist, 1)))
                if math.random() < chance then game.damage_player(props.shot_damage) end
                next_attack = game.time() + props.shot_interval * (0.8 + 0.4 * math.random())
                self:play(props.anim_shoot, { loop = false, restart = true, fade = 0.05, speed = props.anim_speed })
            end
        end
    elseif state == "search" then
        if walk_to(self, last_seen, props.walk_speed) then
            -- Look around on the spot.
            self.yaw = self.yaw + 90 * dt
        end
        if timer > props.search_time then set_state(self, "return") end
    elseif state == "return" then
        if walk_to(self, home, props.walk_speed) then set_state(self, "patrol") end
    end

    animate(self)

    -- Color shows the state while developing: calm grey, alerted orange, attacking red.
    if hurt_flash > 0 then self.color = vec3(1, 1, 1)
    elseif state == "attack" then self.color = vec3(0.9, 0.15, 0.1)
    elseif state == "chase" or state == "search" then self.color = vec3(0.95, 0.55, 0.1)
    else self.color = vec3(0.55, 0.5, 0.5) end
end
