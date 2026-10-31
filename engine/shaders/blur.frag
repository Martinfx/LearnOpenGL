#version 450 core
// 4x4 box blur of the SSAO term (matches the 4x4 noise period).
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource;
out float fragColor;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSource, 0));
    float sum = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x)
            sum += texture(uSource, vUV + (vec2(x, y) + 0.5) * texel).r;
    fragColor = sum / 16.0;
}
