#version 450 core
// Debug geometry (navmesh, paths, DebugDraw): triangles or lines from a buffer.
// w = 1.0, or the vertex colour packed as RGBA8 bits (uDebugVertexColor).
layout(std430, binding = 4) readonly buffer DebugVertices { vec4 debugVertices[]; };
uniform mat4 uDebugViewProj;
uniform vec4 uDebugColor;
uniform bool uDebugVertexColor;
out vec4 vColor;

void main() {
    vec4 v = debugVertices[gl_VertexID];
    vColor = uDebugVertexColor ? unpackUnorm4x8(floatBitsToUint(v.w)) : uDebugColor;
    gl_Position = uDebugViewProj * vec4(v.xyz, 1.0);
}
