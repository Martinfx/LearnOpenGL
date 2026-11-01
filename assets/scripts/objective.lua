-- Objective marker: reports once when the player comes within `radius` (e.g. a stash to find).
-- With a quest it counts only while that quest is at `stage` (any stage when empty) and then
-- moves the quest on to its next stage (or completes it after the last one). A quest stage
-- that names this entity as its target shows it on the PDA map.
-- Attach to an entity of type "objective" (no character model needed).

properties = {
    { "radius",  1.8, "Distance at which the player reaches the objective, m" },
    { "message", "Objective reached", "Text shown on the screen when the player reaches it (empty = none)" },
    { "quest",   "",  "Quest this objective belongs to (id from assets/quests/, empty = none)" },
    { "stage",   "",  "Counts only while the quest is at this stage (empty = while it is active)" },
}

local done = false

function on_update(self, dt)
    if done then return end
    if props.quest ~= "" then
        local state, stage = game.quest(props.quest)
        if state ~= "active" or (props.stage ~= "" and stage ~= props.stage) then return end
    end
    local d = game.player_position() - self.position
    if d:length() < props.radius then
        done = true
        if props.message ~= "" then game.message(props.message) end
        game.log(props.message)
        if props.quest ~= "" then game.advance_quest(props.quest) end
        self:fire_output("OnReached")
    end
end

-- Saved games: a reached objective stays reached.
function on_save(self) return { done = done } end
function on_load(self, d) done = d and d.done or false end
