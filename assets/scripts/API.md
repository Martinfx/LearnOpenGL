# Lua scripting API

Each entity with a script runs it in its own environment (globals are per entity).
Lua 5.4 with `base`, `math`, `string`, `table` and `coroutine` libraries.

## Callbacks

| Function | When |
| --- | --- |
| `on_start(self)` | Once when the level starts |
| `on_update(self, dt)` | Every simulation step (60 Hz, `dt` = 1/60 s) |
| `on_damage(self, amount, attacker)` | Hurt (health already reduced); `attacker` is `"player"`, the entity that did it, or `nil` (anomaly, trap) |
| `on_death(self, attacker)` | Health reached 0; the entity is removed from play |
| `on_save(self)` | Saving the game: return a table (numbers, strings, booleans, `vec3`, nested tables) to get back later |
| `on_load(self, data)` | After loading a saved game, with that table (health, position and facing are already restored) |
| `on_input(self, input, parameter, activator)` | Entity I/O: another entity's output (or `game.fire`) sent it `input`; `activator` is the entity that set it off, `nil` for the player. Built-in inputs (`TurnOn`, `Kill`...) arrive here too, after the engine handled them |

## Editor parameters

Declare tunables at the top of the script; the editor lists them for every
entity using the script and saves per-entity values in the level:

```lua
properties = {
    { "speed", 3.0, "Walk speed, m/s" },   -- name, default (number, boolean or string), description
}
function on_update(self, dt) self:move(self:forward(), props.speed) end
```

`props` holds the defaults overridden by the editor values.

## `vec3`

`vec3(x, y, z)`, fields `x y z`, operators `+ - *(number)` and unary `-`,
methods `:length()`, `:normalized()`, `:flat()` (y = 0), `:dot(other)`.

## `self` (the entity)

