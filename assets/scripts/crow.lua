-- Crow (one bird of a flock): pecks around on the ground and takes off when someone comes
-- close or a shot is heard. In the air the flock flies as boids (keeps together, same way,
-- not too close) circling above its home, and lands again once the Zone is quiet.
-- Place with Place > Crow flock: the flock entity becomes `count` crows when the game loads.

properties = {
    { "count",          6,    "Crows in the flock (flock entity)" },
    { "radius",         5.0,  "The crows start scattered this far around the flock, m (flock entity)" },
    { "startle_range",  8.0,  "Takes off when a person comes this close, m" },
    { "fly_height",     16.0, "Height of the circling above home, m" },
    { "fly_radius",     18.0, "Radius of the circling, m" },
    { "fly_speed",      7.0,  "Flight speed, m/s" },
    { "fly_time",       25.0, "Seconds in the air after the last scare before landing" },
    { "sound",          "sounds/ambient/crow.sound", "Caw on taking off (empty = silent)" },
    { "anim_idle",      "Idle_Loop",  "Clip on the ground" },
    { "anim_walk",      "Idle_Loop",  "Clip while hopping on the ground" },
    { "anim_takeoff",   "TakeOff",    "Clip played once when taking off (then the flying clip)" },
    { "anim_fly",       "Fly_Loop",   "Clip while flapping" },
    { "anim_glide",     "Glide_Loop", "Clip while gliding" },
}

local state = "ground"   -- ground, fly, land
local home, spot, velocity
local calm = 0           -- seconds without a scare while flying
local phase = 0          -- where on the circle the flock aims
local hop = 0            -- seconds to the next hop on the ground
local hop_to

local function ground_at(p)
    local hit, point = game.raycast(vec3(p.x, p.y + 40, p.z), vec3(0, -1, 0), 80)
    if hit then return point.y end
    return home.y
end

local taking_off = 0     -- seconds of the take-off clip left

local function play(self, clip, once)
    if not self.has_model then return end
    if once then
        self:play(clip, { loop = false, restart = true, fade = 0.05 })
    elseif taking_off <= 0 then
        self:play(clip, { fade = 0.15 })
    end
end

local function scared(self)
    if game.noise_near(self.position) then return true end
    for _, c in ipairs(game.characters_near(self.position, props.startle_range)) do
        return true -- a person or an animal
    end
    if (game.player_position() - self.position):length() < props.startle_range and game.player_health() > 0 then return true end
    -- A flock mate flying off (in the last moment) scares the rest.
    for _, m in ipairs(game.entities_near(self.position, 25, "crow")) do
        local t = tonumber(m:get_property("took_off") or "")
        if t and game.time() - t < 1.5 and m:get_property("flock") == self:get_property("flock") then return true end
    end
    return false
end

local function take_off(self)
    if state ~= "fly" and props.sound ~= "" and math.random() < 0.6 then game.play_sound(props.sound, self.position, 0.9) end
    state, calm = "fly", 0
    self:set_property("flying", "1")
    self:set_property("took_off", game.time())
    velocity = vec3(math.random() - 0.5, 1.5, math.random() - 0.5):normalized() * props.fly_speed * 0.6
    taking_off = 0
    play(self, props.anim_takeoff, true)
    taking_off = 0.75
end

function on_start(self)
    home = self.position
    spot = self.position
    velocity = vec3(0, 0, 0)
    phase = math.random() * math.pi * 2
    hop = math.random() * 3
    self:set_property("flying", nil)
    self.health = 5
    play(self, props.anim_idle)
end

-- Shot down: it drops where it was (the rest of the flock is scared off by the shot).
function on_death(self)
    self:set_property("flying", nil)
    local p = self.position
    self.position = vec3(p.x, ground_at(p), p.z)
end

function on_save(self)
    return { state = state, home = home, spot = spot }
end

function on_load(self, d)
    if not d then return end
    state, home, spot = d.state or "ground", d.home or home, d.spot or spot
    velocity = vec3(0, 0, 0)
    self:set_property("flying", state == "fly" and "1" or nil)
