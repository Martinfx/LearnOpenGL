#version 450 core
// Water surface: one quad (unit square around the origin) placed by uWaterModel.
#include "common.glsl"
uniform mat4 uWaterModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
void main() {
    const vec2 corners[6] = vec2[](vec2(-0.5, -0.5), vec2(-0.5, 0.5), vec2(0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5, 0.5), vec2(0.5, 0.5));
    vec4 world = uWaterModel * vec4(corners[gl_VertexID].x, 0.0, corners[gl_VertexID].y, 1.0);
    vWorldPos = world.xyz;
    gl_Position = uViewProj * world;
}
