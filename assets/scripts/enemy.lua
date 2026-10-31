-- Enemy AI: patrol -> chase -> attack -> search -> return home.
-- Attach to an entity of type "enemy" in the editor. Tune the values below.

local config = {
    walk_speed   = 2.0,   -- m/s while patrolling / returning
    run_speed    = 4.2,   -- m/s while chasing (player walks 5, sprints 8.5)
    view_range   = 18.0,  -- m
    view_angle   = 120,   -- degrees, full cone in front of the enemy
    hear_range   = 3.5,   -- m, notices the player behind it at this distance
    attack_range = 1.6,   -- m
    attack_damage = 10,
    attack_cooldown = 1.0, -- s
    search_time  = 4.0,   -- s spent looking around the last known position
    patrol_radius = 4.0,  -- m around the spawn point
}

local state, timer, home, last_seen, patrol_target, next_attack

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
    if dist < config.hear_range then return game.can_see(eye, player) end
    if dist > config.view_range then return false end
    local cos_half = math.cos(math.rad(config.view_angle * 0.5))
    if self:forward():dot(to:flat():normalized()) < cos_half then return false end
    return game.can_see(eye, player)
end

local function pick_patrol_target()
    local a = math.random() * math.pi * 2
    local r = config.patrol_radius * (0.4 + 0.6 * math.random())
    return home + vec3(math.cos(a) * r, 0, math.sin(a) * r)
end

local function set_state(self, s)
    if state ~= s then self:log(state and (state .. " -> " .. s) or s) end
    state, timer = s, 0
end

function on_start(self)
    self:enable_character()
    home = self.position
    patrol_target = pick_patrol_target()
    next_attack = 0
    set_state(self, "patrol")
end

function on_update(self, dt)
    timer = timer + dt
    local player = game.player_position()

    if sees_player(self) then
        last_seen = player
        if state ~= "chase" and state ~= "attack" then set_state(self, "chase") end
    elseif state == "chase" or state == "attack" then
        set_state(self, "search")
    end

    if state == "patrol" then
        if walk_to(self, patrol_target, config.walk_speed) or timer > 6 then
            patrol_target = pick_patrol_target()
            timer = 0
        end
    elseif state == "chase" then
        if distance(self.position, player) <= config.attack_range then
            set_state(self, "attack")
        else
            walk_to(self, player, config.run_speed)
        end
    elseif state == "attack" then
        self:face(player)
        if distance(self.position, player) > config.attack_range * 1.2 then
            set_state(self, "chase")
        elseif game.time() >= next_attack then
            game.damage_player(config.attack_damage)
            next_attack = game.time() + config.attack_cooldown
        end
    elseif state == "search" then
        if walk_to(self, last_seen, config.walk_speed) then
            -- Look around on the spot.
            self.yaw = self.yaw + 90 * dt
        end
        if timer > config.search_time then set_state(self, "return") end
    elseif state == "return" then
        if walk_to(self, home, config.walk_speed) then set_state(self, "patrol") end
    end

    -- Color shows the state while developing: calm grey, alerted orange, attacking red.
    if state == "attack" then self.color = vec3(0.9, 0.15, 0.1)
    elseif state == "chase" or state == "search" then self.color = vec3(0.95, 0.55, 0.1)
    else self.color = vec3(0.55, 0.5, 0.5) end
end