end

local function flock_steer(self)
    -- Boids: cohesion to the flying mates, alignment with their way, separation from close ones,
    -- plus the pull to a point circling above home.
    local center, align, apart, n = vec3(0, 0, 0), vec3(0, 0, 0), vec3(0, 0, 0), 0
    for _, m in ipairs(game.entities_near(self.position, 12, "crow")) do
        if m.handle ~= self.handle and m:get_property("flying") == "1" then
            n = n + 1
            center = center + m.position
            local d = self.position - m.position
            local len = d:length()
            if len < 1.5 and len > 0.01 then apart = apart + d * ((1.5 - len) / len) end
            align = align + vec3(math.cos(math.rad(m.yaw)), 0, math.sin(math.rad(m.yaw)))
        end
    end
    local goal = home + vec3(math.cos(phase) * props.fly_radius, props.fly_height + math.sin(phase * 2.3) * 2, math.sin(phase) * props.fly_radius)
    local steer = (goal - self.position):normalized() * 1.2
    if n > 0 then
        steer = steer + ((center * (1 / n)) - self.position):normalized() * 0.5 + align * (0.4 / n) + apart * 2.0
    end
    return steer
end

function on_update(self, dt)
    taking_off = math.max(0, taking_off - dt)
    if state == "ground" then
        if scared(self) then take_off(self) return end
        hop = hop - dt
        if hop <= 0 then
            hop = 1.5 + math.random() * 4
            local a = math.random() * math.pi * 2
            hop_to = spot + vec3(math.cos(a), 0, math.sin(a)) * (0.3 + math.random() * 0.8)
        end
        if hop_to then
            local d = (hop_to - self.position):flat()
            if d:length() < 0.05 then
                hop_to = nil
                play(self, props.anim_idle)
            else
                local step = math.min(d:length(), 0.8 * dt)
                local p = self.position + d:normalized() * step
                self.position = vec3(p.x, ground_at(p), p.z)
                self.yaw = math.deg(math.atan(d.z, d.x))
                play(self, props.anim_walk)
            end
        end
    elseif state == "fly" then
        phase = phase + dt * props.fly_speed / math.max(props.fly_radius, 1)
        if scared(self) and (game.player_position() - self.position):length() < props.startle_range * 2 then calm = 0 end
        if game.noise_near(self.position) then calm = 0 end
        calm = calm + dt
        local want = (velocity + flock_steer(self) * (props.fly_speed * dt * 3))
        local speed = math.max(props.fly_speed * 0.7, math.min(props.fly_speed, want:length()))
        velocity = want:normalized() * speed
        self.position = self.position + velocity * dt
        if velocity:flat():length() > 0.1 then self.yaw = math.deg(math.atan(velocity.z, velocity.x)) end
        play(self, velocity.y < -0.5 and props.anim_glide or props.anim_fly)
        if calm > props.fly_time * (0.8 + 0.4 * math.random()) then
            -- Land somewhere near home.
            local a = math.random() * math.pi * 2
            local r = math.random() * 6
            local p = home + vec3(math.cos(a) * r, 0, math.sin(a) * r)
            spot = vec3(p.x, ground_at(p), p.z)
            state = "land"
        end
    elseif state == "land" then
        if scared(self) and (game.player_position() - self.position):length() < props.startle_range then take_off(self) return end
        local d = spot - self.position
        if d:length() < 0.3 then
            self.position = spot
            state = "ground"
            self:set_property("flying", nil)
            play(self, props.anim_idle)
            hop = 2 + math.random() * 3
            return
        end
        velocity = d:normalized() * math.min(props.fly_speed * 0.6, d:length() * 1.5 + 1)
        self.position = self.position + velocity * dt
        if velocity:flat():length() > 0.1 then self.yaw = math.deg(math.atan(velocity.z, velocity.x)) end
        play(self, props.anim_glide)
    end
end
