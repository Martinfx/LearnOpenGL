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
    vec4 uMoonDir;         // xyz toward the moon, w: moon brightness 0..1
    vec4 uSkyParams;       // x: stars 0..1, y: twilight glow, z: cloud shadows, w: 1 = time of day on
    vec4 uSunPos;          // xyz toward the sun (the main light is the moon at night)
    mat4 uRainMatrix;      // world -> rain occlusion map (depth seen from above)
    vec4 uWeather;         // x: wetness, y: lightning flash, z: 1 = rain map valid, w: puddles
    vec4 uWind;            // xz: direction the wind blows, y: strength 0..1, w: gusts 0..1
    vec4 uVolumetric;      // volumetric fog: x 1 = on, y: near (m), z: 1 / log(far / near), w: far (m)
    vec4 uProbeParams;     // x: 1 = baked probes on (0 while baking them), y: reflection cube max mip
    mat4 uPrevViewProj;    // previous frame (no TAA jitter): screen-space reflections reproject into it
    vec4 uSsr;             // x: 1 = SSR on, y: max distance (m), z: intensity, w: max roughness
    vec4 uAtmosphere;      // x: physical sky 0..1, y: radiance scale (sun illuminance)
};
// Physical sky table (engine/Atmosphere.h): azimuth from the sun by elevation, linear radiance.
layout(binding = 31) uniform sampler2D uSkyLut;
vec2 skyLutUv(vec3 view, vec3 toSun) {
    float elevation = asin(clamp(view.y, -1.0, 1.0));
    float v = 0.5 + 0.5 * sign(elevation) * sqrt(abs(elevation) / 1.5707963);
    vec2 a = view.xz, s = toSun.xz;
    float u = 0.0;
    if (length(a) > 1e-5 && length(s) > 1e-5)
        u = acos(clamp(dot(normalize(a), normalize(s)), -1.0, 1.0)) / 3.14159265;
    return vec2(u, v);
}
// Volumetric fog up to uVolumetric.w (view depth): rgb light scattered toward the eye,
// a: transmittance, both from the eye to the froxel (volumetric_*.comp).
layout(binding = 15) uniform sampler3D uVolumeFog;

// Interior volumes (entity type "interior"): turned boxes where the sky light and the
// outdoor fog are dimmed. centre + fade, half size + sky light left, cos/sin yaw + fog left.
struct Interior {
    vec4 centerFade;
    vec4 halfSky;
    vec4 rotationFog;
};
layout(std430, binding = 9) readonly buffer Interiors {
    ivec4 uInteriorCount;
    Interior uInteriors[];
};

// x: sky light multiplier, y: fog density multiplier at a world position (1 outdoors).
vec2 interiorFactors(vec3 p) {
    vec2 f = vec2(1.0);
    for (int i = 0; i < uInteriorCount.x; ++i) {
        Interior v = uInteriors[i];
        vec3 d = p - v.centerFade.xyz;
        vec2 cs = v.rotationFog.xy;
        vec3 local = vec3(cs.x * d.x + cs.y * d.z, d.y, -cs.y * d.x + cs.x * d.z);
        float outside = length(max(abs(local) - v.halfSky.xyz, vec3(0.0)));
        float w = 1.0 - clamp(outside / v.centerFade.w, 0.0, 1.0);
        if (w > 0.0)
            f = min(f, mix(vec2(1.0), vec2(v.halfSky.w, v.rotationFog.z), w));
    }
    return f;
}

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

// Stars: one random point per cell of a grid around the camera, twinkling a little.
vec3 starField(vec3 dir) {
    vec3 p = dir * 260.0;
    vec3 cell = floor(p);
    float h = hash12(cell.xy + cell.z * 17.31);
    if (h < 0.985)
        return vec3(0.0);
    vec3 center = cell + vec3(hash12(cell.yz + 3.1), hash12(cell.zx + 7.7), hash12(cell.xy + 1.3));
    float d = length(p - center);
    float size = 0.10 + 0.25 * (h - 0.985) / 0.015;
    float twinkle = 0.75 + 0.25 * sin(uSkyHorizon.w * 3.0 + h * 400.0);
    vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.88, 0.75), hash12(cell.xz + 5.5));
    return tint * smoothstep(size, 0.0, d) * twinkle * 0.6 * smoothstep(0.0, 0.15, dir.y);
}

// Moon disk lit from the sun's side (phases), with darker "seas".
vec3 moonDisk(vec3 dir) {
    vec3 m = normalize(uMoonDir.xyz);
    float c = dot(dir, m);
    const float radius = 0.018;
    if (c < cos(radius * 1.6) || m.y < -0.05)
        return vec3(0.0);
    vec3 offset = (dir - m * c) / radius;
    float r2 = dot(offset, offset);
    vec3 glow = vec3(0.6, 0.7, 0.9) * 0.02 * uMoonDir.w * smoothstep(2.56, 0.0, r2);
    if (r2 > 1.0)
        return glow;
    vec3 n = offset - m * sqrt(1.0 - r2);
    float lit = smoothstep(-0.05, 0.1, dot(n, normalize(uSunPos.xyz)));
    vec3 a = normalize(cross(m, vec3(0.0, 1.0, 0.0)) + 1e-4);
    vec3 b = cross(a, m);
    float seas = valueNoise(vec2(dot(offset, a), dot(offset, b)) * 2.5 + 3.0);
    vec3 surface = vec3(0.95, 0.93, 0.88) * mix(0.55, 1.0, smoothstep(0.35, 0.6, seas));
    return surface * (0.03 + 0.9 * lit) * smoothstep(1.0, 0.92, r2) + glow;
}

