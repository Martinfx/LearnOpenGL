#version 450 core
// Final image: exposure (manual x auto), bloom, ACES tone mapping, color grading
// (saturation, tint, white balance, contrast, lift/gamma/gain, 3D LUT), vignette
// and film grain, then gamma.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uBloom;
uniform float uExposure;
uniform float uAutoExposure;  // 0 manual .. 1 fully automatic
layout(binding = 3) uniform sampler2D uAdapted; // r: auto-exposure multiplier
uniform vec3 uWhiteBalance;   // multiplier from temperature and green/magenta tint
uniform vec3 uLift;           // added to the shadows
uniform vec3 uGamma;          // midtones (> 1 brighter)
uniform vec3 uGain;           // multiplies the highlights
layout(binding = 4) uniform sampler3D uLut;
uniform float uLutStrength;   // 0 = no LUT
uniform float uLutSize;
uniform float uBloomIntensity;
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uTint;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;
uniform bool uLumaAlpha;   // luma in alpha for FXAA; opaque when drawing straight to the screen
// Anomaly warps: xy = center (uv), z = radius (uv height), w = strength (> 0 heat shimmer, < 0 lens).
layout(binding = 2) uniform sampler2D uDepth;
uniform int uDistortCount;
uniform vec4 uDistort[8];
uniform float uAspect;
uniform bool uDistortMask; // depth < 1 is the first-person weapon (own depth layer): not warped
out vec4 fragColor;

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 distortedUV(vec2 uv) {
    if (uDistortCount == 0 || (uDistortMask && texture(uDepth, uv).r < 1.0))
        return uv;
    vec2 offset = vec2(0.0);
    for (int i = 0; i < uDistortCount; ++i) {
        vec4 d = uDistort[i];
        vec2 v = uv - d.xy;
        v.x *= uAspect;
        float dist = length(v) / d.z;
        if (dist >= 1.0)
            continue;
        float fall = 1.0 - smoothstep(0.35, 1.0, dist);
        if (d.w > 0.0) {
            // Heat: fine rising ripples, stronger toward the bottom of the haze.
            vec2 p = uv / d.z;
            vec2 wobble = vec2(sin(p.y * 38.0 - uTime * 9.0 + sin(p.x * 21.0) * 1.7),
                               cos(p.x * 29.0 + uTime * 6.0 + p.y * 13.0)) ;
            offset += wobble * fall * d.w * d.z * 0.022;
        } else {
            // Lens: pulls the image toward the middle and twists it a little.
            vec2 dir = v / max(length(v), 1e-5);
            vec2 swirl = vec2(-dir.y, dir.x);
            float bend = sin(dist * 3.14159) * fall * (-d.w);
            vec2 o = (dir * 0.16 + swirl * 0.05 * sin(uTime * 1.3 + dist * 6.0)) * bend * d.z;
            o.x /= uAspect;
            offset += o;
        }
    }
    vec2 result = clamp(uv + offset, vec2(0.001), vec2(0.999));
    // Do not pull the weapon into the world around it.
    if (uDistortMask && texture(uDepth, result).r < 1.0)
        return uv;
    return result;
}

void main() {
    vec3 color = texture(uHdr, distortedUV(vUV)).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;
    if (any(isnan(color)) || any(isinf(color))) color = vec3(0.0);
    if (any(isnan(bloom)) || any(isinf(bloom))) bloom = vec3(0.0);
    color += bloom * uBloomIntensity;
    float autoExposure = uAutoExposure > 0.0 ? texelFetch(uAdapted, ivec2(0), 0).r : 1.0;
    color *= uExposure * mix(1.0, autoExposure, uAutoExposure);
    color = aces(color);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, uSaturation);
    color *= uTint * uWhiteBalance;
    vec2 d = vUV - 0.5;
    color *= 1.0 - uVignette * smoothstep(0.2, 0.85, dot(d, d) * 2.0);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    // Contrast in display space, so dark colors are not crushed to black.
    color = clamp((color - 0.5) * uContrast + 0.5, 0.0, 1.0);
    // Lift / gamma / gain (display space).
    color = clamp(uGain * (color + uLift * (1.0 - color)), 0.0, 1.0);
    color = pow(color, 1.0 / max(uGamma, vec3(0.01)));
    if (uLutStrength > 0.0) {
        vec3 graded = texture(uLut, color * ((uLutSize - 1.0) / uLutSize) + 0.5 / uLutSize).rgb;
        color = mix(color, graded, uLutStrength);
    }
    color += (hash(gl_FragCoord.xy + fract(uTime) * 1000.0) - 0.5) * uGrain;
    // Luma in alpha for FXAA. The window must get alpha 1: Qt composites the editor view with it.
    fragColor = vec4(color, uLumaAlpha ? dot(color, vec3(0.299, 0.587, 0.114)) : 1.0);
}
