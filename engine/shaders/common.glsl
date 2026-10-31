// Shared by all engine shaders: environment uniforms, BRDF terms and the sky.
layout(std140, binding = 1) uniform Environment {
    vec4 uSunDir;          // xyz: direction the light travels, w: intensity
    vec4 uSunColor;        // rgb linear, w: 1 = shadows on
    vec4 uSkyZenith;       // rgb linear, w: cloud cover 0..1
    vec4 uSkyHorizon;      // rgb linear, w: time in seconds (cloud drift)
    vec4 uGroundColor;     // rgb linear, w: image based lighting intensity
    vec4 uFogColor;        // rgb linear, w: density
    vec4 uFogParams;       // x: height, y: height falloff, z: sun scattering, w: SSAO strength
    vec4 uCameraPos;       // xyz, w: 1 = SSAO on
    mat4 uView;
    mat4 uProj;
    mat4 uInvViewProj;
    mat4 uShadowMatrices[3];
    vec4 uCascadeSplits;   // view distances of the cascade ends, w: cascade count
    vec4 uViewport;        // width, height, 1/width, 1/height
    vec4 uShadowParams;    // x: depth bias, y: normal bias, z: texel size, w: env max mip
};

const float PI = 3.14159265;

float distributionGGX(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySmith(float nv, float nl, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    return nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelRoughness(float cosTheta, vec3 f0, float rough) {
    return f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * valueNoise(p);
        p = p * 2.03 + vec2(17.1, 9.2);
        a *= 0.5;
    }
    return v;
}

// Procedural sky: zenith/horizon gradient, ground below the horizon, sun disk
// and glow, and a layer of clouds whose cover darkens the whole sky (overcast).
vec3 skyColor(vec3 dir, bool withSun) {
    dir = normalize(dir);
    vec3 sunDir = -normalize(uSunDir.xyz);
    float cover = uSkyZenith.w;
    float up = dir.y;
    vec3 sky = mix(uSkyHorizon.rgb, uSkyZenith.rgb, pow(clamp(up, 0.0, 1.0), 0.45));
    vec3 ground = mix(uSkyHorizon.rgb, uGroundColor.rgb, clamp(-up * 4.0, 0.0, 1.0));
    vec3 color = up >= 0.0 ? sky : ground;
    float sunAmount = max(dot(dir, sunDir), 0.0);
    vec3 sunLight = uSunColor.rgb * uSunDir.w;
    color += sunLight * 0.06 * pow(sunAmount, 8.0) * (1.0 - cover * 0.7);
    if (withSun && up > 0.0)
        color += sunLight * 8.0 * smoothstep(0.9993, 0.9997, sunAmount) * (1.0 - cover * 0.9);
    if (up > 0.0 && cover > 0.0) {
        vec2 uv = dir.xz / (up + 0.12) * 0.6 + vec2(uSkyHorizon.w * 0.004, uSkyHorizon.w * 0.002);
        float n = fbm(uv * 2.0);
        float clouds = smoothstep(1.0 - cover, 1.0 - cover + 0.35, n) * smoothstep(0.0, 0.12, up);
        vec3 cloudColor = mix(uSkyHorizon.rgb * 1.05, sunLight * 0.12 + uSkyHorizon.rgb * 0.6, pow(sunAmount, 4.0) * 0.5);
        cloudColor *= mix(1.0, 0.7, n);
        color = mix(color, cloudColor, clouds * 0.9);
    }
    return color;
}

// Exponential height fog along the view ray, tinted toward the sun.
vec3 applyFog(vec3 color, vec3 worldPos) {
    float density = uFogColor.w;
    if (density <= 0.0)
        return color;
    vec3 ray = worldPos - uCameraPos.xyz;
    float dist = length(ray);
    vec3 dir = ray / max(dist, 1e-4);
    float falloff = max(uFogParams.y, 1e-4);
    float startHeight = uCameraPos.y - uFogParams.x;
    float fogAmount = density * exp(-falloff * startHeight) * (1.0 - exp(-falloff * dir.y * dist)) / (falloff * dir.y);
    if (abs(dir.y) < 1e-3)
        fogAmount = density * exp(-falloff * startHeight) * dist;
    float f = 1.0 - exp(-max(fogAmount, 0.0));
    float sun = pow(max(dot(dir, -normalize(uSunDir.xyz)), 0.0), 8.0) * uFogParams.z;
    vec3 fogColor = uFogColor.rgb + uSunColor.rgb * uSunDir.w * 0.08 * sun;
    return mix(color, fogColor, f);
}