// Procedural sky: zenith/horizon gradient, ground below the horizon, sun disk
// and glow, sunrise/sunset glow, stars and moon at night, and a layer of clouds
// whose cover darkens the whole sky (overcast).
vec3 skyColor(vec3 dir, bool withSun) {
    dir = normalize(dir);
    bool cycle = uSkyParams.w > 0.5;
    vec3 sunDir = cycle ? normalize(uSunPos.xyz) : -normalize(uSunDir.xyz);
    bool sunIsLight = !cycle || dot(sunDir, -normalize(uSunDir.xyz)) > 0.999;
    float cover = uSkyZenith.w;
    float up = dir.y;
    vec3 sky = mix(uSkyHorizon.rgb, uSkyZenith.rgb, pow(clamp(up, 0.0, 1.0), 0.45));
    if (uAtmosphere.x > 0.0) // the physical model: blue overhead, pale and hazy at the horizon, warm toward a low sun
        sky = mix(sky, texture(uSkyLut, skyLutUv(vec3(dir.x, max(up, 0.0), dir.z), sunDir)).rgb * uAtmosphere.y, uAtmosphere.x);
    vec3 ground = mix(uSkyHorizon.rgb, uGroundColor.rgb, clamp(-up * 4.0, 0.0, 1.0));
    vec3 color = up >= 0.0 ? sky : ground;
    float sunAmount = max(dot(dir, sunDir), 0.0);
    vec3 sunLight = sunIsLight ? uSunColor.rgb * uSunDir.w : vec3(0.0);
    color += sunLight * 0.06 * pow(sunAmount, 8.0) * (1.0 - cover * 0.7) * (1.0 - 0.7 * uAtmosphere.x); // the model has its own glow
    // Sunrise / sunset: warm band along the horizon, strongest toward the sun.
    float twilight = uSkyParams.y;
    if (twilight > 0.0) {
        vec2 flatDir = normalize(dir.xz + 1e-5), flatSun = normalize(sunDir.xz + 1e-5);
        float toward = 0.5 + 0.5 * dot(flatDir, flatSun);
        float band = exp(-abs(up - max(sunDir.y, -0.05) * 0.3) * 7.0);
        vec3 glow = mix(vec3(0.55, 0.22, 0.30), vec3(1.0, 0.42, 0.12), toward) * band * (0.15 + 0.85 * toward * toward);
        color += glow * twilight * 0.55 * (1.0 - cover * 0.6);
    }
    if (withSun && up > 0.0 && sunIsLight)
        color += sunLight * 8.0 * smoothstep(0.9993, 0.9997, sunAmount) * (1.0 - cover * 0.9);
    if (up > 0.0 && cycle) {
        float clear = 1.0 - cover * 0.95;
        color += starField(dir) * uSkyParams.x * clear;
        color += moonDisk(dir) * clamp(uMoonDir.w + 0.15, 0.0, 1.0) * mix(0.25, 1.0, uSkyParams.x) * clear;
    }
    if (up > 0.0 && cover > 0.0) {
        vec2 uv = dir.xz / (up + 0.12) * 0.6 + vec2(uSkyHorizon.w * 0.004, uSkyHorizon.w * 0.002);
        // Cumulus: a broad shape with finer billows eroding its edge, so clouds have crisp bright rims.
        float n = fbm(uv * 2.0);
        float d = n + 0.16 * (fbm(uv * 7.3 + vec2(3.1, 7.7)) - 0.5);
        float body = smoothstep(1.0 - cover, 1.0 - cover + 0.16, d);
        float clouds = body * smoothstep(0.0, 0.12, up);
        // Self-shadowing: where the cloud thins toward the sun it is lit, the far side and the thick
        // core are grey-blue (the sky seen through and under it).
        vec3 toSun = -normalize(uSunDir.xyz);
        float ns = fbm((uv + normalize(toSun.xz + 1e-5) * 0.07) * 2.0);
        float sunSide = clamp(0.55 + (n - ns) * 5.0, 0.0, 1.0);
        float core = smoothstep(1.0 - cover + 0.1, 1.0 - cover + 0.4, d);
        vec3 white = uSkyHorizon.rgb * vec3(1.32, 1.3, 1.26);
        vec3 shade = mix(uSkyZenith.rgb, uSkyHorizon.rgb, 0.55) * 0.92;
        vec3 cloudColor = mix(shade, white, clamp(sunSide * (1.0 - 0.45 * core) + 0.15, 0.0, 1.0));
        // Silver lining toward the sun.
        cloudColor += uSunColor.rgb * uSunDir.w * 0.05 * pow(max(dot(dir, toSun), 0.0), 6.0) * (1.0 - core);
        // Overcast: a thick cover turns the whole layer into a flat grey ceiling.
        cloudColor = mix(cloudColor, uSkyHorizon.rgb * 0.85, smoothstep(0.6, 0.95, cover));
        // Lightning lights the clouds from inside.
        color += vec3(0.75, 0.8, 1.0) * uWeather.y * 0.8 * clouds;
        color = mix(color, cloudColor, clouds * 0.95);
    }
    color += vec3(0.55, 0.6, 0.75) * uWeather.y * 0.25 * smoothstep(-0.05, 0.3, up); // the whole sky flashes
    return color;
}

