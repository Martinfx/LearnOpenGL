#version 450 core
#define RAIN_MAP
layout(binding = 14) uniform sampler2D uRainMap;
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
// Terrain: up to 8 layers. Their albedo and normal maps are copied into two texture
// arrays (one slice per layer); the splat map is an array of two RGBA8 slices (layers 0-3, 4-7).
layout(binding = 10) uniform sampler2DArray uLayerAlbedo;
layout(binding = 11) uniform sampler2DArray uLayerNormal;
layout(binding = 12) uniform sampler2DArray uTerrainSplat;
uniform bool uTerrain;
uniform vec2 uTerrainOrigin;
uniform float uTerrainSize;
uniform float uLayerTiling[8];
uniform vec3 uLayerColor[8];       // sRGB
uniform int uLayerMaps;            // bits 0-7: albedo map, bits 8-15: normal map
uniform float uLayerRoughness[8];
uniform float uLayerNormalStrength[8];
uniform bool uHasTexture;
uniform bool uHasNormalMap;
uniform bool uHasRoughnessMap;
uniform bool uHasMetallicMap;
uniform bool uHasOcclusionMap;
uniform bool uHasEmissiveMap;
// Detail maps: x tiling (per base repeat), y strength, z fade-out distance (m).
layout(binding = 22) uniform sampler2D uDetailAlbedo;
layout(binding = 23) uniform sampler2D uDetailNormal;
uniform bool uHasDetailAlbedo;
uniform bool uHasDetailNormal;
uniform vec3 uDetail;
uniform int uRoughnessChannel; // 0 = R (separate map), 1 = G (glTF metal/rough)
uniform int uMetallicChannel;  // 0 = R, 2 = B (glTF)
uniform float uNormalStrength;
uniform float uRoughness;
uniform float uMetallic;
uniform bool uEmissive;        // unlit effect (muzzle flash, gizmo)
uniform int uAlphaMode;        // 0 opaque, 1 mask (cut below uAlphaCutoff), 2 blend
uniform float uAlphaCutoff;
uniform float uOpacity;        // multiplies the albedo map's alpha
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

#include "lights.glsl"
#include "probes.glsl"
#include "ssr.glsl"

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
    float shadow;
    if (cascade == 0) {
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                sum += texture(uShadowMap, vec4(c.xy + vec2(x, y) / size, float(cascade), c.z - bias));
        shadow = sum / 9.0;
    } else {
        // Far cascades: 4 bilinear PCF taps cover the same 3x3 texel area at half the cost.
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                sum += texture(uShadowMap, vec4(c.xy + (vec2(x, y) - 0.5) / size, float(cascade), c.z - bias));
        shadow = sum * 0.25;
    }
    // Fade out at the far end of the last cascade.
    float fade = smoothstep(uCascadeSplits[count - 1] * 0.85, uCascadeSplits[count - 1], viewDepth);
    return mix(shadow, 1.0, fade);
}

