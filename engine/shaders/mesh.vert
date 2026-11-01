#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in uint aInstance; // base instance + instance number (mesh pool VAO)
#include "common.glsl"
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
void main() {
    mat4 skin = mat4(1.0);
    if (uSkinned)
        skin = aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y]
             + aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
    mat4 model = uInstanced ? uInstances[uInstanceBase + int(aInstance)] : uModel;
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
    }
    vWorldPos = world.xyz;
    vNormal = mat3(model) * mat3(skin) * aNormal; // renormalized in the fragment shader
    vUV = aUV;
    vPaint = uPaintBase >= 0 ? unpackUnorm4x8(uPaint[uPaintBase + gl_VertexID - uPaintBaseVertex]) : vec4(0.0);
    gl_Position = uViewProj * world;
}
