-- Corpse: a character model lying where it fell. Plays its death clip once, fast, and holds the last frame.
-- Attach to any entity with a character model (Properties > Character model), e.g. a stalker who did not come back.

properties = {
    { "clip", "Death01", "Death clip of the model whose last frame is the pose" },
}

local posed = false

function on_update(self, dt)
    if posed then return end
    posed = true
    if self.has_model and self:has_animation(props.clip) then
        self:play(props.clip, { loop = false, speed = 40, fade = 0 })
    end
end