// Sum of the local lights reaching this fragment (GGX like the sun).
// `everyLight`: the first-person layer (own projection, right at the eye) skips the clusters.
vec3 localLights(vec3 worldPos, vec3 n, vec3 geomNormal, vec3 v, vec3 albedo, float rough, float metal, vec3 f0, bool everyLight) {
    if (uClusterDims.w == 0)
        return vec3(0.0);
    uvec2 cluster = uvec2(0u, uint(uClusterDims.w));
    if (!everyLight) {
        float depth = -(uView * vec4(worldPos, 1.0)).z;
        cluster = lightCluster(gl_FragCoord.xy * uViewport.zw, depth);
    }
    vec3 sum = vec3(0.0);
    float nv = max(dot(n, v), 1e-4);
    for (uint i = 0u; i < cluster.y; ++i) {
        LocalLight light = uLights[everyLight ? i : uLightIndices[cluster.x + i]];
        vec3 toLight = light.posRange.xyz - worldPos;
        float d2 = dot(toLight, toLight);
        float r2 = light.posRange.w * light.posRange.w;
        if (d2 >= r2)
            continue;
        float dist = sqrt(d2);
        vec3 l = toLight / max(dist, 1e-4);
        float nl = dot(n, l);
        if (nl <= 0.0)
            continue;
        // Inverse square with a smooth window to zero at the range.
        float window = clamp(1.0 - (d2 / r2) * (d2 / r2), 0.0, 1.0);
        float atten = window * window / (d2 + 1.0);
        if (light.colorType.w > 0.5) {
            float cd = dot(-l, light.dirShadow.xyz);
            float cone = clamp((cd - light.cone.x) * light.cone.y, 0.0, 1.0);
            atten *= cone * cone;
            if (light.cone.w > 0.5)
                atten *= flashlightCookie(light, cd);
        }
        if (atten <= 0.0)
            continue;
        if (light.dirShadow.w >= 0.0)
            atten *= localShadow(light, worldPos, geomNormal, dist);
        vec3 h = normalize(l + v);
        float nh = max(dot(n, h), 0.0);
        vec3 F = fresnelSchlick(max(dot(h, v), 0.0), f0);
        vec3 spec = distributionGGX(nh, rough * rough) * geometrySmith(nv, nl, rough) * F / (4.0 * nv * max(nl, 1e-4));
        vec3 diff = (1.0 - F) * (1.0 - metal) * albedo / PI;
        sum += (diff + spec) * light.colorType.rgb * nl * atten;
    }
    return sum;
}

uniform vec2 uFade; // dithered dissolve from x to y metres from the camera (cull distance); y 0 = off

