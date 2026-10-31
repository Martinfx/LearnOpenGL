#version 450 core
// Editor debug geometry (navmesh, paths): plain triangles or lines from a buffer.
layout(std430, binding = 4) readonly buffer DebugVertices { vec4 debugVertices[]; };
uniform mat4 uDebugViewProj;

void main() {
    gl_Position = uDebugViewProj * vec4(debugVertices[gl_VertexID].xyz, 1.0);
}