| Member | Description |
| --- | --- |
| `name`, `type` | From the editor (read only) |
| `position` | Base position, `vec3`; assigning teleports |
| `yaw` | Facing in degrees (0 = +X, 90 = +Z) |
| `color` | `vec3` 0..1 |
| `height` | Entity height in metres |
| `health` | Hit points (default 100), read/write |
| `alive` | `false` after death |
| `faction` | Faction id (`"stalker"`, `"bandit"`...; Properties > Character, default by type: enemy = bandit, npc = stalker, mutant = mutant), read/write |
| `group` | Group (squad) name from Properties > Character, `""` = none |
| `hates_player` | The player hurt it: it is hostile to the player whatever its faction thinks (saved), read/write |
| `handle` | Number unique to the entity in this run (compare entities with it; not saved) |
| `grounded` | Standing on the ground (characters only) |
| `:eye()` | Eye position |
| `:forward()` | Facing direction |
| `:enable_character()` | Gives the entity a capsule that walks with collision |
| `:move(direction, speed)` | Walk this step (characters) |
| `:jump(speed)` | Jump if grounded (characters) |
| `:face(point)` | Turn toward a point |
| `:log(text)` | Print with the entity name |
| `:get_property(key)` | Entity property as text (set in the editor, e.g. a light's `enabled`), `nil` if unset |
| `:set_property(key, value)` | Sets a property (`nil` removes it); a light with `enabled` = `"0"` goes dark, sit points use `occupied` |
| `:fire_output(name)` | Entity I/O: fires this entity's output `name` (e.g. `"OnSpotted"`); the connections are set in the editor's Outputs table, which also offers the names a script fires |
| `has_model` | Entity has an animated character model (Properties > Character model) |
| `:play(clip, opts)` | Play an animation clip; `opts` = `{ loop = true, speed = 1, fade = 0.2, restart = false }`. Calling it with the clip that already plays only updates loop/speed, so it is safe every update. Returns `false` if the model has no such clip |
| `animation` | Name of the clip playing now |
| `:animation_done()` | A non-looping clip reached its end (it then holds the last frame) |
| `animation_time` | Seconds into the current clip |
| `:has_animation(clip)` | The model has this clip |
| `:set_anim(param, value)` | Sets an animation graph parameter (number or bool); false if the entity has no graph or no such parameter |
| `:get_anim(param)` | Current value of an animation graph parameter |
| `:anim_trigger(param)` | Fires a graph trigger (`hit`, `shoot`); the transition that uses it consumes it |
| `:anim_state(layer)` | Name of the state graph layer `layer` is in (1 = base, default) |
| `:anim_event(name)` | A clip event of the graph (`footstep`, `mag_out`) fired in the last animation update |
| `:use_graph()` | Ends a clip started with `:play` and hands the pose back to the graph |
| `:move_to(target, speed)` | Walks along the navmesh around obstacles (re-plans as `target` moves); returns `true` on arrival. Without a navmesh it walks straight |
| `flash` | Seconds of white hit flash (e.g. `self.flash = 0.12` in `on_damage`) |

## `game`

| Function | Returns |
| --- | --- |
| `game.player_position()` / `game.player_eye()` | `vec3` |
| `game.player_health()` | number 0..100 |
| `game.damage_player(amount)` | |
| `game.damage(entity, amount, [attacker])` | Hurts an entity; `attacker` (an entity or `"player"`) is passed to its `on_damage` |
| `game.attitude(a, b)` | `"hostile"`, `"neutral"` or `"friendly"`; `a` and `b` are entities, `"player"` or faction ids (assets/factions.cfg, editor: Assets > Factions) |
| `game.relation(a, b)` | Relation -100..100 (the player's includes the goodwill earned) |
| `game.set_relation(a, b, value)` | Changes the relation of two factions for this game |
| `game.goodwill(faction)`, `game.change_goodwill(faction, delta)` | The player's standing with a faction (saved; shooting its members lowers it) |
| `game.player_faction()`, `game.faction_name(id)` | The player's faction id; a faction's display name |
| `game.entities_near(position, radius, [type])` | Living entities within `radius`, nearest first |
| `game.characters_near(position, radius)` | Living entities that belong to a faction (people, mutants), nearest first |
| `game.nav_ready()` | `true` once the level's navmesh is built (it builds in the background after loading) |
| `game.random_nav_point(center, radius)` | A reachable `vec3` within `radius`, or `nil` |
| `game.find_path(from, to)` | Table of `vec3` corners along the navmesh (empty when there is no way) |
| `game.play_sound(sound, [position], [volume])` | Plays a `.sound` or audio file (path relative to `assets/`, e.g. `"sounds/weapons/pistol_shot.sound"`) at a `vec3` position, or not positioned when `position` is `nil` |
| `game.time()` | Seconds since the level started |
| `game.can_see(from, to)` | `true` when no level geometry is in between |
| `game.raycast(origin, dir, max)` | `hit, point, distance` |
| `game.find(name)` | Entity or `nil` |
| `game.find_nearest(type, position, max_distance, free_key)` | Nearest living entity of a type (e.g. `"sit_point"`) within `max_distance` (optional); with `free_key` it skips entities whose property of that name is set |
| `game.log(text)`, `print(...)` | Prints to the game console |
| `game.fire(target, input, [parameter], [delay])` | Entity I/O: sends `input` to every entity named `target` (or `"!player"`) after `delay` seconds |
| `game.message(text)` | Shows a short message on the player's screen |
| `game.duck_ambience(gain, [seconds])` | Turns the ambience and music down to `gain` (0..1) for `seconds` (default 2), e.g. the moment before a scripted scare |
| `game.change_level(file)` | Loads another level (file in `assets/levels/` or absolute); health, weapon and magazines come along |
| `game.cvar(name)` | Console variable as text (`"1"`, `"0.5"`), nil if unknown |
| `game.set_cvar(name, value)` | Sets a console variable; false if unknown, out of range or a locked cheat |
| `game.debug_line(a, b, [color], [seconds])` | Debug line (also `debug_arrow`); color is a `vec3` 0..1, seconds 0 = this frame |
| `game.debug_box(center, half, [color], [seconds])` | Debug box (axis aligned, half size) |
| `game.debug_sphere(center, radius, [color], [seconds])` | Debug sphere (three circles) |
| `game.debug_text(position, text, [color], [seconds])` | Text floating at a world position |

## Entity I/O

Every entity has an Outputs table in the editor (Details > Script > Outputs): *when this
entity fires Output, every entity named Target receives Input (Parameter) after Delay
seconds, optionally only once*. The cvar `io_debug 1` logs every output and input.

| Outputs (engine) | When |
| --- | --- |
| `OnStart` | The level starts |
| `OnDamaged`, `OnDeath` | Hurt by a weapon, script or `Damage` input; health reached 0 |
| `OnTrigger` | Trigger volume: something its Filter allows entered (a trigger with Once then switches itself off). Relay: it got `Trigger` |
| `OnStartTouch`, `OnEndTouch` | Trigger volume: every entering and leaving |

| Inputs (engine) | Effect |
| --- | --- |
| `Enable`, `Disable`, `Toggle` (lights also `TurnOn`, `TurnOff`) | Property `enabled`: lights shine, sound sources play, particle effects emit, triggers react |
| `Trigger` | Relay or trigger: fires its `OnTrigger` |
| `Kill` | Removes the entity for the rest of the game (no `OnDeath`) |
| `Damage` | Hurts it by Parameter (default 100) |
| `SetProperty` | Parameter `key=value` |
| `ShowMessage` | Parameter (or the property `message`) on the player's screen |
| `PlaySound` | The `.sound` in Parameter (or the property `sound`) at the entity |

Targets: an entity name, `!self`, `!activator` (who set the output off) or `!player`
(inputs `Damage`, `Heal`, `ShowMessage`). Scripts add their own inputs in `on_input` and
outputs with `:fire_output`; `enemy.lua` understands `Alert` and `Calm` and fires `OnSpotted`.
Entity I/O state (switched lights, used-up triggers, inputs still waiting for their delay)
is kept in saved games.
