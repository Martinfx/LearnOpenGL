#version 450 core
// Water surface (rivers, ponds, flooded cellars): refraction of the scene behind with
// depth-based absorption, screen-space reflections falling back to the environment
// probe, sun glints, shoreline foam. Drawn after the opaque pass from copies of its
// colour and depth.
#include "common.glsl"
in vec3 vWorldPos;
layout(binding = 0) uniform sampler2D uSceneColor;
layout(binding = 1) uniform sampler2D uSceneDepth;
layout(binding = 8) uniform samplerCube uEnvMap;
uniform vec3 uWaterColor;  // linear: the colour deep water scatters
uniform float uClarity;    // metres of water until most light is gone
uniform vec2 uFlow;        // surface drift, m/s (rivers)
uniform float uWaves;      // 0 still pond .. 1 choppy
uniform float uFoam;       // shoreline foam 0..1
uniform float uReflection; // 0..1 strength of reflections
out vec4 fragColor;

vec3 worldAt(vec2 uv, float depth) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// Ripples: scrolling value-noise octaves, normal from the gradient.
float ripples(vec2 p, float t) {
    float h = valueNoise(p * 0.35 + uFlow * t * 0.35) * 0.6;
    h += valueNoise(p * 1.1 - uFlow.yx * t * 0.5 + vec2(t * 0.05, 0.0)) * 0.3;
    h += valueNoise(p * 3.3 + uFlow * t * 1.1 + vec2(0.0, t * 0.11)) * 0.12;
    h += valueNoise(p * 8.0 + uFlow * t * 2.0 - vec2(t * 0.2)) * 0.05 * (0.5 + uWind.y);
    return h;
}

