#version 450 core
#include "common.glsl"
// Screen-space ambient occlusion from the depth buffer (normals reconstructed
// from depth), 16 samples in a hemisphere, rotated per pixel.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uDepth;
uniform float uRadius;
uniform mat4 uInvProj;
out float fragColor;

vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0) {
        fragColor = 1.0;
        return;
    }
    vec3 p = viewPos(vUV);
    vec2 texel = 1.0 / vec2(textureSize(uDepth, 0));
    vec3 px = viewPos(vUV + vec2(texel.x, 0.0)) - p, nx = p - viewPos(vUV - vec2(texel.x, 0.0));
    vec3 py = viewPos(vUV + vec2(0.0, texel.y)) - p, ny = p - viewPos(vUV - vec2(0.0, texel.y));
    vec3 dx = abs(px.z) < abs(nx.z) ? px : nx;   // pick the smoother side at edges
    vec3 dy = abs(py.z) < abs(ny.z) ? py : ny;
    vec3 n = normalize(cross(dx, dy));
    float angle = hash12(gl_FragCoord.xy) * 6.2831853;
    vec3 rnd = vec3(cos(angle), sin(angle), 0.0);
    vec3 t = normalize(rnd - n * dot(rnd, n));
    mat3 tbn = mat3(t, cross(n, t), n);
    float occlusion = 0.0;
    const int kSamples = 16;
    for (int i = 0; i < kSamples; ++i) {
        float fi = float(i);
        float r = sqrt(fract(fi * 0.618034 + 0.31));
        float phi = fi * 2.399963;
        float z = sqrt(max(0.0, 1.0 - r * r));
        vec3 s = tbn * vec3(cos(phi) * r, sin(phi) * r, z);
        float scale = mix(0.15, 1.0, (fi / float(kSamples)) * (fi / float(kSamples)));
        vec3 samplePos = p + s * uRadius * scale;
        vec4 clip = uProj * vec4(samplePos, 1.0);
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0))))
            continue;
        float sceneZ = viewPos(suv).z;
        float range = smoothstep(0.0, 1.0, uRadius / abs(p.z - sceneZ));
        occlusion += (sceneZ >= samplePos.z + 0.02 ? 1.0 : 0.0) * range;
    }
    fragColor = 1.0 - occlusion / float(kSamples);
}
