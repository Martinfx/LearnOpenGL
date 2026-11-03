#version 450 core
// Final image: exposure (manual x auto), bloom, tone mapping (ACES / AgX / neutral), color grading
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
uniform int uTonemap;         // 0 ACES filmic, 1 AgX, 2 Khronos PBR Neutral
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uTint;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;
uniform bool uLumaAlpha;   // luma in alpha for FXAA; opaque when drawing straight to the screen
// Air distortion (heat haze, anomaly lenses): screen offsets in pixels from distort.frag.
layout(binding = 2) uniform sampler2D uDepth;
layout(binding = 5) uniform sampler2D uDistortTex;
uniform bool uDistortOn;
uniform bool uDistortMask; // depth < 1 is the first-person weapon (own depth layer): not warped
// Camera motion blur (optional, game): each pixel is smeared along how far it moved on screen
// since the previous frame (scene depth reprojected), times the shutter fraction.
layout(binding = 6) uniform sampler2D uSceneDepth; // depth of the world (before the weapon layer)
uniform float uMotionBlur;     // 0 = off .. 1 = a whole frame of motion
uniform mat4 uMotionReproject; // this frame's clip -> previous frame's clip
uniform float uSharpen;        // scene drawn below the output size: 0 off .. 1 strong
uniform float uChromatic;      // lens colour fringes toward the edges, 0 .. 1
uniform float uLensDirt;       // smudges on the lens lit by bloom, 0 .. 1
uniform vec4 uScreenFx;        // x hit, y low health, z radiation, w rain drops on the lens (0..1 each)
uniform vec4 uDof;             // depth of field: x focus (m, 0 off), y max blur (px), zw projection [2][2], [3][2]
out vec4 fragColor;

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// AgX (Troy Sobotka; polynomial fit by Benjamin Wrensch): bright saturated light (fire,
// a low sun, anomalies) fades toward white instead of skewing to a hard yellow or cyan.
vec3 agx(vec3 x) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    vec3 v = clamp(log2(max(inset * x, vec3(1e-10))), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    vec3 v2 = v * v, v4 = v2 * v2;
    v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4 - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
    return clamp(pow(max(outset * v, vec3(0.0)), vec3(2.2)), 0.0, 1.0); // back to linear
}

// Khronos PBR Neutral: colours stay as authored up to bright highlights (no hue shifts).
vec3 pbrNeutral(vec3 c) {
    const float start = 0.76, desaturation = 0.15;
    float x = min(c.r, min(c.g, c.b));
    c -= x < 0.08 ? x - 6.25 * x * x : 0.04;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < start)
        return c;
    const float d = 1.0 - start;
    float newPeak = 1.0 - d * d / (peak + d - start);
    c *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(c, vec3(newPeak), g);
}

