-- Feral dog (one of a pack): rests and wanders around its lair, hunts whoever it sees or
-- smells (the mutant faction is hostile to everyone), calls the pack, comes at the prey from
-- different sides, bites and jumps back, and runs home when badly hurt. An anomaly storm
-- does not hurt it, but it senses one coming and lies low at the lair until it passes.
-- Attach to an entity of type "mutant" with the pack's name as Group (Place > Dog pack adds
-- a lair point and four dogs with models/characters/animals/wolf.glb).

properties = {
    { "health",         60,   "Hit points" },
    { "walk_speed",     1.5,  "m/s while wandering" },
    { "run_speed",      7.0,  "m/s while hunting (the player sprints 8.5)" },
    { "view_range",     25.0, "Sees prey this far (in front), m" },
    { "smell_range",    10.0, "Notices prey all around within this distance, m" },
    { "bite_range",     1.4,  "Bites within this distance, m" },
    { "bite_damage",    8,    "Damage of a bite" },
    { "bite_interval",  1.3,  "Seconds between bites" },
    { "lair_radius",    12.0, "Wanders this far from its lair, m" },
    { "flee_health",    0.3,  "Runs home below this share of its health" },
    { "alert_range",    40.0, "Calls the pack (same Group) within this distance, m" },
    { "sound_bark",     "sounds/ambient/dog_bark_far.sound", "Bark when it starts a hunt (empty = silent)" },
    { "anim_idle",      "Idle_Loop",   "Clip while standing" },
    { "anim_walk",      "Walk_Loop",   "Clip while wandering" },
    { "anim_run",       "Run_Loop",    "Clip while hunting or fleeing" },
    { "anim_attack",    "Attack_Bite", "Clip of a bite" },
    { "anim_hit",       "Hit",         "Clip when hurt" },
    { "anim_death",     "Death",       "Clip when killed (holds its last frame)" },
}

local state, timer = "idle", 0
local lair, wander_to, target, next_bite, look = nil, nil, nil, 0, 0
local flank = 0          -- angle this dog comes from (each dog of a pack a different one)
local start_health

local function flat(a, b) return (b - a):flat():length() end
local function t_alive(t)
    if t == "player" then return game.player_health() > 0 end
    return t ~= nil and t.alive
end
local function t_pos(t) if t == "player" then return game.player_position() end return t.position end
local function t_eye(t) if t == "player" then return game.player_eye() end return t:eye() end
local function t_name(t) if t == "player" then return "!player" end return t.name end

local function play(self, clip, once)
    if self.has_model then self:play(clip, { loop = not once, restart = once, fade = 0.12 }) end
end

local function senses(self, t)
    local eye, p = self:eye(), t_eye(t)
    local d = flat(self.position, t_pos(t))
    if d <= props.smell_range then return true end -- the nose does not need a line of sight
    if d > props.view_range then return false end
    if self:forward():dot((p - eye):flat():normalized()) < 0.2 then return false end
    return game.can_see(eye, p)
end

local function find_prey(self)
    local best, best_d
    local function consider(t)
        if not t_alive(t) or game.attitude(self, t) ~= "hostile" or not senses(self, t) then return end
        local d = flat(self.position, t_pos(t))
        if not best or d < best_d then best, best_d = t, d end
    end
    consider("player")
    for _, e in ipairs(game.characters_near(self.position, props.view_range)) do
        if e.handle ~= self.handle then consider(e) end
    end
    return best
end

local function call_pack(self, t)
    if self.group == "" then return end
    for _, m in ipairs(game.characters_near(self.position, props.alert_range)) do
        if m.handle ~= self.handle and m.group == self.group then game.fire(m.name, "Alert", t_name(t)) end
    end
end

local function go(self, to, speed)
    if game.nav_ready() then return self:move_to(to, speed) end
    self:move((to - self.position):flat():normalized(), speed)
    self:face(to)
    return flat(self.position, to) < 0.5
end

local function hunt(self, t)
    if state ~= "hunt" and state ~= "retreat" then
        if props.sound_bark ~= "" then game.play_sound(props.sound_bark, self.position, 0.8) end
        call_pack(self, t)
    end
    target, state, timer = t, "hunt", 0
end

