#version 450 core
#include "common.glsl"
// Volumetric clouds (after Schneider, "The real-time volumetric cloudscapes of Horizon Zero Dawn",
// and Hillaire, "Physically based sky, atmosphere and cloud rendering in Frostbite"): a layer
// between uClouds.y and uClouds.y + uClouds.z metres over a round Earth, raymarched for each
// texel of a sky map (azimuth by sqrt elevation, common.glsl cloudMapUv). The sky pass reads the
// map: sky * a + rgb. A quarter of the texels (one of each 2x2) is traced per frame, the rest keep
// the previous result: clouds drift slowly and the map does not turn with the camera.
in vec2 vUV;
layout(binding = 0) uniform sampler3D uCloudNoise; // R Perlin-Worley, GBA Worley fBm (Atmosphere.h buildCloudNoise)
uniform int uPhase;     // texel of each 2x2 block traced this frame, -1 = all
uniform int uSteps;     // view ray samples (horizon; fewer overhead)
out vec4 fragColor;

const float kEarth = 6360000.0;
const float kPi = 3.14159265;

// Distance along a ray from inside a sphere (origin at the Earth's centre) to its surface.
float sphereExit(vec3 o, vec3 d, float r) {
    float b = dot(o, d), c = dot(o, o) - r * r;
    return -b + sqrt(max(b * b - c, 0.0));
}

float remap(float v, float lo, float hi, float a, float b) {
    return a + (v - lo) / max(hi - lo, 1e-5) * (b - a);
}

float henyeyGreenstein(float cosT, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-4), 1.5));
}

