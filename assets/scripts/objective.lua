-- Objective marker: reports once when the player comes within `radius` (e.g. a stash to find).
-- Attach to an entity of type "objective" (no character model needed).

properties = {
    { "radius",  1.8, "Distance at which the player reaches the objective, m" },
    { "message", "Objective reached", "Text printed when the player reaches it" },
}

local done = false

function on_update(self, dt)
    if done then return end
    local d = game.player_position() - self.position
    if d:length() < props.radius then
        done = true
        game.log(props.message)
    end
end
