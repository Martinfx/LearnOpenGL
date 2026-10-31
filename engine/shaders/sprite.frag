#version 450 core
#include "common.glsl"
in vec2 vUV;
in vec4 vColor;
in float vFrame;
in vec3 vWorld;
in vec3 vNormal;
layout(binding = 0) uniform sampler2D uSpriteTex;
uniform int uSpriteHasTexture;
uniform ivec2 uSpriteGrid;   // flipbook columns, rows
uniform int uSpriteLit;
uniform int uSpriteDecal;
uniform int uSpriteAdditive;
uniform int uSpriteNoFog;   // first-person layer (view space)
out vec4 fragColor;

vec4 cell(float index) {
    int count = max(uSpriteGrid.x * uSpriteGrid.y, 1);
    int i = int(mod(index, float(count)));
    vec2 g = vec2(i % uSpriteGrid.x, i / uSpriteGrid.x);
    // Keep half a texel off the cell edges so neighbours do not bleed in.
    vec2 size = vec2(textureSize(uSpriteTex, 0)) / vec2(uSpriteGrid);
    vec2 uv = clamp(vUV, 0.5 / size, 1.0 - 0.5 / size);
    return texture(uSpriteTex, (g + uv) / vec2(uSpriteGrid));
}

void main() {
    vec4 tex;
    if (uSpriteHasTexture != 0) {
        float f = floor(vFrame);
        tex = mix(cell(f), cell(f + 1.0), vFrame - f);
    } else { // soft round dot
        float d = length(vUV * 2.0 - 1.0);
        tex = vec4(1.0, 1.0, 1.0, pow(clamp(1.0 - d, 0.0, 1.0), 1.5));
    }
    vec4 color = tex * vColor;
    if (color.a < 0.003)
        discard;
    if (uSpriteLit != 0) {
        // Sun and sky without shadows: decals use their surface normal, particles face the camera
        // with a soft wrap so smoke lit from behind still reads.
        vec3 toLight = -normalize(uSunDir.xyz);
        vec3 n = uSpriteDecal != 0 ? normalize(vNormal) : normalize(uCameraPos.xyz - vWorld);
        float wrap = uSpriteDecal != 0 ? max(dot(n, toLight), 0.0) : 0.35 + 0.65 * max(dot(n, toLight) * 0.5 + 0.5, 0.0);
        vec3 sky = (uSkyZenith.rgb + uSkyHorizon.rgb) * 0.5 * uGroundColor.w;
        color.rgb *= uSunColor.rgb * uSunDir.w * wrap / PI + sky;
    }
    if (uSpriteNoFog != 0) {
    } else if (uSpriteAdditive != 0) {
        // Glow fades into the fog instead of turning fog-coloured.
        vec3 fogged = applyFog(vec3(0.0), vWorld);
        float f = clamp(length(fogged) / max(length(uFogColor.rgb), 1e-4), 0.0, 1.0);
        color.rgb *= 1.0 - f;
    } else {
        color.rgb = applyFog(color.rgb, vWorld);
    }
    fragColor = color;
}
