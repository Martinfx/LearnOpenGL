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
layout(binding = 6) uniform sampler2DArray uShadowMap; // raw depth (sampler object without compare: PCSS reads it)
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
// Parallax occlusion mapping: height map (white = high), the darkest texel uHeightDepth metres deep.
layout(binding = 30) uniform sampler2D uHeightMap;
uniform bool uHasHeightMap;
uniform float uHeightDepth;
uniform vec3 uDetail;
// Blend layers painted on the vertices (vPaint: red = layer 0, green = layer 1, blue = wet).
in vec4 vPaint;
in float vSway;
layout(binding = 24) uniform sampler2D uBlendAlbedo[2];
layout(binding = 26) uniform sampler2D uBlendNormal[2];
layout(binding = 28) uniform sampler2D uBlendRoughMap[2];
uniform int uBlendLayers;        // bit i: layer i, 2+i: its albedo map, 4+i: normal map, 6+i: roughness map
uniform vec3 uBlendColor[2];     // sRGB
uniform vec2 uBlendTiling[2];
uniform vec2 uBlendSurface[2];   // roughness, metallic
uniform float uBlendNormalStrength[2];
uniform float uBlendSharpness;
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

// Contact shadows: a short ray toward the sun through this frame's depth (the pre-pass copy
// SSR reads) finds small casters the shadow map is too coarse for - grass, kerbs, rubble,
// window frames, a foot on the ground. 1 = lit, 0 = in contact shadow.
float contactShadow(vec3 worldPos, vec3 n) {
    if (uContact.x <= 0.0)
        return 1.0;
    vec3 origin = (uView * vec4(worldPos + n * 0.01, 1.0)).xyz;
    float fade = 1.0 - smoothstep(uContact.z * 0.6, uContact.z, -origin.z);
    if (fade <= 0.0)
        return 1.0;
    vec3 dir = mat3(uView) * -normalize(uSunDir.xyz);
    const int kSteps = 10;
    float stepLen = uContact.x / float(kSteps);
    float t = stepLen * (0.3 + 0.7 * hash12(gl_FragCoord.xy + uSkyHorizon.w * 37.0));
    for (int i = 0; i < kSteps; ++i, t += stepLen) {
        vec3 p = origin + dir * t;
        if (p.z > -0.05)
            break;
        vec4 clip = uProj * vec4(p, 1.0);
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            break;
        float depth = textureLod(uSsrDepth, uv, 0.0).r;
        float behind = ssrViewZ(depth) - p.z; // > 0: the ray is behind a surface
        // A margin that grows with distance (depth precision) and a thickness: thin casters
        // only, so the ray does not hit the far side of the same wall.
        float margin = 0.004 * -p.z + 0.01;
        if (depth < 1.0 && behind > margin && behind < uContact.y)
            return 1.0 - fade * (1.0 - float(i) / float(kSteps) * 0.5);
    }
    return 1.0;
}

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

// Terrain anti-tiling: a second lookup of each layer, rotated and ~2.3x larger, offset per layer.
// Returns its mix weight (a slow noise, more of it with distance) and both coordinates.
const mat2 kTerrainRot = mat2(0.8, 0.6, -0.6, 0.8);
const float kTerrainScale = 0.43;
float terrainBomb(vec2 p, float tiling, float layer, float far, out vec2 uvA, out vec2 uvB) {
    uvA = p * tiling;
    uvB = kTerrainRot * (p * tiling * kTerrainScale) + vec2(0.37, 0.71) * (layer + 1.0);
    float k = smoothstep(0.35, 0.65, valueNoise(p * 0.07 + layer * 3.1));
    return mix(k, 0.5 + 0.5 * k, far);
}

