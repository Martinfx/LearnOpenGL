// Baked lighting probes (engine/Probes.h): reflection cubemaps and irradiance volumes,
// turned boxes that blend in over `fade` metres inside their edges.
struct ReflectionProbe {
    vec4 centerFade;
    vec4 halfLayer;   // half size, w: layer of the cube map array
    vec4 rotation;    // cos, sin of the yaw, z: 1 = box projection, w: intensity
};
struct IrradianceVolume {
    vec4 centerFade;
    vec4 halfZ0;      // half size, w: first z slice in the atlas
    vec4 rotation;    // cos, sin of the yaw, w: intensity
    vec4 dims;        // grid points
};
layout(std430, binding = 10) readonly buffer ReflectionProbes {
    ivec4 uReflectionCount;
    ReflectionProbe uReflections[];
};
layout(std430, binding = 11) readonly buffer IrradianceVolumes {
    ivec4 uVolumeInfo; // x: count, yzw: atlas size
    IrradianceVolume uIrrVolumes[];
};
layout(binding = 16) uniform sampler3D uIrradianceR;
layout(binding = 17) uniform sampler3D uIrradianceG;
layout(binding = 18) uniform sampler3D uIrradianceB;
layout(binding = 19) uniform samplerCubeArray uProbeCubes;

vec3 probeLocal(vec3 p, vec3 center, vec2 cs) {
    vec3 d = p - center;
    return vec3(cs.x * d.x + cs.y * d.z, d.y, -cs.y * d.x + cs.x * d.z);
}
vec3 probeLocalDir(vec3 d, vec2 cs) { return vec3(cs.x * d.x + cs.y * d.z, d.y, -cs.y * d.x + cs.x * d.z); }
vec3 probeWorldDir(vec3 l, vec2 cs) { return vec3(cs.x * l.x - cs.y * l.z, l.y, cs.y * l.x + cs.x * l.z); }
float probeWeight(vec3 local, vec3 halfSize, float fade) {
    vec3 inside = halfSize - abs(local);
    return clamp(min(min(inside.x, inside.y), inside.z) / fade, 0.0, 1.0);
}

// Diffuse light (radiance a white Lambert surface reflects) from the irradiance volumes; w: weight.
vec3 probeIrradiance(vec3 p, vec3 n, out float w) {
    w = 0.0;
    vec3 result = vec3(0.0);
    if (uProbeParams.x < 0.5)
        return result;
    for (int i = 0; i < uVolumeInfo.x; ++i) {
        IrradianceVolume v = uIrrVolumes[i];
        vec3 local = probeLocal(p, v.centerFade.xyz, v.rotation.xy);
        float weight = probeWeight(local, v.halfZ0.xyz, v.centerFade.w);
        if (weight <= w)
            continue;
        // Grid points span the box inset by min(half / 2, 0.25 m) (engine::irradiancePoint).
        vec3 inset = min(v.halfZ0.xyz * 0.5, vec3(0.25));
        vec3 t = clamp((local + v.halfZ0.xyz - inset) / max((v.halfZ0.xyz - inset) * 2.0, vec3(1e-3)), 0.0, 1.0);
        vec3 cell = t * (v.dims.xyz - 1.0);
        vec3 uvw = (cell + 0.5 + vec3(0.0, 0.0, v.halfZ0.w)) / vec3(uVolumeInfo.yzw);
        vec4 r = texture(uIrradianceR, uvw), g = texture(uIrradianceG, uvw), b = texture(uIrradianceB, uvw);
        vec4 basis = vec4(1.0, n.y, n.z, n.x);
        result = max(vec3(dot(r, basis), dot(g, basis), dot(b, basis)), vec3(0.0)) * v.rotation.w;
        w = weight;
    }
    return result;
}

// Prefiltered reflection from the reflection probes (box projected); w: weight.
vec3 probeReflection(vec3 p, vec3 r, float rough, out float w) {
    w = 0.0;
    vec3 result = vec3(0.0);
    if (uProbeParams.x < 0.5)
        return result;
    for (int i = 0; i < uReflectionCount.x; ++i) {
        ReflectionProbe probe = uReflections[i];
        vec3 local = probeLocal(p, probe.centerFade.xyz, probe.rotation.xy);
        float weight = probeWeight(local, probe.halfLayer.xyz, probe.centerFade.w);
        if (weight <= w)
            continue;
        vec3 dir = r;
        if (probe.rotation.z > 0.5) {
            // Box projection: where the reflected ray leaves the box, seen from the capture point.
            vec3 rl = probeLocalDir(r, probe.rotation.xy);
            vec3 safe = mix(vec3(1e-4), rl, greaterThan(abs(rl), vec3(1e-4)));
            vec3 t1 = (probe.halfLayer.xyz - local) / safe, t2 = (-probe.halfLayer.xyz - local) / safe;
            vec3 tf = max(t1, t2);
            float t = min(min(tf.x, tf.y), tf.z);
            dir = probeWorldDir(local + rl * t, probe.rotation.xy);
        }
        result = textureLod(uProbeCubes, vec4(dir, probe.halfLayer.w), rough * uProbeParams.y).rgb * probe.rotation.w;
        w = weight;
    }
    return result;
}