// The operators scaled so that middle grey comes out alike (0.18 -> 0.27, as with ACES):
// switching keeps the exposure of a level.
vec3 tonemap(vec3 x) {
    if (uTonemap == 1)
        return agx(x * 1.334);
    if (uTonemap == 2)
        return clamp(pbrNeutral(x * 1.706), 0.0, 1.0);
    return aces(x);
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 distortedUV(vec2 uv) {
    if (!uDistortOn || (uDistortMask && texture(uDepth, uv).r < 1.0))
        return uv;
    vec2 offset = texture(uDistortTex, uv).rg / vec2(textureSize(uDistortTex, 0));
    vec2 result = clamp(uv + offset, vec2(0.001), vec2(0.999));
    // Do not pull the weapon into the world around it.
    if (uDistortMask && texture(uDepth, result).r < 1.0)
        return uv;
    return result;
}

// Contrast-adaptive sharpening of the scene stretched to the window (in the spirit of AMD CAS):
// a cross of source texels, stronger where the neighbourhood is flat, never past its min/max.
vec3 sharpened(vec2 uv, vec3 c) {
    if (uSharpen <= 0.0)
        return c;
    vec2 t = 1.0 / vec2(textureSize(uHdr, 0));
    vec3 n = texture(uHdr, uv + vec2(0.0, t.y)).rgb, s = texture(uHdr, uv - vec2(0.0, t.y)).rgb;
    vec3 e = texture(uHdr, uv + vec2(t.x, 0.0)).rgb, w = texture(uHdr, uv - vec2(t.x, 0.0)).rgb;
    vec3 lo = min(c, min(min(n, s), min(e, w)));
    vec3 hi = max(c, max(max(n, s), max(e, w)));
    // HDR: the contrast relative to the brightness (lo/hi), so bright sky edges don't ring.
    vec3 amount = sqrt(clamp(lo / max(hi, vec3(1e-4)), 0.0, 1.0));
    vec3 k = -amount * mix(0.125, 0.2, uSharpen) * uSharpen;
    vec3 r = (c + k * (n + s + e + w)) / (1.0 + 4.0 * k);
    return clamp(r, lo, hi);
}

float aspect() {
    vec2 size = vec2(textureSize(uHdr, 0));
    return size.x / max(size.y, 1.0);
}

// Rain drops on the lens: two layers of cells, a drop in some of them that appears, sits and
// dries; inside a drop the image is seen through it upside down (a small lens). Returns the UV
// offset, `wet` the drop coverage (0..1).
vec2 lensRainOffset(vec2 uv, out float wet) {
    wet = 0.0;
    float amount = uScreenFx.w;
    if (amount <= 0.0)
        return vec2(0.0);
    vec2 offset = vec2(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        float scale = layer == 0 ? 7.0 : 13.0;
        vec2 p = uv * vec2(aspect(), 1.0) * scale + float(layer) * 3.7;
        vec2 cell = floor(p);
        vec2 f = fract(p) - 0.5;
        float h = hash(cell + float(layer) * 13.1);
        if (h > amount * 0.75)
            continue;
        vec2 center = (vec2(hash(cell + 1.7), hash(cell + 5.3)) - 0.5) * 0.55;
        float life = fract(uTime * (0.08 + 0.06 * h) + h * 7.0);
        // Older drops run down a little before they dry.
        center.y += smoothstep(0.55, 1.0, life) * 0.35;
        float radius = mix(0.1, 0.28, hash(cell + 9.1)) * (layer == 0 ? 1.0 : 0.75);
        vec2 dv = (f - center) * vec2(1.0, 1.25);
        float d = length(dv) / radius;
        float drop = (1.0 - smoothstep(0.75, 1.0, d)) * smoothstep(0.0, 0.04, life) * (1.0 - smoothstep(0.7, 1.0, life));
        offset -= dv / (scale * vec2(aspect(), 1.0)) * drop * 1.6;
        wet = max(wet, drop);
    }
    return offset;
}

// Smudges and specks on the lens (procedural), shown where bloom lights them.
float lensDirtMask(vec2 uv) {
    vec2 p = uv * vec2(aspect(), 1.0);
    float m = 0.0;
    for (int i = 0; i < 3; ++i) {
        float scale = 2.5 + float(i) * 4.5;
        vec2 q = p * scale + float(i) * 11.3;
        vec2 c = floor(q);
        vec2 f = fract(q) - 0.5;
        if (hash(c + 0.3) < 0.45)
            continue;
        vec2 o = (vec2(hash(c + 3.1), hash(c + 7.7)) - 0.5) * 0.5;
        float r = 0.12 + 0.3 * hash(c + 1.9);
        m += (1.0 - smoothstep(0.0, r, length((f - o) * vec2(1.0, 0.6 + hash(c + 4.4))))) * (i == 0 ? 0.8 : 0.45);
    }
    return m;
}

// Depth of field for aiming: the world beyond the focus blurs (12-tap disk, each tap weighted by
// its own blur so sharp near things don't smear into the background); the weapon stays sharp.
vec3 depthOfField(vec2 uv, vec3 c) {
    float depth = texture(uSceneDepth, uv).r;
    float dist = uDof.w / ((depth * 2.0 - 1.0) + uDof.z);
    float coc = clamp((dist - uDof.x) / (uDof.x * 1.5 + 2.0), 0.0, 1.0);
    if (depth >= 1.0)
        coc = 1.0; // the sky
    if (coc <= 0.02)
        return c;
    vec2 texel = 1.0 / vec2(textureSize(uHdr, 0));
    const vec2 disk[12] = vec2[](vec2(0.9, 0.1), vec2(0.45, 0.78), vec2(-0.4, 0.82), vec2(-0.88, 0.2), vec2(-0.6, -0.65),
                                 vec2(0.1, -0.95), vec2(0.7, -0.6), vec2(0.42, 0.05), vec2(-0.05, 0.45), vec2(-0.42, -0.02),
                                 vec2(0.0, -0.45), vec2(0.25, -0.3));
    vec3 sum = c;
    float wsum = 1.0;
    float radius = coc * uDof.y;
    for (int i = 0; i < 12; ++i) {
        vec2 s = uv + disk[i] * radius * texel;
        float sd = texture(uSceneDepth, s).r;
        float sdist = uDof.w / ((sd * 2.0 - 1.0) + uDof.z);
        float w = sd >= 1.0 ? 1.0 : clamp((sdist - uDof.x) / (uDof.x * 1.5 + 2.0), 0.0, 1.0);
        if (uDistortMask && texture(uDepth, s).r < 1.0)
            w = 0.0; // the weapon doesn't smear into the world
        sum += texture(uHdr, s).rgb * w;
        wsum += w;
    }
    return sum / wsum;
}

// Scene drawn below the window size: Catmull-Rom (bicubic) instead of bilinear stretching, so
// edges stay crisp instead of soft blocks (5 bilinear taps, Jimenez's form without the corners).
vec3 upscaled(vec2 uv) {
    vec2 size = vec2(textureSize(uHdr, 0));
    vec2 pos = uv * size;
    vec2 center = floor(pos - 0.5) + 0.5;
    vec2 f = pos - center;
    vec2 f2 = f * f, f3 = f2 * f;
    vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    vec2 w3 = 0.5 * f3 - 0.5 * f2;
    vec2 w12 = w1 + w2;
    vec2 tc12 = (center + w2 / w12) / size;
    vec2 tc0 = (center - 1.0) / size, tc3 = (center + 2.0) / size;
    vec3 r = texture(uHdr, vec2(tc12.x, tc0.y)).rgb * (w12.x * w0.y)
           + texture(uHdr, vec2(tc0.x, tc12.y)).rgb * (w0.x * w12.y)
           + texture(uHdr, tc12).rgb * (w12.x * w12.y)
           + texture(uHdr, vec2(tc3.x, tc12.y)).rgb * (w3.x * w12.y)
           + texture(uHdr, vec2(tc12.x, tc3.y)).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / wsum, vec3(0.0)); // the negative lobes can undershoot
}

