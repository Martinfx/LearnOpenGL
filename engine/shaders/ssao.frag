#version 450 core
#include "common.glsl"
// Ground-truth ambient occlusion (GTAO, Jimenez et al. 2016) from the depth buffer: in a few
// screen-space slices around the pixel, march both ways for the highest horizon, then
// integrate the visible arc cosine-weighted against the normal reconstructed from depth.
// Slice directions turn per pixel (4x4 pattern, blurred away by blur.frag) and per frame
// (averaged by TAA when it is on). Output: visibility 0..1 and linear depth for the blur.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uDepth;
uniform float uRadius;   // metres
uniform mat4 uInvProj;
uniform float uFrame;    // frame index for the temporal rotation (0 = still)
out vec2 fragColor;

vec3 viewPos(vec2 uv) {
    float d = textureLod(uDepth, uv, 0.0).r;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

const float kPi = 3.14159265;

void main() {
    float depth = textureLod(uDepth, vUV, 0.0).r;
    if (depth >= 1.0) {
        fragColor = vec2(1.0, 1e6);
        return;
    }
    vec3 p = viewPos(vUV);
    vec2 texel = 1.0 / vec2(textureSize(uDepth, 0));
    vec3 px = viewPos(vUV + vec2(texel.x, 0.0)) - p, nx = p - viewPos(vUV - vec2(texel.x, 0.0));
    vec3 py = viewPos(vUV + vec2(0.0, texel.y)) - p, ny = p - viewPos(vUV - vec2(0.0, texel.y));
    vec3 dx = abs(px.z) < abs(nx.z) ? px : nx;   // the smoother side at edges
    vec3 dy = abs(py.z) < abs(ny.z) ? py : ny;
    vec3 n = normalize(cross(dx, dy));
    vec3 v = normalize(-p);

    // Screen radius of uRadius metres at this depth, kept between a few pixels and 1/4 screen.
    float pixelsPerMetre = uProj[1][1] * 0.5 * float(textureSize(uDepth, 0).y) / max(-p.z, 0.01);
    float radiusPx = clamp(uRadius * pixelsPerMetre, 4.0, 0.25 * float(textureSize(uDepth, 0).y));
    const int kSlices = 3, kSteps = 6;
    // 4x4 interleaved pattern: 16 slice angles and step offsets, blurred together.
    ivec2 q = ivec2(gl_FragCoord.xy) & 3;
    float pattern = float((q.x * 4 + q.y * 9) & 15) / 16.0;   // spreads neighbours apart
    float jitter = fract(float((q.x + q.y * 4) * 7 & 15) / 16.0 + uFrame * 0.618034);
    float rotation = (pattern + uFrame * 0.2451) * kPi / float(kSlices);

    float visibility = 0.0;
    for (int s = 0; s < kSlices; ++s) {
        float phi = rotation + float(s) * kPi / float(kSlices);
        vec2 dir = vec2(cos(phi), sin(phi));
        // Slice plane through the view vector; the normal projected into it.
        vec3 dir3 = vec3(dir, 0.0);
        vec3 axis = normalize(cross(dir3, v));
        vec3 ortho = dir3 - v * dot(dir3, v);
        vec3 nProj = n - axis * dot(n, axis);
        float nLen = length(nProj);
        if (nLen < 1e-4)
            continue;
        float cosN = clamp(dot(nProj, v) / nLen, -1.0, 1.0);
        float gamma = sign(dot(ortho, nProj)) * acos(cosN);
        // Highest horizon on both sides (cosine of the angle to the view vector).
        float h0 = -1.0, h1 = -1.0;
        for (int i = 0; i < kSteps; ++i) {
            float t = (float(i) + jitter) / float(kSteps);
            vec2 offset = dir * texel * max(1.0, t * t * radiusPx);
            for (int side = 0; side < 2; ++side) {
                vec2 suv = vUV + (side == 0 ? offset : -offset);
                if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0))))
                    continue;
                vec3 d = viewPos(suv) - p;
                float len = length(d);
                // Falloff: occluders beyond the radius count less, so far walls don't darken.
                float falloff = clamp(1.0 - len * len / (uRadius * uRadius), 0.0, 1.0);
                float c = mix(-1.0, dot(d, v) / max(len, 1e-4), falloff);
                if (side == 0)
                    h1 = max(h1, c); // along +dir: the positive side of the slice
                else
                    h0 = max(h0, c);
            }
        }
        // Horizon angles relative to the view vector, limited by the normal's hemisphere.
        float a0 = gamma + max(-acos(h0) - gamma, -kPi * 0.5);
        float a1 = gamma + min(acos(h1) - gamma, kPi * 0.5);
        // Cosine-weighted integral of the visible arc (Jimenez eq. 10).
        float sinG = sin(gamma), cosG = cos(gamma);
        float arc0 = -cos(2.0 * a0 - gamma) + cosG + 2.0 * a0 * sinG;
        float arc1 = -cos(2.0 * a1 - gamma) + cosG + 2.0 * a1 * sinG;
        visibility += nLen * 0.25 * (arc0 + arc1);
    }
    visibility = clamp(visibility / float(kSlices), 0.0, 1.0);
    fragColor = vec2(visibility, -p.z);
}