// Density at a world position (m, y above the ground) at height fraction h (0 base .. 1 top).
float cloudDensity(vec3 p, float h, bool detail) {
    float coverage = cloudCoverageAt(p.xz);
    if (coverage <= 0.01)
        return 0.0;
    // Cumulus profile: a flat-ish base and a rounded top; dense cover reaches higher.
    float profile = smoothstep(0.0, 0.07, h) * (1.0 - smoothstep(mix(0.35, 0.75, coverage), 1.0, h));
    vec3 wind = vec3(cloudWind().x, 0.0, cloudWind().y);
    vec3 q = (p - wind) / 4200.0 + vec3(0.0, h * 0.06, 0.0); // tops lean downwind a little
    vec4 n = textureLod(uCloudNoise, q, 0.0);
    float low = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = remap(n.r, (low - 1.0) * 0.6, 1.0, 0.0, 1.0) * profile;
    float shape = clamp(remap(base, 1.0 - coverage, 1.0, 0.0, 1.0), 0.0, 1.0) * coverage;
    if (!detail || shape <= 0.0)
        return shape;
    // Finer billows eat the edge: wispy at the base, puffy at the top.
    vec4 d = textureLod(uCloudNoise, (p - wind * 1.4) / 900.0, 0.0);
    float erode = d.g * 0.625 + d.b * 0.25 + d.a * 0.125;
    erode = mix(erode, 1.0 - erode, clamp(h * 4.0, 0.0, 1.0));
    return clamp(remap(shape, erode * 0.45, 1.0, 0.0, 1.0), 0.0, 1.0);
}

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    if (uPhase >= 0 && ((texel.x & 1) + (texel.y & 1) * 2) != uPhase)
        discard;
    vec3 dir = cloudMapDir(vUV);
    float base = uClouds.y, top = uClouds.y + max(uClouds.z, 10.0);
    vec3 eye = vec3(uCameraPos.x, 0.0, uCameraPos.z);
    float eyeH = clamp(uCameraPos.y, 0.0, base - 1.0);
    vec3 o = vec3(0.0, kEarth + eyeH, 0.0);
    float t0 = sphereExit(o, dir, kEarth + base), t1 = sphereExit(o, dir, kEarth + top);
    const float kFar = 60000.0;
    if (t0 > kFar) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    t1 = min(t1, t0 + 25000.0);
    int steps = int(mix(float(uSteps), float(uSteps) * 0.6, dir.y));
    float dt = (t1 - t0) / float(steps);
    float t = t0 + dt * hash12(gl_FragCoord.xy + fract(uSkyHorizon.w * 0.37) * 97.0);

    // Light. The sun (or the moon at night) and the sky's light from above and from the horizon.
    vec3 toSun = -normalize(uSunDir.xyz);
    vec3 sun = uSunColor.rgb * uSunDir.w;
    bool cycle = uSkyParams.w > 0.5;
    vec3 sunPos = cycle ? normalize(uSunPos.xyz) : toSun;
    vec3 skyTop = uSkyZenith.rgb, skyLow = uSkyHorizon.rgb;
    if (uAtmosphere.x > 0.0) {
        // Half way to the model: the authored colours carry the mood (a dark stormy zenith).
        skyTop = mix(skyTop, texture(uSkyLut, skyLutUv(vec3(0.0, 1.0, 0.0), sunPos)).rgb * uAtmosphere.y, uAtmosphere.x * 0.5);
        skyLow = mix(skyLow, texture(uSkyLut, skyLutUv(normalize(vec3(-toSun.x, 0.1, -toSun.z) + vec3(1e-4)), sunPos)).rgb * uAtmosphere.y, uAtmosphere.x * 0.5);
    }
    float cosT = dot(dir, toSun);
    // Strong forward lobe (silver lining) with some back scattering.
    float phase = mix(henyeyGreenstein(cosT, 0.75), henyeyGreenstein(cosT, -0.25), 0.4);
    float sigma = 0.02 * max(uClouds.w, 0.0); // extinction per metre at density 1
    float lightStep = uClouds.z * 0.08;

    vec3 light = vec3(0.0);
    float trans = 1.0, depthSum = 0.0, depthW = 0.0;
    for (int i = 0; i < steps && trans > 0.02; ++i, t += dt) {
        vec3 wp = eye + dir * t;
        float height = length(o + dir * t) - kEarth;
        float h = (height - base) / (top - base);
        wp.y = height;
        float dens = cloudDensity(wp, h, true);
        if (dens <= 0.002)
            continue;
        // Toward the light: optical depth through the cloud above this point (5 growing steps).
        float od = 0.0, s = lightStep * 0.5;
        for (int j = 0; j < 5; ++j, s *= 1.8) {
            vec3 lp = wp + toSun * s;
            float lh = (lp.y - base) / (top - base);
            if (lh > 1.0)
                break;
            od += cloudDensity(lp, lh, j < 2) * s * 0.55;
        }
        od *= sigma;
        // Multiple scattering approximated by octaves of weaker extinction and a flatter phase
        // (Wrenninge / Hillaire): thick clouds stay bright inside instead of turning black.
        vec3 sunLit = vec3(0.0);
        float a = 1.0, b = 1.0, c = 1.0;
        for (int k = 0; k < 3; ++k) {
            float ph = mix(1.0 / (4.0 * kPi), phase, c);
            sunLit += sun * a * exp(-od * b) * ph;
            a *= 0.5; b *= 0.4; c *= 0.5;
        }
        // Sky light: from above near the top, darker in the cloud's lower belly.
        // Thick cloud above a point also hides most of the sky: dark storm bellies under the towers.
        vec3 ambient = mix(skyLow * 0.45, skyTop, h) * mix(0.3, 1.0, h) * (0.15 + 0.85 * exp(-od * 0.2));
        vec3 lightning = vec3(0.75, 0.8, 1.0) * uWeather.y * 3.0;
        vec3 scatter = (sunLit + ambient + lightning) * sigma * dens;
        float ext = max(sigma * dens, 1e-7);
        float stepTrans = exp(-ext * dt);
        light += trans * scatter * (1.0 - stepTrans) / ext; // integrated over the step
        depthSum += t * trans * (1.0 - stepTrans);
        depthW += trans * (1.0 - stepTrans);
        trans *= stepTrans;
    }
    // Aerial perspective: far clouds fade into the sky behind them.
    float dist = depthW > 0.0 ? depthSum / depthW : t0;
    float fade = exp(-dist / 30000.0) * smoothstep(kFar, kFar * 0.6, t0);
    light *= fade;
    trans = mix(1.0, trans, fade);
    fragColor = vec4(light, trans);
}
