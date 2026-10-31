#version 450 core
// FXAA (simplified from Lottes' FXAA 3.11 "PC quality") on the graded image.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource;
out vec4 fragColor;
float luma(vec2 uv) { return texture(uSource, uv).a; }
void main() {
    vec2 t = 1.0 / vec2(textureSize(uSource, 0));
    float lM = luma(vUV);
    float lN = luma(vUV + vec2(0, t.y)), lS = luma(vUV - vec2(0, t.y));
    float lE = luma(vUV + vec2(t.x, 0)), lW = luma(vUV - vec2(t.x, 0));
    float lMin = min(lM, min(min(lN, lS), min(lE, lW)));
    float lMax = max(lM, max(max(lN, lS), max(lE, lW)));
    float range = lMax - lMin;
    vec3 center = texture(uSource, vUV).rgb;
    if (range < max(0.0312, lMax * 0.125)) {
        fragColor = vec4(center, 1.0);
        return;
    }
    float lNW = luma(vUV + vec2(-t.x, t.y)), lNE = luma(vUV + t), lSW = luma(vUV - t), lSE = luma(vUV + vec2(t.x, -t.y));
    vec2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y = ((lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * t;
    vec3 a = 0.5 * (texture(uSource, vUV + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uSource, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uSource, vUV - dir * 0.5).rgb + texture(uSource, vUV + dir * 0.5).rgb);
    float lB = dot(b, vec3(0.299, 0.587, 0.114));
    fragColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
