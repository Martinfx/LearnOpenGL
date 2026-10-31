#version 450 core
// Bloom downsample (13 taps, Jimenez 2014). The first pass also applies the threshold.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource;
uniform bool uFirst;
uniform float uThreshold;
out vec3 fragColor;
vec3 tap(vec2 offset) {
    vec3 c = texture(uSource, vUV + offset).rgb;
    return any(isnan(c)) || any(isinf(c)) ? vec3(0.0) : min(c, vec3(60000.0)); // never spread NaN/Inf
}
void main() {
    vec2 t = 1.0 / vec2(textureSize(uSource, 0));
    vec3 a = tap(t * vec2(-2, 2)), b = tap(t * vec2(0, 2)), c = tap(t * vec2(2, 2));
    vec3 d = tap(t * vec2(-2, 0)), e = tap(vec2(0)), f = tap(t * vec2(2, 0));
    vec3 g = tap(t * vec2(-2, -2)), h = tap(t * vec2(0, -2)), i = tap(t * vec2(2, -2));
    vec3 j = tap(t * vec2(-1, 1)), k = tap(t * vec2(1, 1)), l = tap(t * vec2(-1, -1)), m = tap(t * vec2(1, -1));
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (uFirst) {
        float brightness = max(color.r, max(color.g, color.b));
        float soft = clamp(brightness - uThreshold + 0.5, 0.0, 1.0);
        float contribution = max(brightness - uThreshold, soft * soft * 0.5) / max(brightness, 1e-4);
        color *= contribution;
    }
    fragColor = max(color, vec3(0.0));
}
