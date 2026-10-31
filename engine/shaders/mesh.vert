#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(std140, binding = 0) uniform Joints { mat4 uJoints[256]; };
uniform bool uSkinned;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
void main() {
    mat4 skin = mat4(1.0);
    if (uSkinned)
        skin = aWeights.x * uJoints[aJoints.x] + aWeights.y * uJoints[aJoints.y]
             + aWeights.z * uJoints[aJoints.z] + aWeights.w * uJoints[aJoints.w];
    vec4 world = uModel * skin * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(uModel) * mat3(skin) * aNormal; // renormalized in the fragment shader
    vUV = aUV;
    gl_Position = uViewProj * world;
}
