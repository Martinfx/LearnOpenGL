#version 450 core
// Temporal anti-aliasing: the scene is rendered with a sub-pixel jitter that changes every
// frame (Halton 2,3); this pass blends it into the history of earlier frames, reprojected
// by the camera motion through the depth buffer. Colours too far from the 3x3 neighbourhood
// of the new frame are pulled into it (variance clip in YCoCg), which removes ghosts of
// moving objects and disocclusions. Blending is done on tone-mapped weights so bright pixels
// don't flicker. The weapon layer has no world depth: it keeps its screen position.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uCurrent;      // HDR scene of this frame (jittered)
layout(binding = 1) uniform sampler2D uHistory;      // resolved result of the previous frame
layout(binding = 2) uniform sampler2D uDepth;        // scene depth (before the weapon layer)
layout(binding = 3) uniform sampler2D uOverlayDepth; // weapon layer depth (1 = no weapon)
uniform mat4 uReproject;  // this frame's jittered clip -> previous frame's clip (no jitter)
uniform vec2 uJitterUV;   // this frame's jitter in UV units
uniform int uHistoryValid;
uniform int uOverlay;
uniform float uFeedback;  // share of the history in the result (0.85..0.95)
uniform float uSharpen;   // 0..1: counters the softness of the bilinear history
out vec4 fragColor;

vec3 toYCoCg(vec3 c) {
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}
vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}
// Tone-mapped space (Karis): a single very bright sample can't dominate the average.
vec3 compress(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 expand(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }

// Catmull-Rom history fetch from 5 bilinear taps: stays sharp over many frames.
vec3 sampleHistory(vec2 uv) {
    vec2 size = vec2(textureSize(uHistory, 0));
    vec2 pos = uv * size;
    vec2 center = floor(pos - 0.5) + 0.5;
    vec2 f = pos - center;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (center - 1.0) / size, t3 = (center + 2.0) / size, t12 = (center + w2 / w12) / size;
    vec3 c = texture(uHistory, vec2(t12.x, t0.y)).rgb * (w12.x * w0.y) +
             texture(uHistory, vec2(t0.x, t12.y)).rgb * (w0.x * w12.y) +
             texture(uHistory, t12).rgb * (w12.x * w12.y) +
             texture(uHistory, vec2(t3.x, t12.y)).rgb * (w3.x * w12.y) +
             texture(uHistory, vec2(t12.x, t3.y)).rgb * (w12.x * w3.y);
    float w = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(c / w, vec3(0.0));
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(uCurrent, 0) - 1;
    vec3 center = compress(texelFetch(uCurrent, p, 0).rgb);
    // Neighbourhood statistics (and a light sharpen of the new sample).
    vec3 m1 = vec3(0.0), m2 = vec3(0.0), cross = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec3 c = compress(texelFetch(uCurrent, clamp(p + ivec2(x, y), ivec2(0), maxP), 0).rgb);
            if (abs(x) + abs(y) == 1)
                cross += c;
            c = toYCoCg(c);
            m1 += c;
            m2 += c * c;
        }
    vec3 current = max(center + (center - cross * 0.25) * uSharpen * 0.5, vec3(0.0));
    if (uHistoryValid == 0) {
        fragColor = vec4(expand(min(current, vec3(0.999))), 1.0);
        return;
    }

    // Where this pixel was in the previous frame. The closest depth of the cross keeps thin
    // foreground edges (wires, branches) moving with their object.
    float depth = texelFetch(uDepth, p, 0).r;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i == 0 ? 1 : i == 1 ? -1 : 0, i == 2 ? 1 : i == 3 ? -1 : 0);
        depth = min(depth, texelFetch(uDepth, clamp(p + o, ivec2(0), maxP), 0).r);
    }
    vec2 historyUV = vUV;
    bool weapon = uOverlay != 0 && texelFetch(uOverlayDepth, p, 0).r < 1.0;
    if (!weapon) {
        vec4 prev = uReproject * vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
        historyUV = prev.xy / prev.w * 0.5 + 0.5 + uJitterUV;
    }
    if (any(lessThan(historyUV, vec2(0.0))) || any(greaterThan(historyUV, vec2(1.0)))) {
        fragColor = vec4(expand(min(current, vec3(0.999))), 1.0); // came into view
        return;
    }
    vec3 history = toYCoCg(compress(sampleHistory(historyUV)));

    // Variance clip: the history is pulled along the line towards the neighbourhood mean.
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    float gamma = weapon ? 0.75 : 1.0;
    vec3 lo = mean - gamma * sigma, hi = mean + gamma * sigma;
    vec3 toHistory = history - mean;
    vec3 unit = toHistory / max(hi - mean, vec3(1e-5));
    float scale = max(abs(unit.x), max(abs(unit.y), abs(unit.z)));
    if (scale > 1.0)
        history = mean + toHistory / scale;
    history = fromYCoCg(history);

    // Less history while the image moves fast (less smearing), more when it stands still.
    vec2 motion = (historyUV - vUV) * vec2(textureSize(uCurrent, 0));
    float feedback = mix(uFeedback, 0.6, clamp(length(motion) / 12.0, 0.0, 1.0));
    vec3 result = mix(current, history, feedback);
    fragColor = vec4(expand(min(max(result, vec3(0.0)), vec3(0.999))), 1.0);
}