vec3 sceneColor(vec2 uv) {
    vec3 c = uSharpen > 0.0 ? sharpened(uv, upscaled(uv)) : texture(uHdr, uv).rgb;
    bool weapon = uDistortMask && texture(uDepth, uv).r < 1.0;
    if (uDof.x > 0.0 && uDof.y > 0.5 && !weapon)
        c = depthOfField(uv, c);
    // Lens colour fringes, stronger toward the edges (a hit kicks them up for a moment).
    float ca = uChromatic + uScreenFx.x * 0.5;
    if (ca > 0.0) {
        vec2 d = uv - 0.5;
        vec2 off = d * dot(d, d) * ca * 0.03;
        c.r = texture(uHdr, uv + off).r;
        c.b = texture(uHdr, uv - off).b;
    }
    if (uMotionBlur <= 0.0 || (uDistortMask && texture(uDepth, vUV).r < 1.0))
        return c; // the weapon moves with the camera: sharp
    float depth = texture(uSceneDepth, vUV).r;
    vec4 prev = uMotionReproject * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    if (prev.w <= 0.0)
        return c;
    vec2 velocity = (vUV - (prev.xy / prev.w * 0.5 + 0.5)) * uMotionBlur;
    vec2 size = vec2(textureSize(uHdr, 0));
    float pixels = length(velocity * size);
    if (pixels < 0.75)
        return c;
    if (pixels > 48.0)
        velocity *= 48.0 / pixels;
    float jitter = hash(gl_FragCoord.xy + fract(uTime) * 517.0) - 0.5;
    vec3 sum = c;
    float count = 1.0;
    for (int i = 0; i < 8; ++i) {
        vec2 s = clamp(uv + velocity * ((float(i) + 0.5 + jitter) / 8.0 - 0.5), vec2(0.001), vec2(0.999));
        if (uDistortMask && texture(uDepth, s).r < 1.0)
            continue; // don't smear the weapon into the world
        sum += texture(uHdr, s).rgb;
        count += 1.0;
    }
    return sum / count;
}

