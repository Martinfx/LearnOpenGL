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
// Terrain: layers 1..3 albedo (layer 0 uses uAlbedoMap). Layer normals use the normal,
// roughness, metallic and emissive units (0..3); the splat map uses the occlusion unit.
layout(binding = 10) uniform sampler2D uLayer1Albedo;
layout(binding = 11) uniform sampler2D uLayer2Albedo;
layout(binding = 12) uniform sampler2D uLayer3Albedo;
uniform bool uTerrain;
uniform vec2 uTerrainOrigin;
uniform float uTerrainSize;
uniform vec4 uLayerTiling;
uniform vec3 uLayerColor[4];       // sRGB
uniform ivec4 uLayerHasAlbedo;
uniform ivec4 uLayerHasNormal;
uniform vec4 uLayerRoughness;
uniform vec4 uLayerNormalStrength;
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

// Local lights, clustered: the screen is split into tiles and depth slices; each
// cluster lists the lights whose range reaches it (built on the CPU every frame).
struct LocalLight {
    vec4 posRange;   // xyz, w: range
    vec4 colorType;  // rgb linear * intensity, w: 0 point, 1 spot
    vec4 dirShadow;  // xyz spot axis, w: first shadow layer (-1 = none)
    vec4 cone;       // x: cos outer, y: 1 / (cos inner - cos outer), z: shadow normal offset per m, w: 1 = flashlight cookie
};
layout(std430, binding = 5) readonly buffer LocalLights { LocalLight uLights[]; };
layout(std430, binding = 6) readonly buffer LightGrid {
    vec4 uClusterParams;  // x: slice scale, y: slice bias (slice = log(depth) * x + y), z: near
    ivec4 uClusterDims;   // x, y, z, w: light count
    uvec2 uClusters[];    // offset into uLightIndices, count
};
layout(std430, binding = 7) readonly buffer LightIndices { uint uLightIndices[]; };
layout(std430, binding = 8) readonly buffer LightShadows { mat4 uLightShadowMatrices[]; };
layout(binding = 13) uniform sampler2DArrayShadow uLocalShadowMap;

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