// Shadow of the cloud layer on the ground (1 = no cloud), matching the sky's clouds
// drifting with the wind. Three noise octaves are enough at ground scale.
float cloudShadow(vec3 worldPos) {
    float strength = uSkyParams.z;
    float cover = uSkyZenith.w;
    vec3 l = -normalize(uSunDir.xyz);
    if (strength <= 0.0 || cover <= 0.0 || l.y < 0.02)
        return 1.0;
    vec3 q = worldPos + l * ((800.0 - worldPos.y) / l.y);
    vec2 uv = q.xz / 800.0 * 0.6 + vec2(uSkyHorizon.w * 0.004, uSkyHorizon.w * 0.002);
    vec2 p = uv * 2.0;
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * valueNoise(p);
        p = p * 2.03 + vec2(17.1, 9.2);
        a *= 0.5;
    }
    v += 0.06; // the missing octaves' average
    float clouds = smoothstep(1.0 - cover, 1.0 - cover + 0.35, v);
    return 1.0 - strength * clouds * 0.85;
}

// 1 where rain reaches `worldPos` (open sky), 0 under a roof. Needs the rain map
// (texture unit 14) declared by the shader as uRainMap.
#ifdef RAIN_MAP
float rainExposure(vec3 worldPos) {
    if (uWeather.z < 0.5)
        return 1.0;
    vec4 p = uRainMatrix * vec4(worldPos, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (any(lessThan(c.xy, vec2(0.0))) || any(greaterThan(c.xy, vec2(1.0))))
        return 1.0;
    // Something more than ~0.4 m above the point (300 m depth range) keeps it dry; 4 taps soften the edge.
    vec2 texel = 1.0 / vec2(textureSize(uRainMap, 0));
    float open = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = (vec2(i & 1, i >> 1) - 0.5) * texel;
        open += c.z <= texture(uRainMap, c.xy + o).r + 0.4 / 300.0 ? 1.0 : 0.0;
    }
    return open * 0.25;
}
#endif

// Exponential height fog along the view ray, tinted toward the sun.
// Analytic height fog over [start, dist] of the ray from the camera: optical depth.
float heightFogDepth(vec3 dir, float start, float dist) {
    float density = uFogColor.w;
    float falloff = max(uFogParams.y, 1e-4);
    float startHeight = uCameraPos.y + dir.y * start - uFogParams.x;
    float len = max(dist - start, 0.0);
    float amount = abs(dir.y) < 1e-3 ? density * exp(-falloff * startHeight) * len
                                     : density * exp(-falloff * startHeight) * (1.0 - exp(-falloff * dir.y * len)) / (falloff * dir.y);
    return max(amount, 0.0);
}

vec3 analyticFogColor(vec3 dir) {
    float sun = pow(max(dot(dir, -normalize(uSunDir.xyz)), 0.0), 8.0) * uFogParams.z;
    return uFogColor.rgb + uSunColor.rgb * uSunDir.w * 0.08 * sun + vec3(0.5, 0.55, 0.7) * uWeather.y * 0.12;
}

// Fog between the camera and a surface: the volumetric fog (light shafts, lamps and the
// flashlight in the fog) near the camera, the analytic height fog beyond its range.
vec3 applyFog(vec3 color, vec3 worldPos) {
    if (uFogColor.w <= 0.0)
        return color;
    vec3 ray = worldPos - uCameraPos.xyz;
    float dist = length(ray);
    vec3 dir = ray / max(dist, 1e-4);
    if (uVolumetric.x < 0.5)
        return mix(color, analyticFogColor(dir), 1.0 - exp(-heightFogDepth(dir, 0.0, dist)));
    vec4 clip = uProj * (uView * vec4(worldPos, 1.0));
    float depth = max(clip.w, 1e-4); // view depth (perspective)
    vec2 uv = clip.xy / depth * 0.5 + 0.5;
    float z = log(max(depth, uVolumetric.y) / uVolumetric.y) * uVolumetric.z;
    vec4 volume = texture(uVolumeFog, vec3(uv, min(z, 1.0)));
    if (depth > uVolumetric.w) {
        float start = dist * uVolumetric.w / depth; // where the ray leaves the volume
        color = mix(color, analyticFogColor(dir), 1.0 - exp(-heightFogDepth(dir, start, dist)));
    }
    return color * volume.a + volume.rgb;
}