// One bilinear PCF tap from four gathered depths (what a comparison sampler does in hardware).
float shadowTap(vec2 uv, float layer, float ref, vec2 size) {
    vec2 st = uv * size - 0.5;
    vec2 f = fract(st);
    vec4 lit = step(vec4(ref), textureGather(uShadowMap, vec3((floor(st) + 1.0) / size, layer)));
    // gather order: x (0,1), y (1,1), z (1,0), w (0,0)
    return mix(mix(lit.w, lit.z, f.x), mix(lit.x, lit.y, f.x), f.y);
}

const vec2 kPoisson16[16] = vec2[](
    vec2(-0.9420, -0.3991), vec2(0.9456, -0.7689), vec2(-0.0942, -0.9294), vec2(0.3450, 0.2939),
    vec2(-0.9159, 0.4577), vec2(-0.8154, -0.8791), vec2(-0.3828, 0.2768), vec2(0.9748, 0.7565),
    vec2(0.4432, -0.9751), vec2(0.5374, -0.4737), vec2(-0.2650, -0.4189), vec2(0.7920, 0.1909),
    vec2(-0.2419, 0.9971), vec2(-0.8141, 0.9144), vec2(0.1998, 0.7864), vec2(0.1438, -0.1410));

// Cascaded sun shadows with a normal offset bias. Near cascades (quality softShadows):
// percentage-closer soft shadows - the average depth of the casters around the point gives
// its distance to them, and the filter widens with it by the sun's angular radius
// (uShadowSoft.y, widened under clouds): sharp at the contact, soft far from the caster.
// Otherwise 3x3 PCF (cascade 0) or 4 bilinear taps.
// cheap: plain PCF (leaves and blades: the soft penumbra is lost in their own detail).
float sunShadow(vec3 worldPos, vec3 n, float nl, bool cheap) {
    if (uSunColor.w < 0.5)
        return 1.0;
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    int count = int(uCascadeSplits.w);
    int cascade = count - 1;
    for (int i = 0; i < count; ++i)
        if (viewDepth < uCascadeSplits[i]) { cascade = i; break; }
    if (viewDepth > uCascadeSplits[count - 1])
        return 1.0;
    vec3 offsetPos = worldPos + n * uShadowParams.y * (1.0 + float(cascade)) * (1.0 - nl * 0.5);
    vec4 p = uShadowMatrices[cascade] * vec4(offsetPos, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (c.z >= 1.0)
        return 1.0;
    float layer = float(cascade);
    float ref = c.z - uShadowParams.x * (1.0 + float(cascade));
    vec2 size = vec2(textureSize(uShadowMap, 0).xy);
    float shadow = 0.0;
    if (uShadowSoft.x > 0.5 && cascade < 2 && uShadowSoft.y > 0.0 && !cheap) {
        mat4 m = uShadowMatrices[cascade];
        float uvPerM = 0.5 * length(vec3(m[0][0], m[1][0], m[2][0]));
        float depthPerM = 0.5 * length(vec3(m[0][2], m[1][2], m[2][2]));
        float tanA = uShadowSoft.y;
        // A tilted receiver drops away from the light across the filter: bias by its slope.
        float slope = min(sqrt(max(1.0 - nl * nl, 0.0)) / max(nl, 0.05), 6.0) * depthPerM / uvPerM;
        float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy + 5.588238 * floor(uSkyHorizon.w * 60.0), vec2(0.06711056, 0.00583715))));
        mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
        float searchUv = clamp(tanA * uShadowSoft.z * uvPerM, 1.5 / size.x, 20.0 / size.x);
        float blockerSum = 0.0, blockers = 0.0;
        for (int i = 0; i < 16; i += 2) {
            vec2 o = rot * kPoisson16[i] * searchUv;
            vec4 d = textureGather(uShadowMap, vec3(c.xy + o, layer));
            vec4 b = step(d, vec4(ref - slope * length(o)) - 1e-6);
            blockerSum += dot(d, b);
            blockers += dot(b, vec4(1.0));
        }
        if (blockers < 0.5) {
            shadow = 1.0;
        } else {
            float distM = max(ref - blockerSum / blockers, 0.0) / depthPerM;
            float r = clamp(distM * tanA * uvPerM, 1.0 / size.x, min(uShadowSoft.w * uvPerM, 48.0 / size.x));
            int taps = uShadowSoft.x > 12.0 ? 16 : 8, stride = 16 / taps; // 8: every other point
            for (int i = 0; i < 16; i += stride) {
                vec2 o = rot * kPoisson16[i] * r;
                shadow += shadowTap(c.xy + o, layer, ref - slope * length(o), size);
            }
            shadow /= float(taps);
        }
    } else if (cascade == 0 && !cheap) {
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                shadow += shadowTap(c.xy + vec2(x, y) / size, layer, ref, size);
        shadow /= 9.0;
    } else {
        // Far cascades: 4 bilinear PCF taps cover the same 3x3 texel area at half the cost.
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                shadow += shadowTap(c.xy + (vec2(x, y) - 0.5) / size, layer, ref, size);
        shadow *= 0.25;
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

// Marches the view ray into the height field and returns the texture coordinate where it hits.
// The UV gradients come from screen-space derivatives (as perturbNormal), so world-projected and
// mesh UVs work alike and the depth is in metres whatever the tiling. Call in uniform control flow.
vec2 parallaxUv(vec2 uv, vec3 n, vec3 toEye) {
    vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    float det = dot(dp1, dp2perp);
    float dist = length(toEye);
    float fade = 1.0 - smoothstep(10.0, 22.0, dist);
    if (abs(det) < 1e-14 || fade <= 0.0)
        return uv;
    vec3 gu = (dp2perp * duv1.x + dp1perp * duv2.x) / det; // d(uv.x) / d(world position) along the surface
    vec3 gv = (dp2perp * duv1.y + dp1perp * duv2.y) / det;
    vec3 v = toEye / dist;
    float vn = dot(v, n);
    if (vn <= 0.02)
        return uv;
    vec3 vt = v - n * vn;
    // UV shift at full depth: following the ray `depth` metres under the surface moves along it by vt * depth / vn.
    vec2 shift = -vec2(dot(vt, gu), dot(vt, gv)) * (uHeightDepth * fade / max(vn, 0.2));
    float steps = mix(24.0, 6.0, vn);
    float stepSize = 1.0 / steps;
    float depth = 0.0, surface = 1.0 - textureGrad(uHeightMap, uv, duv1, duv2).r;
    float prevDepth = 0.0, prevSurface = surface;
    for (int i = 0; i < 24 && depth < surface; ++i) {
        prevDepth = depth;
        prevSurface = surface;
        depth += stepSize;
        surface = 1.0 - textureGrad(uHeightMap, uv + shift * depth, duv1, duv2).r;
    }
    // Between the last two samples the ray crossed the surface: intersect the two line pieces.
    float after = surface - depth, before = prevSurface - prevDepth;
    float t = clamp(before / max(before - after, 1e-5), 0.0, 1.0);
    return uv + shift * mix(prevDepth, depth, t);
}

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
    if (!gl_FrontFacing) {
        // Two-sided thin parts see their back face. Alpha-masked foliage (grass, leaves) keeps the up
        // component: blades lit with a bent-up normal stay lit from both sides like the ground under them.
        if (uAlphaMode == 1)
            n.xz = -n.xz;
        else
            n = -n;
    }
    vec2 uv = vUV;
    if (uWorldUv) {
        vec3 an = abs(n);
        uv = an.y > 0.5 ? vWorldPos.xz : (an.x > 0.5 ? vec2(vWorldPos.z, -vWorldPos.y) : vec2(vWorldPos.x, -vWorldPos.y));
    }
    if (uUvRotation != 0.0) {
        float c = cos(uUvRotation), s = sin(uUvRotation);
        uv = mat2(c, s, -s, c) * uv;
    }
    vec2 uv0 = uv;
    uv = uv * uTiling * uUvScale + uUvOffset;
    if (uHasHeightMap && uHeightDepth > 0.0 && !uTerrain)
        uv = parallaxUv(uv, n, uOverlay ? -vWorldPos : uCameraPos.xyz - vWorldPos);

    float terrainW[8];
    vec2 terrainDx = vec2(0.0), terrainDy = vec2(0.0);
    float terrainFar = 0.0;
    if (uTerrain) {
        // The splat lookup is pushed around by noise (up to ~1.5 m), so layer borders wander
        // organically instead of following the splat texels.
        vec2 p = vWorldPos.xz;
        terrainDx = dFdx(p);
        terrainDy = dFdy(p);
        terrainFar = smoothstep(12.0, 70.0, distance(vWorldPos, uCameraPos.xyz));
        vec2 warp = vec2(fbm(p * 0.19), fbm(p * 0.19 + vec2(31.7, 11.3))) - 0.5;
        warp += (vec2(valueNoise(p * 1.7), valueNoise(p * 1.7 + vec2(7.3, 3.1))) - 0.5) * 0.3;
        vec2 st = (p + warp * 3.2 - uTerrainOrigin) / uTerrainSize;
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
        // Each layer is read twice, at its tiling and at a rotated, ~2.3x larger copy, mixed by a
        // slow noise (more of the large copy far away), so no repeat shows. Layers then meet by
        // their texture height (the albedo's brightness): stones and tufts poke through the
        // neighbour first instead of a soft cross-fade. Layers under ~0.5 % weight are skipped.
        vec2 p = vWorldPos.xz;
        vec3 col[8];
        float hmax = -1.0, hgt[8];
        for (int i = 0; i < 8; ++i) {
            col[i] = vec3(0.0);
            hgt[i] = -1.0;
            if (terrainW[i] < 0.004)
                continue;
            vec3 c = pow(max(uLayerColor[i], vec3(0.0)), vec3(2.2));
            float h = 0.5;
            if ((uLayerMaps & (1 << i)) != 0) {
                vec2 uvA, uvB;
                float k = terrainBomb(p, uLayerTiling[i], float(i), terrainFar, uvA, uvB);
                vec3 tA = textureGrad(uLayerAlbedo, vec3(uvA, float(i)), terrainDx * uLayerTiling[i], terrainDy * uLayerTiling[i]).rgb;
                vec3 tB = textureGrad(uLayerAlbedo, vec3(uvB, float(i)), kTerrainRot * terrainDx * uLayerTiling[i] * kTerrainScale,
                                      kTerrainRot * terrainDy * uLayerTiling[i] * kTerrainScale).rgb;
                vec3 t = mix(tA, tB, k);
                c *= t;
                h = dot(sqrt(t), vec3(0.3, 0.59, 0.11));
            }
            col[i] = c;
            hgt[i] = terrainW[i] + h * 0.45;
            hmax = max(hmax, hgt[i]);
        }
        albedo = vec3(0.0);
        float wsum = 0.0;
        for (int i = 0; i < 8; ++i) {
            float w = hgt[i] < 0.0 ? 0.0 : max(hgt[i] - hmax + 0.2, 0.0);
            terrainW[i] = w;
            wsum += w;
        }
        for (int i = 0; i < 8; ++i) {
            terrainW[i] /= max(wsum, 1e-4);
            albedo += col[i] * terrainW[i];
        }
        // Large-scale variation: patches of lighter and darker, greener and more yellow ground
        // (tens of metres), so a meadow is not one flat colour to the horizon.
        float m1 = fbm(p * 0.017 + vec2(3.7, 1.9)), m2 = fbm(p * 0.043 + vec2(11.0, 5.0));
        // Kept gentle: stronger swings read as stains, not as a meadow drying out unevenly.
        albedo *= mix(0.9, 1.08, m1);
        albedo *= mix(vec3(1.035, 1.0, 0.93), vec3(0.97, 1.01, 1.03), m2);
    }
    float alpha = uOpacity;
    if (uHasTexture) {
        vec4 texel = texture(uAlbedoMap, uv);
        albedo *= texel.rgb;
        alpha *= texel.a;
    }
    // Painted blend layers: the edge follows the layer's brightness (moss and rust fill the
    // dark cracks first); full paint always shows the layer, none never does.
    float blendW[2] = float[2](0.0, 0.0);
    vec2 blendUv[2] = vec2[2](vec2(0.0), vec2(0.0));
    if (uBlendLayers != 0) {
        for (int i = 0; i < 2; ++i) {
            if ((uBlendLayers & (1 << i)) == 0 || vPaint[i] <= 0.0)
                continue;
            blendUv[i] = uv0 * uBlendTiling[i];
            vec3 c = pow(max(uBlendColor[i], vec3(0.0)), vec3(2.2));
            float h = 0.5;
            if ((uBlendLayers & (4 << i)) != 0) {
                vec3 t = texture(uBlendAlbedo[i], blendUv[i]).rgb;
                c *= t;
                h = clamp(dot(sqrt(t), vec3(0.3, 0.59, 0.11)), 0.0, 1.0);
            }
            float x = vPaint[i] * 1.6 - 0.3 + (0.5 - h) * 0.6;
            blendW[i] = clamp((x - 0.5) * uBlendSharpness + 0.5, 0.0, 1.0);
            albedo = mix(albedo, c, blendW[i]);
        }
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
    if (blendW[0] > 0.0 || blendW[1] > 0.0) {
        vec3 geo = normalize(vNormal) * (gl_FrontFacing ? 1.0 : -1.0);
        for (int i = 0; i < 2; ++i) {
            if (blendW[i] <= 0.0)
                continue;
            vec3 ln = geo;
            if ((uBlendLayers & (16 << i)) != 0) {
                vec3 m;
                m.xy = (texture(uBlendNormal[i], blendUv[i]).xy * 2.0 - 1.0) * uBlendNormalStrength[i];
                m.z = sqrt(max(1.0 - dot(m.xy, m.xy), 0.0));
                ln = perturbNormal(geo, vWorldPos, blendUv[i], normalize(m));
            }
            n = normalize(mix(n, ln, blendW[i]));
        }
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
            vec2 uvA, uvB;
            float k = terrainBomb(p, uLayerTiling[i], float(i), terrainFar, uvA, uvB);
            vec2 xyA = textureGrad(uLayerNormal, vec3(uvA, float(i)), terrainDx * uLayerTiling[i], terrainDy * uLayerTiling[i]).xy * 2.0 - 1.0;
            vec2 xyB = textureGrad(uLayerNormal, vec3(uvB, float(i)), kTerrainRot * terrainDx * uLayerTiling[i] * kTerrainScale,
                                   kTerrainRot * terrainDy * uLayerTiling[i] * kTerrainScale).xy * 2.0 - 1.0;
            vec2 xy = mix(xyA, transpose(kTerrainRot) * xyB, k); // back from the rotated copy's frame
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
    float metal = uMetallic;
    if (uHasMetallicMap)
        metal *= texture(uMetallicMap, uv)[uMetallicChannel];
    for (int i = 0; i < 2; ++i) {
        if (blendW[i] <= 0.0)
            continue;
        float lr = uBlendSurface[i].x;
        if ((uBlendLayers & (64 << i)) != 0)
            lr *= texture(uBlendRoughMap[i], blendUv[i]).r;
        rough = mix(rough, lr, blendW[i]);
        metal = mix(metal, uBlendSurface[i].y, blendW[i]);
    }
    if (vPaint.b > 0.0) { // painted wetness: damp corners, leaks, puddles
        albedo *= mix(1.0, 0.5, vPaint.b * (1.0 - metal));
        rough = mix(rough, 0.12, vPaint.b * 0.9);
    }
    rough = clamp(rough, 0.04, 1.0);
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
                           smoothstep(0.15, 0.55, uWeather.x) * skyOpen;   // a steady drizzle fills them too
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
    // Swaying alpha-tested foliage covers most of the screen in many tiny triangles (each shaded
    // as at least a 2x2 quad): it gets the cheaper sun shadow, no contact shadows and a coarser sky
    // occlusion, which its own detail hides.
    bool foliage = !uOverlay && uAlphaMode == 1 && vSway > 0.0;
    float shadow = uOverlay || nl <= 0.0 ? 1.0 : sunShadow(vWorldPos, normalize(vNormal), nl, foliage);
    if (!uOverlay && nl > 0.0)
        shadow *= cloudShadow(vWorldPos);
    if (!uOverlay && shadow > 0.01 && !foliage)
        shadow *= contactShadow(vWorldPos, normalize(vNormal));
    // Inside an interior volume the sky light is dimmed (the first-person layer: where the camera is,
    // and the sun too, as the overlay has no shadows).
    float interiorSky = interiorFactors(uOverlay ? uCameraPos.xyz : vWorldPos).x;
    if (uOverlay)
        shadow *= interiorSky;
    vec3 color = (diffuse + specular) * uSunColor.rgb * uSunDir.w * nl * shadow;
    // Leaves and blades are thin: sunlight comes through them and scatters inside a crown, so the far,
    // shadowed side of a tree is a deep green, not black, and a leaf against the sun glows.
    if (!uOverlay && uAlphaMode == 1 && vSway > 0.0) {
        float sh = nl > 0.0 ? shadow : sunShadow(vWorldPos, -normalize(vNormal), 0.5, true) * cloudShadow(vWorldPos);
        float through = 0.07 + 0.25 * pow(max(dot(-v, l), 0.0), 3.0);
        color += albedo * uSunColor.rgb * uSunDir.w * through * mix(0.4, 1.0, sh);
    }
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
    // Buildings and roofs hide part of the sky (large scale; baked probes already know it).
    float skyVis = uOverlay ? 1.0 : skyVisibility(vWorldPos + normalize(vNormal) * 0.05, nWorld, foliage ? 3 : int(max(uContact.w, 3.0)));
    vec3 irradiance = textureLod(uEnvMap, nWorld, maxMip).rgb * uGroundColor.w * interiorSky * skyVis;
    vec3 prefiltered = textureLod(uEnvMap, rWorld, rough * maxMip).rgb * uGroundColor.w * interiorSky * mix(sqrt(skyVis), skyVis, rough);
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
    vec3 bounce = vec3(0.0); // light from nearby surfaces on screen (SSGI)
    if (!uOverlay && uCameraPos.w > 0.5) {
        vec4 screenAo = texture(uSsao, gl_FragCoord.xy * uViewport.zw);
        occlusion *= mix(1.0, screenAo.a, uFogParams.w);
        bounce = screenAo.rgb * uProbeParams.z * ao;
    }
    // Direct light leaks less into crevices than sky light: soften AO on the sun term.
    color *= mix(1.0, occlusion, 0.3);
    // Energy of light scattered more than once between microfacets (Fdez-Aguera 2019): rough
    // metals (rust, old paint) keep their brightness instead of turning dark and dull.
    vec3 FssEss = Fa * brdf.x + brdf.y;
    float Ess = brdf.x + brdf.y;
    float Ems = 1.0 - Ess;
    vec3 Favg = f0 + (1.0 - f0) / 21.0;
    vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
    vec3 specularEnergy = FssEss + Fms * Ems;
    // Specular occlusion (Lagarde 2014): a crevice that hides the sky hides its reflection too,
    // more for glossy surfaces, so corners and gaps stop shining with the sky.
    float specOcclusion = clamp(pow(nv + occlusion, exp2(-16.0 * rough - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    vec3 ambient = (1.0 - specularEnergy) * (1.0 - metal) * albedo * (irradiance * occlusion + bounce) + prefiltered * specularEnergy * specOcclusion;
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
