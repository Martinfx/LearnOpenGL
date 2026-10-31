#version 450 core
#include "common.glsl"
// Physically based shading (metal/roughness, GGX): sun with cascaded shadows,
// image based lighting from the sky (prefiltered cubemap + BRDF lookup), SSAO,
// normal maps with a screen-space tangent frame, height fog. Output is linear HDR;
// tone mapping and color grading happen in post.frag.
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
layout(binding = 0) uniform sampler2D uAlbedoMap;
layout(binding = 1) uniform sampler2D uNormalMap;
layout(binding = 2) uniform sampler2D uRoughnessMap;
layout(binding = 3) uniform sampler2D uMetallicMap;
layout(binding = 4) uniform sampler2D uOcclusionMap;
layout(binding = 5) uniform sampler2D uEmissiveMap;
layout(binding = 6) uniform sampler2DArrayShadow uShadowMap;
layout(binding = 7) uniform sampler2D uSsao;
layout(binding = 8) uniform samplerCube uEnvMap;
layout(binding = 9) uniform sampler2D uBrdfLut;
uniform bool uHasTexture;
uniform bool uHasNormalMap;
uniform bool uHasRoughnessMap;
uniform bool uHasMetallicMap;
uniform bool uHasOcclusionMap;
uniform bool uHasEmissiveMap;
uniform int uRoughnessChannel; // 0 = R (separate map), 1 = G (glTF metal/rough)
uniform int uMetallicChannel;  // 0 = R, 2 = B (glTF)
uniform float uNormalStrength;
uniform float uRoughness;
uniform float uMetallic;
uniform bool uEmissive;        // unlit effect (muzzle flash, gizmo)
uniform vec3 uColor;           // sRGB
uniform float uHighlight;
uniform float uFlash;
uniform vec2 uTiling;
uniform vec2 uUvScale;         // box texture tools: scale, offset (in tiles) and rotation (radians)
uniform vec2 uUvOffset;
uniform float uUvRotation;
uniform int uShading;          // 0 lit, 1 unlit, 2 lighting only, 3 wireframe
uniform bool uWorldUv;
uniform bool uGrid;
uniform vec3 uEmissiveColor;   // sRGB
uniform bool uOverlay;         // first-person layer: view space, no shadows/SSAO/fog
uniform mat3 uEnvRotation;     // overlay: view -> world for environment lookups
uniform vec3 uOverlaySunDir;   // overlay: sun direction in view space
out vec4 fragColor;

float gridLines(vec3 p, vec3 n) {
    vec2 uv = abs(n.y) > 0.5 ? p.xz : (abs(n.x) > 0.5 ? p.zy : p.xy);
    vec2 g = abs(fract(uv - 0.5) - 0.5) / fwidth(uv);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

// Tangent frame from screen-space derivatives (no vertex tangents needed).
vec3 perturbNormal(vec3 n, vec3 p, vec2 uv, vec3 mapNormal) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(t, t), dot(b, b)));
    if (isinf(invmax) || isnan(invmax))
        return n;
    return normalize(mat3(t * invmax, b * invmax, n) * mapNormal);
}

// Cascaded shadow map lookup with a 3x3 PCF kernel and normal offset bias.
float sunShadow(vec3 worldPos, vec3 n, float nl) {
    if (uSunColor.w < 0.5)
        return 1.0;
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    int count = int(uCascadeSplits.w);
    int cascade = count - 1;
    for (int i = 0; i < count; ++i)
        if (viewDepth < uCascadeSplits[i]) { cascade = i; break; }
    if (viewDepth > uCascadeSplits[count - 1])
        return 1.0;
    float texel = uShadowParams.z * float(1 << cascade);
    vec3 offsetPos = worldPos + n * uShadowParams.y * (1.0 + float(cascade)) * (1.0 - nl * 0.5);
    vec4 p = uShadowMatrices[cascade] * vec4(offsetPos, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (c.z >= 1.0)
        return 1.0;
    float bias = uShadowParams.x * (1.0 + float(cascade));
    float sum = 0.0;
    vec2 size = vec2(textureSize(uShadowMap, 0).xy);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadowMap, vec4(c.xy + vec2(x, y) / size, float(cascade), c.z - bias));
    float shadow = sum / 9.0;
    // Fade out at the far end of the last cascade.
    float fade = smoothstep(uCascadeSplits[count - 1] * 0.85, uCascadeSplits[count - 1], viewDepth);
    return mix(shadow, 1.0, fade);
}

