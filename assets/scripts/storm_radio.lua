-- A radio tuned to the military beacon: once `quest` is done (someone fixed it), it warns the
-- player `lead` seconds before each anomaly storm's warning, earlier than the siren.
-- Attach to any entity (e.g. type "radio" next to the radio model); no model needed.

properties = {
    { "quest",   "",  "Quest that has to be done first (id from assets/quests/, empty = always)" },
    { "lead",    120, "Seconds before the storm warning (siren) that the radio speaks up" },
    { "message", "Radio: a storm is coming", "Text on the screen (\"@key\" = translated text)" },
    { "sound",   "sounds/ui/pda_beep.sound", "Sound played with the message (empty = none)" },
}

local warned = false

function on_update(self, dt)
    if props.quest ~= "" then
        local state = game.quest(props.quest)
        if state ~= "done" then return end
    end
    local phase, left = game.storm()
    if phase ~= "calm" then
        warned = false -- the next calm counts down to the next storm
        return
    end
    if not warned and left <= props.lead then
        warned = true
        if props.message ~= "" then game.message(props.message) end
        if props.sound ~= "" then game.play_sound(props.sound) end
    end
end

function on_save(self) return { warned = warned } end
function on_load(self, d) warned = d and d.warned or false end
