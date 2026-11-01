#pragma once

namespace engine {

struct Scene;

// Animals placed as one entity that stands for many: a "crow_flock" entity (Place panel >
// Crow flock) becomes `count` crows (property, default 6) of type "crow" scattered within
// `radius` m (default 5) around it when the game loads the level. Each crow is a copy of the
// flock entity (its character model, animation graph, script and properties: by default
// scripts/crow.lua, the boid flock that takes off when someone comes close or a shot is
// heard) with property `flock` = the flock's name. The copies get stable ids derived from the
// flock's, so saved games find them again. The flock entity itself stays as an editor helper.
// Returns how many animals were added.
int expandFlocks(Scene& scene);

} // namespace engine
