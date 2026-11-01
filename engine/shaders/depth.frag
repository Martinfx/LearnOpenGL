#version 450 core
#include "common.glsl"
// Shadow maps and the depth pre-pass only need depth, except that alpha-masked
// surfaces (leaves, fences) cut their holes here too, and the dithered fade at the
// cull distance matches pbr.frag (else the main pass leaves black holes in front of the sky).
in vec3 vWorldPos;
in vec2 vUV;
layout(binding = 0) uniform sampler2D uAlbedoMap;
uniform bool uHasTexture;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform float uOpacity;
uniform vec2 uTiling;
uniform vec2 uFade;
void main() {
    if (uFade.y > 0.0) {
        float k = smoothstep(uFade.x, uFade.y, length(vWorldPos - uCameraPos.xyz));
        float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        if (k > noise)
            discard;
    }
    if (uAlphaMode == 1 && uHasTexture && texture(uAlbedoMap, vUV * uTiling).a * uOpacity < uAlphaCutoff)
        discard;
}
