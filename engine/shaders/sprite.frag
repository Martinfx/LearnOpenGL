#version 450 core
#define RAIN_MAP
layout(binding = 14) uniform sampler2D uRainMap;
#include "common.glsl"
in vec2 vUV;
in vec4 vColor;
in float vFrame;
in vec3 vWorld;
in vec3 vNormal;
in vec3 vLightRight;
in vec3 vLightLeft;
in vec3 vLightUp;
in vec3 vLightDown;
in vec3 vLightFront;
in vec3 vLightBack;
layout(binding = 0) uniform sampler2D uSpriteTex;
layout(binding = 1) uniform sampler2D uSceneDepth;  // opaque depth (soft particles)
layout(binding = 2) uniform sampler2D uSpriteLightMap; // six-way: r below, g front, b behind
uniform int uSpriteHasTexture;
uniform ivec2 uSpriteGrid;   // flipbook columns, rows
uniform int uSpriteLit;
uniform int uSpriteDecal;
uniform int uSpriteAdditive;
uniform int uSpriteNoFog;   // first-person layer (view space)
uniform int uSpriteSheltered; // rain: not under roofs
uniform int uSpriteSixWay;    // uSpriteTex rgb = light from right, left, above; uSpriteLightMap rgb = below, front, behind
uniform float uSpriteSoft;    // 1 / metres over which the particle fades into the geometry behind it, 0 = hard
uniform float uSpriteNearFade; // metres from the eye where particles fade out (no wall of smoke in the face)
out vec4 fragColor;

vec2 cellUv(float index) {
    int count = max(uSpriteGrid.x * uSpriteGrid.y, 1);
    int i = int(mod(index, float(count)));
    vec2 g = vec2(i % uSpriteGrid.x, i / uSpriteGrid.x);
    vec2 size = vec2(textureSize(uSpriteTex, 0)) / vec2(uSpriteGrid);
    vec2 uv = clamp(vUV, 0.5 / size, 1.0 - 0.5 / size);
    return (g + uv) / vec2(uSpriteGrid);
}

void main() {
    vec4 tex;
    vec3 sixB = vec3(0.0);
    if (uSpriteHasTexture != 0) {
        float f = floor(vFrame);
        float blend = vFrame - f;
        vec2 uv0 = cellUv(f), uv1 = cellUv(f + 1.0);
        tex = mix(texture(uSpriteTex, uv0), texture(uSpriteTex, uv1), blend);
        if (uSpriteSixWay != 0)
            sixB = mix(texture(uSpriteLightMap, uv0).rgb, texture(uSpriteLightMap, uv1).rgb, blend);
    } else { // soft round dot
        float d = length(vUV * 2.0 - 1.0);
        tex = vec4(1.0, 1.0, 1.0, pow(clamp(1.0 - d, 0.0, 1.0), 1.5));
    }
    if (uSpriteSheltered != 0 && rainExposure(vWorld) < 0.5)
        discard;
    float alpha = tex.a * vColor.a;
    // Soft particles: fade where the quad cuts into the scene and right in front of the eye.
    float eyeDist = -(uView * vec4(vWorld, 1.0)).z;
    if (uSpriteSoft > 0.0) {
        float d = texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r;
        float sceneDist = uProj[3][2] / ((d * 2.0 - 1.0) + uProj[2][2]); // view distance of the opaque surface
        alpha *= clamp((sceneDist - eyeDist) * uSpriteSoft, 0.0, 1.0);
    }
    if (uSpriteNearFade > 0.0)
        alpha *= clamp((eyeDist - uSpriteNearFade * 0.3) / (uSpriteNearFade * 0.7), 0.0, 1.0);
    if (alpha < 0.003)
        discard;
    vec4 color;
    if (uSpriteLit != 0 && uSpriteDecal == 0) {
        // Six-way: the smoke's own light maps; otherwise a soft ball (top lighter than the bottom).
        vec3 a = uSpriteSixWay != 0 ? tex.rgb : vec3(0.55, 0.55, 0.65);
        vec3 b = uSpriteSixWay != 0 ? sixB : vec3(0.4, 0.6, 0.45);
        vec3 light = a.r * vLightRight + a.g * vLightLeft + a.b * vLightUp + b.r * vLightDown + b.g * vLightFront + b.b * vLightBack;
        vec3 albedo = uSpriteSixWay != 0 ? vec3(1.0) : tex.rgb;
        color = vec4(albedo * vColor.rgb * light, alpha);
    } else {
        color = vec4(tex.rgb * vColor.rgb, alpha);
        if (uSpriteLit != 0) { // decals: sun and sky on their surface normal
            vec3 toLight = -normalize(uSunDir.xyz);
            float wrap = max(dot(normalize(vNormal), toLight), 0.0);
            vec3 sky = (uSkyZenith.rgb + uSkyHorizon.rgb) * 0.5 * uGroundColor.w * interiorFactors(vWorld).x;
            color.rgb *= uSunColor.rgb * uSunDir.w * wrap / PI + sky;
        }
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
