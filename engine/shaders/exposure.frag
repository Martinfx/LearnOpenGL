#version 450 core
// Auto-exposure adaptation (1x1 target): eye-like exposure from the metered
// average luminance, limited to [min, max] EV around the manual exposure and
// eased toward the target over time (slower when the view gets darker).
layout(binding = 0) uniform sampler2D uLuminance; // rg: weighted log2 luminance, weight (all mips)
layout(binding = 1) uniform sampler2D uPrevious;  // r: previous exposure multiplier
uniform float uLevel;      // last mip level
uniform float uDt;         // seconds since the last frame
uniform vec2 uEvRange;     // min, max EV
uniform vec2 uSpeed;       // x: brightening (going into the dark), y: darkening (into the light), 1/s
uniform bool uReset;       // jump straight to the target
out vec4 fragColor;
void main() {
    vec2 m = textureLod(uLuminance, vec2(0.5), uLevel).rg;
    float avgLog = m.g > 1e-5 ? m.r / m.g : log2(0.18);
    // Middle grey (0.18) at the average; EV of the correction clamped.
    float ev = clamp(log2(0.18) - avgLog, uEvRange.x, uEvRange.y);
    float target = exp2(ev);
    float previous = texelFetch(uPrevious, ivec2(0), 0).r;
    if (uReset || isnan(previous) || isinf(previous) || previous <= 0.0) {
        fragColor = vec4(target, 0.0, 0.0, 1.0);
        return;
    }
    float speed = target > previous ? uSpeed.x : uSpeed.y;
    // Adapt in EV space so a stop up takes as long as a stop down.
    float e = mix(log2(previous), log2(target), 1.0 - exp(-uDt * speed));
    fragColor = vec4(exp2(e), 0.0, 0.0, 1.0);
}