function on_start(self)
    self.health = props.health
    start_health = props.health
    self:enable_character()
    lair = self.position
    local point = game.find_nearest("alife_point", self.position, 40)
    if point and string.find(point:get_property("roles") or "", "lair") then lair = point.position end
    flank = (self.handle * 1.7) % (math.pi * 2)
    play(self, props.anim_idle)
end

function on_save(self)
    return { state = state, lair = lair, target = target and t_name(target) }
end

function on_load(self, d)
    if not d then return end
    state, lair = d.state or "idle", d.lair or lair
    if d.target == "!player" then target = "player" elseif d.target then target = game.find(d.target) end
    if (state == "hunt" or state == "retreat") and not t_alive(target) then state = "idle" end
end

function on_damage(self, amount, attacker)
    self.flash = 0.12
    play(self, props.anim_hit, true)
    if attacker and self.health > start_health * props.flee_health then hunt(self, attacker) end
end

function on_input(self, input, param)
    if not self.alive then return end
    if input == "Alert" then
        local t = "player"
        if param ~= nil and param ~= "" and param ~= "!player" then t = game.find(param) end
        if t_alive(t) and state ~= "flee" then hunt(self, t) end
    elseif input == "Calm" then
        target, state = nil, "idle"
    end
end

function on_death(self)
    play(self, props.anim_death, true)
end

function on_update(self, dt)
    timer = timer + dt
    look = look - dt

    -- Badly hurt: back to the lair.
    if state ~= "flee" and self.health < start_health * props.flee_health then state, timer = "flee", 0 end
    -- Anomaly storm coming or raging: the pack runs home and lies low.
    local phase = game.storm()
    local lying_low = phase == "warning" or phase == "storm"
    if lying_low and state ~= "flee" and state ~= "idle" then target, state, timer = nil, "flee", 0 end

    if (state == "idle" or state == "wander") and look <= 0 and not lying_low then
        look = 0.3 + 0.1 * math.random()
        local prey = find_prey(self)
        if prey then hunt(self, prey) end
    end

    if state == "idle" then
        play(self, props.anim_idle)
        if timer > 4 + math.random() * 6 and not lying_low then
            local a = math.random() * math.pi * 2
            wander_to = game.random_nav_point(lair, props.lair_radius) or
                        (lair + vec3(math.cos(a), 0, math.sin(a)) * (props.lair_radius * math.random()))
            state, timer = "wander", 0
        end
    elseif state == "wander" then
        play(self, props.anim_walk)
        if go(self, wander_to, props.walk_speed) or timer > 12 then state, timer = "idle", 0 end
    elseif state == "hunt" then
        if not t_alive(target) or flat(self.position, t_pos(target)) > props.view_range * 2 then
            target, state, timer = nil, "return", 0
            return
        end
        local p = t_pos(target)
        local d = flat(self.position, p)
        play(self, props.anim_run)
        if d > 4 then
            -- Come in from its own side of the prey, then straight at it.
            local side = vec3(math.cos(flank), 0, math.sin(flank)) * math.min(3, d * 0.3)
            go(self, p + side, props.run_speed)
        elseif d > props.bite_range then
            go(self, p, props.run_speed)
        else
            self:face(p)
            if game.time() >= next_bite then
                if target == "player" then game.damage_player(props.bite_damage) else game.damage(target, props.bite_damage, self) end
                next_bite = game.time() + props.bite_interval * (0.8 + 0.4 * math.random())
                play(self, props.anim_attack, true)
                state, timer = "retreat", 0
            end
        end
    elseif state == "retreat" then
        -- Jump back after a bite, then go again.
        if t_alive(target) then
            local away = (self.position - t_pos(target)):flat():normalized()
            self:move(away, props.run_speed * 0.7)
            self:face(t_pos(target))
        end
        if timer > 0.6 then state, timer = t_alive(target) and "hunt" or "return", 0 end
    elseif state == "flee" or state == "return" then
        play(self, state == "flee" and props.anim_run or props.anim_walk)
        if go(self, lair, state == "flee" and props.run_speed or props.walk_speed) or timer > 30 then
            target, state, timer = nil, "idle", 0
        end
    end

    if game.cvar("ai_debug") == "1" then
        game.debug_text(self:eye() + vec3(0, 0.4, 0), string.format("%s [%s] %s %d hp", self.name, self.group, state, math.floor(self.health)),
                        vec3(0.9, 0.7, 0.4))
    end
end
