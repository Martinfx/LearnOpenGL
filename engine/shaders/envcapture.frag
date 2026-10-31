#version 450 core
#include "common.glsl"
// Renders the sky (without the sun disk) into one cubemap face.
in vec2 vUV;
uniform mat3 uFace; // face orientation: (right, up, forward)
out vec4 fragColor;
void main() {
    vec2 p = vUV * 2.0 - 1.0;
    vec3 dir = normalize(uFace * vec3(p.x, p.y, 1.0));
    fragColor = vec4(skyColor(dir, false), 1.0);
}
