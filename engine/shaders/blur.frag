#version 450 core
// 4x4 blur of the ambient occlusion term (matches its 4x4 pattern), weighted by depth so
// it doesn't bleed across silhouettes (a wall's dark corner stays off the sky behind it).
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource; // r: visibility, g: linear depth (m)
out float fragColor;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSource, 0));
    float centerDepth = texture(uSource, vUV).g;
    float sum = 0.0, weights = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x) {
            vec2 s = texture(uSource, vUV + (vec2(x, y) + 0.5) * texel).rg;
            float w = 1.0 / (1e-3 + abs(s.g - centerDepth) / max(centerDepth * 0.05, 0.02));
            sum += s.r * w;
            weights += w;
        }
    fragColor = weights > 0.0 ? sum / weights : 1.0;
}