// Shadow of a local light (spot: one layer, point: the cube face the fragment is on).
float localShadow(LocalLight light, vec3 worldPos, vec3 geomNormal, float dist) {
    int layer = int(light.dirShadow.w);
    if (light.colorType.w < 0.5) {
        vec3 d = worldPos - light.posRange.xyz;
        vec3 a = abs(d);
        if (a.x >= a.y && a.x >= a.z)
            layer += d.x > 0.0 ? 0 : 1;
        else if (a.y >= a.z)
            layer += d.y > 0.0 ? 2 : 3;
        else
            layer += d.z > 0.0 ? 4 : 5;
    }
    vec3 offsetPos = worldPos + geomNormal * dist * light.cone.z * 1.5;
    vec4 p = uLightShadowMatrices[layer] * vec4(offsetPos, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (any(lessThan(c, vec3(0.0))) || any(greaterThan(c, vec3(1.0))))
        return 1.0;
    vec2 size = vec2(textureSize(uLocalShadowMap, 0).xy);
    float sum = 0.0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            sum += texture(uLocalShadowMap, vec4(c.xy + (vec2(x, y) - 0.5) / size, float(layer), c.z - 0.00005));
    return sum * 0.25;
}

// Sum of the local lights reaching this fragment (GGX like the sun).
// `everyLight`: the first-person layer (own projection, right at the eye) skips the clusters.
vec3 localLights(vec3 worldPos, vec3 n, vec3 geomNormal, vec3 v, vec3 albedo, float rough, float metal, vec3 f0, bool everyLight) {
    if (uClusterDims.w == 0)
        return vec3(0.0);
    uvec2 cluster = uvec2(0u, uint(uClusterDims.w));
    if (!everyLight) {
        float depth = -(uView * vec4(worldPos, 1.0)).z;
        ivec3 cell;
        cell.xy = clamp(ivec2(gl_FragCoord.xy * uViewport.zw * vec2(uClusterDims.xy)), ivec2(0), uClusterDims.xy - 1);
        cell.z = clamp(int(floor(log(max(depth, uClusterParams.z)) * uClusterParams.x + uClusterParams.y)), 0, uClusterDims.z - 1);
        cluster = uClusters[cell.x + cell.y * uClusterDims.x + cell.z * uClusterDims.x * uClusterDims.y];
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
            if (light.cone.w > 0.5) {
                // Flashlight reflector: hot centre, a darker ring, a faint bright rim (t = 1 at the edge).
                float t = acos(clamp(cd, -1.0, 1.0)) / max(acos(light.cone.x), 1e-3);
                atten *= 0.55 + 0.9 * exp(-t * t * 18.0) - 0.18 * exp(-pow((t - 0.42) * 9.0, 2.0)) + 0.12 * exp(-pow((t - 0.72) * 10.0, 2.0));
            }
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

    vec4 terrainW = vec4(0.0);
    if (uTerrain) {
        terrainW = texture(uOcclusionMap, (vWorldPos.xz - uTerrainOrigin) / uTerrainSize);
        terrainW /= max(dot(terrainW, vec4(1.0)), 1e-4);
    }
    vec3 albedo = pow(max(uColor, vec3(0.0)), vec3(2.2));
    if (uTerrain) {
        vec2 p = vWorldPos.xz;
        vec3 c0 = pow(max(uLayerColor[0], vec3(0.0)), vec3(2.2));
        vec3 c1 = pow(max(uLayerColor[1], vec3(0.0)), vec3(2.2));
        vec3 c2 = pow(max(uLayerColor[2], vec3(0.0)), vec3(2.2));
        vec3 c3 = pow(max(uLayerColor[3], vec3(0.0)), vec3(2.2));
        if (uLayerHasAlbedo.x != 0) c0 *= texture(uAlbedoMap, p * uLayerTiling.x).rgb;
        if (uLayerHasAlbedo.y != 0) c1 *= texture(uLayer1Albedo, p * uLayerTiling.y).rgb;
        if (uLayerHasAlbedo.z != 0) c2 *= texture(uLayer2Albedo, p * uLayerTiling.z).rgb;
        if (uLayerHasAlbedo.w != 0) c3 *= texture(uLayer3Albedo, p * uLayerTiling.w).rgb;
        albedo = c0 * terrainW.x + c1 * terrainW.y + c2 * terrainW.z + c3 * terrainW.w;
    }
    float alpha = uOpacity;
    if (uHasTexture) {
        vec4 texel = texture(uAlbedoMap, uv);
        albedo *= texel.rgb;
        alpha *= texel.a;
    }
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
    if (uTerrain) {
        // Blend the layers' tangent-space normals by weight (screen-space frame from world X/Z).
        vec2 p = vWorldPos.xz;
        vec3 m = vec3(0.0, 0.0, 1.0) * dot(terrainW, vec4(1.0) - vec4(uLayerHasNormal));
        vec4 strength = uLayerNormalStrength;
        if (uLayerHasNormal.x != 0) { vec2 xy = texture(uNormalMap, p * uLayerTiling.x).xy * 2.0 - 1.0; m += terrainW.x * vec3(xy * strength.x, sqrt(max(1.0 - dot(xy, xy), 0.0))); }
        if (uLayerHasNormal.y != 0) { vec2 xy = texture(uRoughnessMap, p * uLayerTiling.y).xy * 2.0 - 1.0; m += terrainW.y * vec3(xy * strength.y, sqrt(max(1.0 - dot(xy, xy), 0.0))); }
        if (uLayerHasNormal.z != 0) { vec2 xy = texture(uMetallicMap, p * uLayerTiling.z).xy * 2.0 - 1.0; m += terrainW.z * vec3(xy * strength.z, sqrt(max(1.0 - dot(xy, xy), 0.0))); }
        if (uLayerHasNormal.w != 0) { vec2 xy = texture(uEmissiveMap, p * uLayerTiling.w).xy * 2.0 - 1.0; m += terrainW.w * vec3(xy * strength.w, sqrt(max(1.0 - dot(xy, xy), 0.0))); }
        n = perturbNormal(n, vWorldPos, p, normalize(m));
    }
    float rough = uTerrain ? dot(terrainW, uLayerRoughness) : uRoughness;
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
    // Facing away from the sun: nothing to shadow (the sun term is zero anyway).
    float shadow = uOverlay || nl <= 0.0 ? 1.0 : sunShadow(vWorldPos, normalize(vNormal), nl);
    if (!uOverlay && nl > 0.0)
        shadow *= cloudShadow(vWorldPos);
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
    fragColor = vec4(color, uAlphaMode == 2 ? clamp(alpha, 0.0, 1.0) : 1.0);
}
