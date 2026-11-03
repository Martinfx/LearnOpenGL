-- Starts a quest when the level begins (once; a saved game keeps its state).
-- Attach to any entity, e.g. an "empty" at the player start.

properties = {
    { "quest",   "",  "Quest to start (id from assets/quests/)" },
    { "message", "",  "Text shown on the screen when it starts (empty = none)" },
}

local started = false

function on_update(self, dt)
    if started then return end
    started = true
    if props.quest ~= "" and game.quest(props.quest) == "none" then
        game.start_quest(props.quest)
        if props.message ~= "" then game.message(props.message) end
    end
end

function on_save(self) return { started = started } end
function on_load(self, d) started = d and d.started or false end
