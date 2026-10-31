#version 450 core
// Shadow maps and the depth pre-pass only need depth, except that alpha-masked
// surfaces (leaves, fences) cut their holes here too.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uAlbedoMap;
uniform bool uHasTexture;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform float uOpacity;
uniform vec2 uTiling;
void main() {
    if (uAlphaMode == 1 && uHasTexture && texture(uAlbedoMap, vUV * uTiling).a * uOpacity < uAlphaCutoff)
        discard;
}