void main() {
    float t = uSkyHorizon.w;
    vec3 toCam = uCameraPos.xyz - vWorldPos;
    float dist = length(toCam);
    vec3 v = toCam / max(dist, 1e-4);
    bool below = uCameraPos.y < vWorldPos.y;
    // Normal: stronger ripples up close, calmer far away (no shimmer at the horizon).
    float e = 0.05;
    vec2 p = vWorldPos.xz;
    float h0 = ripples(p, t), hx = ripples(p + vec2(e, 0.0), t), hz = ripples(p + vec2(0.0, e), t);
    float amp = (0.08 + 0.55 * uWaves + 0.25 * uWind.y) / (1.0 + dist * 0.04);
    vec3 n = normalize(vec3(-(hx - h0) / e * amp, 1.0, -(hz - h0) / e * amp));
    if (below)
        n = -n;
    float nv = max(dot(n, v), 0.0);

    vec2 screen = gl_FragCoord.xy * uViewport.zw;
    float surfaceDepth = gl_FragCoord.z;
    // Refraction: offset by the normal, but never pull in things in front of the water.
    vec2 refrUv = screen + n.xz * 0.04 / (1.0 + dist * 0.05);
    float behindDepth = texture(uSceneDepth, refrUv).r;
    if (behindDepth < surfaceDepth) {
        refrUv = screen;
        behindDepth = texture(uSceneDepth, screen).r;
    }
    vec3 behind = worldAt(refrUv, behindDepth);
    float thickness = behindDepth >= 1.0 ? 1e4 : length(behind - vWorldPos);
    float vertical = behindDepth >= 1.0 ? 1e4 : max(vWorldPos.y - behind.y, 0.0);
    vec3 sceneColor = texture(uSceneColor, refrUv).rgb;
    // Absorption: red goes first, then green; light scattered back in the water's colour.
    vec3 absorb = vec3(1.0, 0.55, 0.38) / max(uClarity, 0.05);
    vec3 transmit = below ? vec3(1.0) : exp(-absorb * thickness); // from below: air beyond (the volume pass tints the water)
    vec3 sunDir = -normalize(uSunDir.xyz);
    float daylight = uSunDir.w * max(sunDir.y, 0.0) * 0.12 + uGroundColor.w * 0.35;
    vec3 inscatter = uWaterColor * (uSunColor.rgb * daylight + uSkyZenith.rgb * uGroundColor.w * 0.4) * 4.0; // light scattered by silt
    vec3 refracted = sceneColor * transmit + inscatter * (1.0 - transmit);

    // Reflection: march the reflected ray in screen space against the depth copy.
    vec3 r = reflect(-v, n);
    if (!below)
        r.y = abs(r.y); // reflections always go up from above
    float maxMip = uShadowParams.w;
    vec3 reflected = textureLod(uEnvMap, r, clamp(uWaves * 0.25, 0.0, 1.0) * maxMip).rgb * uGroundColor.w;
    if (!below) {
        float stepLen = 0.35 + dist * 0.02;
        vec3 pos = vWorldPos + r * stepLen * (0.5 + hash12(gl_FragCoord.xy) * 0.5);
        for (int i = 0; i < 28; ++i) {
            vec4 clip = uProj * uView * vec4(pos, 1.0);
            if (clip.w <= 0.0)
                break;
            vec3 ndc = clip.xyz / clip.w;
            vec2 uv = ndc.xy * 0.5 + 0.5;
            if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
                break;
            float sceneZ = texture(uSceneDepth, uv).r;
            float rayZ = ndc.z * 0.5 + 0.5;
            if (sceneZ < rayZ && sceneZ < 1.0) {
                // Hit if the surface found is close to the ray (not a far wall seen through a gap).
                vec3 hit = worldAt(uv, sceneZ);
                if (length(hit - pos) < stepLen * 2.5 && sceneZ > surfaceDepth) {
                    vec2 edge = min(uv, 1.0 - uv);
                    float fade = clamp(min(edge.x, edge.y) * 12.0, 0.0, 1.0) * (1.0 - float(i) / 28.0);
                    reflected = mix(reflected, texture(uSceneColor, uv).rgb, fade);
                }
                break;
            }
            pos += r * stepLen;
            stepLen *= 1.18;
        }
    }
    float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    if (below) { // looking up: total internal reflection (the murky water itself) outside the window to the sky
        fresnel = 1.0 - smoothstep(0.6, 0.72, nv) * (1.0 - fresnel);
        reflected = inscatter;
    }
    fresnel *= uReflection;
    vec3 color = mix(refracted, reflected, fresnel);

    // Sun (or moon) glints on the ripples.
    if (!below) {
        vec3 hv = normalize(sunDir + v);
        float spec = distributionGGX(max(dot(n, hv), 0.0), 0.02 + 0.06 * uWaves) * 0.25;
        color += uSunColor.rgb * uSunDir.w * spec * fresnel * cloudShadow(vWorldPos) * step(0.0, sunDir.y + 0.05);
    }

    // Foam where the water is shallow (banks, around stones) and stirred by flow.
    // Depth for the shoreline: between straight down and along the view, so a wall seen
    // at a grazing angle gets a thin line, not a band.
    float shore = mix(vertical, thickness, 0.6);
    float shallow = 1.0 - smoothstep(0.0, 0.08 + 0.17 * uFoam, shore);
    float foamNoise = valueNoise(p * 5.0 + uFlow * t * 1.5) * valueNoise(p * 11.0 - uFlow * t + vec2(t * 0.1));
    float foam = uFoam * shallow * smoothstep(0.2, 0.55, foamNoise + shallow * 0.3);
    vec3 foamColor = (uSunColor.rgb * daylight * 3.0 + uSkyZenith.rgb * uGroundColor.w * 0.5) * vec3(0.85, 0.85, 0.8);
    color = mix(color, foamColor, foam * 0.6);

    // Soft edge where the water meets the ground (no hard line).
    float edgeFade = clamp(vertical * 12.0, 0.0, 1.0);
    color = mix(sceneColor, color, below ? 1.0 : edgeFade);
    if (!below)
        color = applyFog(color, vWorldPos);
    color = any(isnan(color)) || any(isinf(color)) ? vec3(0.0) : min(color, vec3(60000.0));
    fragColor = vec4(color, 1.0);
}