void main() {
    if (uFade.y > 0.0) {
        float k = smoothstep(uFade.x, uFade.y, length(vWorldPos - uCameraPos.xyz));
        // Interleaved gradient noise: a fine, stable screen pattern (TAA-free dither).
        float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        if (k > noise)
            discard;
    }
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

    float terrainW[8];
    if (uTerrain) {
        vec2 st = (vWorldPos.xz - uTerrainOrigin) / uTerrainSize;
        vec4 a = texture(uTerrainSplat, vec3(st, 0.0)), b = texture(uTerrainSplat, vec3(st, 1.0));
        float sum = max(dot(a, vec4(1.0)) + dot(b, vec4(1.0)), 1e-4);
        a /= sum;
        b /= sum;
        for (int i = 0; i < 4; ++i) {
            terrainW[i] = a[i];
            terrainW[i + 4] = b[i];
        }
    }
    vec3 albedo = pow(max(uColor, vec3(0.0)), vec3(2.2));
    if (uTerrain) {
        // Layers under ~0.5 % weight are skipped: most pixels sample one or two.
        vec2 p = vWorldPos.xz;
        albedo = vec3(0.0);
        for (int i = 0; i < 8; ++i) {
            if (terrainW[i] < 0.004)
                continue;
            vec3 c = pow(max(uLayerColor[i], vec3(0.0)), vec3(2.2));
            if ((uLayerMaps & (1 << i)) != 0)
                c *= texture(uLayerAlbedo, vec3(p * uLayerTiling[i], float(i))).rgb;
            albedo += c * terrainW[i];
        }
    }
    float alpha = uOpacity;
    if (uHasTexture) {
        vec4 texel = texture(uAlbedoMap, uv);
        albedo *= texel.rgb;
        alpha *= texel.a;
    }
    // Detail maps, fading out with distance (the overlay layer is in view space).
    vec2 detailUv = uv * uDetail.x;
    float detailW = 0.0;
    if (uHasDetailAlbedo || uHasDetailNormal) {
        float d = uOverlay ? length(vWorldPos) : distance(vWorldPos, uCameraPos.xyz);
        detailW = uDetail.y * (1.0 - smoothstep(uDetail.z * 0.6, uDetail.z, d));
    }
    if (uHasDetailAlbedo && detailW > 0.0)
        albedo *= mix(1.0, texture(uDetailAlbedo, detailUv).r * 2.0, detailW);
    if (uAlphaMode == 1 && alpha < uAlphaCutoff && uShading != 3)
        discard; // leaves, fences, grates
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
    if (uHasDetailNormal && detailW > 0.0) {
        vec3 m;
        m.xy = texture(uDetailNormal, detailUv).xy * 2.0 - 1.0;
        m.z = sqrt(max(1.0 - dot(m.xy, m.xy), 0.0));
        m.xy *= detailW;
        n = perturbNormal(n, vWorldPos, detailUv, normalize(m));
    }
    if (uTerrain) {
        // Blend the layers' tangent-space normals by weight (screen-space frame from world X/Z).
        vec2 p = vWorldPos.xz;
        vec3 m = vec3(0.0);
        for (int i = 0; i < 8; ++i) {
            if (terrainW[i] < 0.004)
                continue;
            if ((uLayerMaps & (256 << i)) == 0) {
                m.z += terrainW[i];
                continue;
            }
            vec2 xy = texture(uLayerNormal, vec3(p * uLayerTiling[i], float(i))).xy * 2.0 - 1.0;
            m += terrainW[i] * vec3(xy * uLayerNormalStrength[i], sqrt(max(1.0 - dot(xy, xy), 0.0)));
        }
        m.z = max(m.z, 1e-3);
        n = perturbNormal(n, vWorldPos, p, normalize(m));
    }
    float rough = uRoughness;
    if (uTerrain) {
        rough = 0.0;
        for (int i = 0; i < 8; ++i)
            rough += terrainW[i] * uLayerRoughness[i];
    }
    if (uHasRoughnessMap)
        rough *= texture(uRoughnessMap, uv)[uRoughnessChannel];
    rough = clamp(rough, 0.04, 1.0);
    float metal = uMetallic;
    if (uHasMetallicMap)
        metal *= texture(uMetallicMap, uv)[uMetallicChannel];
    float ao = uHasOcclusionMap ? texture(uOcclusionMap, uv).r : 1.0;

    // Rain: surfaces open to the sky darken and turn glossy, flat ground collects puddles
    // with ripples while it rains; lightning lights everything under the open sky.
    float skyOpen = 1.0;
    if (!uOverlay && (uWeather.x > 0.0 || uWeather.y > 0.0)) {
        vec3 geo = normalize(vNormal) * (gl_FrontFacing ? 1.0 : -1.0);
        skyOpen = rainExposure(vWorldPos + geo * 0.05);
        float wet = uWeather.x * skyOpen * mix(0.4, 1.0, smoothstep(-0.2, 0.6, geo.y));
        if (wet > 0.0) {
            float porous = 1.0 - metal;
            albedo *= mix(1.0, 0.55, wet * porous);
            rough = mix(rough, 0.3, wet * 0.8);
            float mask = valueNoise(vWorldPos.xz * 0.3) * 0.7 + valueNoise(vWorldPos.xz * 1.3) * 0.3;
            float puddle = smoothstep(0.74 - uWeather.w * 0.2, 0.79 - uWeather.w * 0.2, mask) * smoothstep(0.9, 0.98, geo.y) *
                           smoothstep(0.4, 0.9, uWeather.x) * skyOpen;
            if (puddle > 0.0) {
                // Ripples: rings spreading from random points in a grid, one drop per cell per cycle.
                vec2 cellPos = vWorldPos.xz * 3.0;
                vec2 cell = floor(cellPos);
                float h = hash12(cell);
                float t = fract(uSkyHorizon.w * 1.3 + h);
                vec2 center = cell + vec2(hash12(cell + 1.7), hash12(cell + 4.1));
                vec2 d = cellPos - center;
                float r = length(d);
                float ring = sin((r - t * 0.9) * 40.0) * smoothstep(0.08, 0.0, abs(r - t * 0.9)) * (1.0 - t) * uWeather.x;
                vec3 water = geo + vec3(d.x, 0.0, d.y) / max(r, 1e-3) * ring * 0.25;
                n = normalize(mix(n, normalize(water), puddle));
                rough = mix(rough, 0.04, puddle);
                albedo *= mix(1.0, 0.75, puddle);
            }
        }
    }

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
    // Facing away from the sun: nothing to shadow (the sun term is zero anyway).
    float shadow = uOverlay || nl <= 0.0 ? 1.0 : sunShadow(vWorldPos, normalize(vNormal), nl);
    if (!uOverlay && nl > 0.0)
        shadow *= cloudShadow(vWorldPos);
    // Inside an interior volume the sky light is dimmed (the first-person layer: where the camera is,
    // and the sun too, as the overlay has no shadows).
    float interiorSky = interiorFactors(uOverlay ? uCameraPos.xyz : vWorldPos).x;
    if (uOverlay)
        shadow *= interiorSky;
    vec3 color = (diffuse + specular) * uSunColor.rgb * uSunDir.w * nl * shadow;
    if (uOverlay) // view space -> world
        color += localLights(uEnvRotation * vWorldPos + uCameraPos.xyz, uEnvRotation * n, uEnvRotation * normalize(vNormal), uEnvRotation * v,
                             albedo, rough, metal, f0, true);
    else
        color += localLights(vWorldPos, n, normalize(vNormal) * (gl_FrontFacing ? 1.0 : -1.0), v, albedo, rough, metal, f0, false);

    // Sky light: prefiltered environment for reflections, its blurriest level for diffuse.
    vec3 nWorld = uOverlay ? uEnvRotation * n : n;
    vec3 rWorld = uOverlay ? uEnvRotation * reflect(-v, n) : reflect(-v, n);
    float maxMip = uShadowParams.w;
    // Sky light (dimmed inside interiors), replaced by baked probes where there are any.
    vec3 irradiance = textureLod(uEnvMap, nWorld, maxMip).rgb * uGroundColor.w * interiorSky;
    vec3 prefiltered = textureLod(uEnvMap, rWorld, rough * maxMip).rgb * uGroundColor.w * interiorSky;
    {
        vec3 probePos = uOverlay ? uCameraPos.xyz : vWorldPos;
        float w;
        vec3 probeDiffuse = probeIrradiance(probePos, nWorld, w);
        irradiance = mix(irradiance, probeDiffuse, w);
        vec3 probeSpecular = probeReflection(probePos, rWorld, rough, w);
        prefiltered = mix(prefiltered, probeSpecular, w);
    }
    if (!uOverlay) { // what the screen shows replaces the probe / sky where the ray finds it
        vec4 ssr = screenReflection(vWorldPos, rWorld, rough, nv);
        prefiltered = mix(prefiltered, ssr.rgb, ssr.a);
    }
    vec2 brdf = texture(uBrdfLut, vec2(nv, rough)).rg;
    vec3 Fa = fresnelRoughness(nv, f0, rough);
    float occlusion = ao;
    if (!uOverlay && uCameraPos.w > 0.5)
        occlusion *= mix(1.0, texture(uSsao, gl_FragCoord.xy * uViewport.zw).r, uFogParams.w);
    // Direct light leaks less into crevices than sky light: soften AO on the sun term.
    color *= mix(1.0, occlusion, 0.3);
    vec3 ambient = ((1.0 - Fa) * (1.0 - metal) * albedo * irradiance + prefiltered * (Fa * brdf.x + brdf.y)) * occlusion;
    color += ambient;
    color += albedo * (1.0 - metal) * vec3(0.7, 0.75, 0.9) * uWeather.y * 0.45 * skyOpen * interiorSky * (0.4 + 0.6 * max(nWorld.y, 0.0)); // lightning

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
    fragColor = vec4(color, uAlphaMode == 2 ? clamp(alpha, 0.0, 1.0) : 1.0);
}
