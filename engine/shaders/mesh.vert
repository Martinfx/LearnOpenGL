#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in uint aInstance; // base instance + instance number (mesh pool VAO)
// Trees (vegetation/source/treegen2.py): r crown occlusion, g branch flexibility, b branch phase, a leaf flutter.
layout(location = 6) in vec4 aFoliage;
#include "common.glsl"
// The depth pre-pass and the lit pass compile this separately: identical depth in both, so the
// lit pass can test against the pre-pass depth without writing it.
invariant gl_Position;
layout(std140, binding = 0) uniform Joints { mat4 uJoints[256]; };
uniform float uSway; // bending in the wind: 0 rigid, ~0.3 trees, 1 grass
uniform bool uSkinned;
uniform mat4 uModel;
// Instancing and multi-draw: transforms from this buffer, at the draw's base instance.
uniform bool uInstanced;
uniform int uInstanceBase;
layout(std430, binding = 2) readonly buffer Instances { mat4 uInstances[]; };
uniform mat4 uViewProj;
// Vertex paint of a placed model (engine/VertexPaint.h): RGBA8 per vertex of this mesh part.
layout(std430, binding = 3) readonly buffer Paint { uint uPaint[]; };
uniform int uPaintBase = -1;   // first entry of this part, -1 = no paint
uniform int uPaintBaseVertex;  // the part's first vertex in the mesh pool
out vec4 vPaint;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out float vSway; // the fragment shader lets light through thin leaves
out float vFoliageAo; // baked crown occlusion (1 = open)
// LOD crossfade: an instance's share dissolved (> 0: this level fades out, < 0: fades in),
// carried in its transform's unused m[0][3] (Renderer: both levels drawn within the band).
flat out float vDissolve;
void main() {
    vSway = uSway;
    vFoliageAo = aFoliage.r;
    mat4 skin = mat4(1.0);
    if (uSkinned)
        skin = aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y]
             + aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
    mat4 model = uInstanced ? uInstances[uInstanceBase + int(aInstance)] : uModel;
    vDissolve = model[0][3];
    model[0][3] = 0.0;
    vec4 world = model * skin * vec4(aPosition, 1.0);
    if (uSway > 0.0 && uWind.y > 0.0) {
        // Bend away from the wind with height above the object's origin: slow sway with
        // gusts rolling across the level along the wind, plus a quick flutter on top.
        vec3 base = model[3].xyz;
        float h = max(world.y - base.y, 0.0);
        vec2 dir = uWind.xz;
        float t = uSkyHorizon.w;
        float phase = dot(base.xz, vec2(0.37, 0.23));
        float gust = 1.0 + uWind.w * (0.8 * sin(t * 0.5 - dot(base.xz, dir) * 0.04) + 0.4 * sin(t * 1.3 - dot(base.xz, dir) * 0.11));
        float sway = (0.55 + 0.45 * sin(t * (1.1 + 0.6 * uWind.y) + phase)) * gust;
        float bend = uSway * uWind.y * (0.06 * min(h, 1.0) + 0.012 * h * h) * sway;
        float flutter = uSway * uWind.y * min(h, 1.5) * 0.015 * sin(t * 7.0 + dot(world.xyz, vec3(2.1, 1.7, 2.9)));
        world.xz += dir * bend + vec2(-dir.y, dir.x) * flutter;
        world.y -= bend * bend * 0.5 / max(h, 0.5); // keep the length roughly
        if (aFoliage.g > 0.0 || aFoliage.a > 0.0) {
            // Tree hierarchy: every limb swings on its own phase (and its shoots with it), more toward the
            // outer twigs; the leaves at a twig's tip flutter fast and lightly. Gusts drive both.
            float ph = aFoliage.b * 6.2832;
            float strength = uWind.y * gust;
            float flex = aFoliage.g * aFoliage.g;
            vec2 across = vec2(-dir.y, dir.x);
            float swing = sin(t * (1.6 + 0.5 * uWind.y) + ph) + 0.4 * sin(t * (3.1 + 0.4 * uWind.y) + ph * 2.3);
            vec2 limb = dir * (0.55 + 0.45 * swing) * 0.35 + across * 0.18 * sin(t * 1.3 + ph * 1.7);
            world.xz += limb * flex * strength * 1.2;
            world.y += 0.12 * sin(t * 2.2 + ph * 3.1) * flex * strength;
            vec3 wn = normalize(mat3(model) * aNormal);
            float leaf = aFoliage.a * strength * (0.035 + 0.03 * uWind.w);
            world.xyz += wn * leaf * sin(t * 11.0 + ph * 13.0 + dot(world.xyz, vec3(3.1, 2.3, 2.7)))
                       + vec3(across.x, 0.0, across.y) * leaf * 0.6 * sin(t * 8.3 + ph * 7.0 + world.y * 4.0);
        }
    }
    if (uSway > 0.0 && uBenderInfo.x > 0.0) {
        // Benders (anomalies): plants lean away from, toward or around a centre, the
        // tips most, fading out to the bender's radius. Whole plants lean by their origin.
        vec3 base = model[3].xyz;
        float h = max(world.y - base.y, 0.0);
        float t = uSkyHorizon.w;
        vec2 shift = vec2(0.0);
        for (int i = 0; i < int(uBenderInfo.x); ++i) {
            vec4 a = uBenders[2 * i], b = uBenders[2 * i + 1];
            vec2 d = base.xz - a.xz;
            float dist = length(d);
            if (dist >= a.w || abs(base.y - a.y) > a.w + 2.0)
                continue;
            vec2 away = dist > 1e-3 ? d / dist : vec2(1.0, 0.0);
            float fall = 1.0 - smoothstep(a.w * 0.35, a.w, dist);
            float shake = 0.85 + 0.15 * sin(t * 11.0 + dist * 5.0 + base.x);
            float lean = b.x * fall * shake * 0.35;
            if (b.y < 0.5)
                shift += away * lean;
            else if (b.y < 1.5)
                shift -= away * lean * min(1.0, dist / max(a.w * 0.25, 0.1)); // no flip at the centre
            else
                shift += (vec2(-away.y, away.x) * 0.8 - away * 0.3) * lean;
        }
        float len = length(shift);
        if (len > 0.6)
            shift *= 0.6 / len;
        float tip = min(h, 1.2) * min(1.0, uSway * 1.5);
        world.xz += shift * tip;
        world.y -= dot(shift, shift) * tip * 0.5 / max(h, 0.5);
    }
    vWorldPos = world.xyz;
    vNormal = mat3(model) * mat3(skin) * aNormal; // renormalized in the fragment shader
    vUV = aUV;
    vPaint = uPaintBase >= 0 ? unpackUnorm4x8(uPaint[uPaintBase + gl_VertexID - uPaintBaseVertex]) : vec4(0.0);
    gl_Position = uViewProj * world;
}