void main() {
    if (uEmissive) {
        fragColor = vec4(pow(max(uColor, vec3(0.0)), vec3(2.2)) * 4.0, 1.0); // bright enough to bloom
        return;
    }
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
        n = -n; // two-sided foliage and thin parts
    vec2 uv = vUV;
    if (uWorldUv) {
        vec3 an = abs(n);
        uv = an.y > 0.5 ? vWorldPos.xz : (an.x > 0.5 ? vec2(vWorldPos.z, -vWorldPos.y) : vec2(vWorldPos.x, -vWorldPos.y));
    }
    if (uUvRotation != 0.0) {
        float c = cos(uUvRotation), s = sin(uUvRotation);
        uv = mat2(c, s, -s, c) * uv;
    }
    uv = uv * uTiling * uUvScale + uUvOffset;

    vec3 albedo = pow(max(uColor, vec3(0.0)), vec3(2.2));
    if (uHasTexture)
        albedo *= texture(uAlbedoMap, uv).rgb;
    else if (uGrid)
        albedo *= 1.0 - 0.18 * gridLines(vWorldPos, n);
    if (uShading == 1) { // unlit: base color only
        fragColor = vec4(mix(albedo, vec3(1.0, 0.75, 0.2), uHighlight * 0.5), 1.0);
        return;
    }
    if (uShading == 3) { // wireframe: lines in a light tint of the base color, selection in orange
        vec3 line = mix(vec3(0.55), sqrt(clamp(albedo, 0.0, 1.0)), 0.5);
        fragColor = vec4(mix(line, vec3(2.0, 1.0, 0.15), uHighlight), 1.0);
        return;
    }
    if (uShading == 2)
        albedo = vec3(0.5); // lighting only
    if (uHasNormalMap) {
        // X and Y only, Z rebuilt: works for RGB maps and two-channel BC5 maps alike.
        vec3 m;
        m.xy = texture(uNormalMap, uv).xy * 2.0 - 1.0;
        m.z = sqrt(max(1.0 - dot(m.xy, m.xy), 0.0));
        m.xy *= uNormalStrength;
        n = perturbNormal(n, vWorldPos, uv, normalize(m));
    }
    float rough = uRoughness;
    if (uHasRoughnessMap)
        rough *= texture(uRoughnessMap, uv)[uRoughnessChannel];
    rough = clamp(rough, 0.04, 1.0);
    float metal = uMetallic;
    if (uHasMetallicMap)
        metal *= texture(uMetallicMap, uv)[uMetallicChannel];
    float ao = uHasOcclusionMap ? texture(uOcclusionMap, uv).r : 1.0;

    vec3 camera = uOverlay ? vec3(0.0) : uCameraPos.xyz;
    vec3 v = normalize(camera - vWorldPos);
    vec3 l = uOverlay ? normalize(-uOverlaySunDir) : normalize(-uSunDir.xyz);
    vec3 h = normalize(l + v);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 1e-4);
    float nh = max(dot(n, h), 0.0);
    vec3 f0 = mix(vec3(0.04), albedo, metal);

    // Sun.
    vec3 F = fresnelSchlick(max(dot(h, v), 0.0), f0);
    vec3 specular = distributionGGX(nh, rough * rough) * geometrySmith(nv, nl, rough) * F / (4.0 * nv * max(nl, 1e-4));
    vec3 diffuse = (1.0 - F) * (1.0 - metal) * albedo / PI;
    float shadow = uOverlay ? 1.0 : sunShadow(vWorldPos, normalize(vNormal), nl);
    vec3 color = (diffuse + specular) * uSunColor.rgb * uSunDir.w * nl * shadow;

    // Sky light: prefiltered environment for reflections, its blurriest level for diffuse.
    vec3 nWorld = uOverlay ? uEnvRotation * n : n;
    vec3 rWorld = uOverlay ? uEnvRotation * reflect(-v, n) : reflect(-v, n);
    float maxMip = uShadowParams.w;
    vec3 irradiance = textureLod(uEnvMap, nWorld, maxMip).rgb;
    vec3 prefiltered = textureLod(uEnvMap, rWorld, rough * maxMip).rgb;
    vec2 brdf = texture(uBrdfLut, vec2(nv, rough)).rg;
    vec3 Fa = fresnelRoughness(nv, f0, rough);
    float occlusion = ao;
    if (!uOverlay && uCameraPos.w > 0.5)
        occlusion *= mix(1.0, texture(uSsao, gl_FragCoord.xy * uViewport.zw).r, uFogParams.w);
    // Direct light leaks less into crevices than sky light: soften AO on the sun term.
    color *= mix(1.0, occlusion, 0.3);
    vec3 ambient = ((1.0 - Fa) * (1.0 - metal) * albedo * irradiance + prefiltered * (Fa * brdf.x + brdf.y)) * occlusion;
    color += ambient * uGroundColor.w;

    vec3 emissive = pow(max(uEmissiveColor, vec3(0.0)), vec3(2.2));
    if (uHasEmissiveMap)
        emissive *= texture(uEmissiveMap, uv).rgb;
    color += emissive;

    color = mix(color, vec3(1.0, 0.75, 0.2), uHighlight * 0.35);
    color = mix(color, vec3(2.0), uFlash); // hit feedback
    if (!uOverlay)
        color = applyFog(color, vWorldPos);
    // A single NaN/Inf pixel would spread over the whole screen through bloom: drop it here.
    color = any(isnan(color)) || any(isinf(color)) ? vec3(0.0) : min(color, vec3(60000.0));
    fragColor = vec4(color, 1.0);
}
