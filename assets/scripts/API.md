# Lua scripting API

Each entity with a script runs it in its own environment (globals are per entity).
Lua 5.4 with `base`, `math`, `string`, `table` and `coroutine` libraries.

## Callbacks

| Function | When |
| --- | --- |
| `on_start(self)` | Once when the level starts |
| `on_update(self, dt)` | Every simulation step (60 Hz, `dt` = 1/60 s) |
| `on_damage(self, amount)` | Hit by the player's weapon (health already reduced) |
| `on_death(self)` | Health reached 0; the entity is removed from play |

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
| `grounded` | Standing on the ground (characters only) |
| `:eye()` | Eye position |
| `:forward()` | Facing direction |
| `:enable_character()` | Gives the entity a capsule that walks with collision |
| `:move(direction, speed)` | Walk this step (characters) |
| `:jump(speed)` | Jump if grounded (characters) |
| `:face(point)` | Turn toward a point |
| `:log(text)` | Print with the entity name |

## `game`

| Function | Returns |
| --- | --- |
| `game.player_position()` / `game.player_eye()` | `vec3` |
| `game.player_health()` | number 0..100 |
| `game.damage_player(amount)` | |
| `game.time()` | Seconds since the level started |
| `game.can_see(from, to)` | `true` when no level geometry is in between |
| `game.raycast(origin, dir, max)` | `hit, point, distance` |
| `game.find(name)` | Entity or `nil` |
| `game.log(text)`, `print(...)` | Prints to the game console |