void main() {
    float wet;
    vec2 rain = lensRainOffset(vUV, wet);
    vec2 uv = clamp(distortedUV(vUV) + rain, vec2(0.001), vec2(0.999));
    vec3 color = sceneColor(uv);
    vec3 bloom = texture(uBloom, uv).rgb;
    if (any(isnan(color)) || any(isinf(color))) color = vec3(0.0);
    if (any(isnan(bloom)) || any(isinf(bloom))) bloom = vec3(0.0);
    float dirt = uLensDirt > 0.0 ? lensDirtMask(vUV) * uLensDirt * 8.0 : 0.0;
    color += bloom * uBloomIntensity * (1.0 + dirt);
    color *= 1.0 - wet * 0.12; // a drop's rim darkens a little
    float autoExposure = uAutoExposure > 0.0 ? texelFetch(uAdapted, ivec2(0), 0).r : 1.0;
    color *= uExposure * mix(1.0, autoExposure, uAutoExposure);
    color = tonemap(color);
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
    // Gameplay screen effects (display space).
    vec2 e = (vUV - 0.5) * vec2(aspect() / 1.78, 1.0);
    float edge = smoothstep(0.3, 1.0, dot(e, e) * 2.0);
    if (uScreenFx.y > 0.0) { // low health: greyer, a dark-red edge beating like a heart
        float low = uScreenFx.y;
        float beat = pow(abs(sin(uTime * 3.6)), 12.0) + 0.6 * pow(abs(sin(uTime * 3.6 + 0.6)), 12.0);
        color = mix(color, vec3(dot(color, vec3(0.299, 0.587, 0.114))), low * 0.55);
        color = mix(color, color * vec3(0.55, 0.12, 0.1), edge * low * (0.55 + 0.35 * beat));
    }
    if (uScreenFx.x > 0.0) // a hit: red edges
        color = mix(color, vec3(0.42, 0.02, 0.01), edge * uScreenFx.x * 0.65);
    float grain = uGrain;
    if (uScreenFx.z > 0.0) { // radiation: yellow-green cast, speckles on the "film", flicker
        float rad = uScreenFx.z;
        color = mix(color, color * vec3(0.94, 1.04, 0.78), rad * 0.4);
        float speck = hash(floor(gl_FragCoord.xy * 0.5) + floor(uTime * 24.0) * 17.3);
        color += step(1.0 - 0.003 * rad, speck) * vec3(0.85, 1.0, 0.65) * rad;
        color *= 1.0 - rad * 0.12 * hash(vec2(floor(uTime * 14.0), 3.7));
        grain += rad * 0.07;
    }
    color += (hash(gl_FragCoord.xy + fract(uTime) * 1000.0) - 0.5) * grain;
    // Luma in alpha for FXAA. The window must get alpha 1: Qt composites the editor view with it.
    fragColor = vec4(color, uLumaAlpha ? dot(color, vec3(0.299, 0.587, 0.114)) : 1.0);
}
