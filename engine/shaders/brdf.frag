#version 450 core
// Split-sum BRDF lookup table: x = N.V, y = roughness -> (scale, bias) on F0.
in vec2 vUV;
out vec2 fragColor;
const float PI = 3.14159265;

float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float geometryIbl(float nv, float nl, float rough) {
    float k = rough * rough / 2.0;
    return nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
}

void main() {
    float nv = max(vUV.x, 1e-3), rough = vUV.y;
    vec3 v = vec3(sqrt(1.0 - nv * nv), 0.0, nv);
    float a = 0.0, b = 0.0;
    const uint kSamples = 256u;
    for (uint i = 0u; i < kSamples; ++i) {
        vec2 xi = vec2(float(i) / float(kSamples), radicalInverse(i));
        float r2 = rough * rough;
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (r2 * r2 - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);
        float nl = max(l.z, 0.0), nh = max(h.z, 0.0), vh = max(dot(v, h), 0.0);
        if (nl > 0.0) {
            float g = geometryIbl(nv, nl, rough);
            float gVis = g * vh / (nh * nv);
            float fc = pow(1.0 - vh, 5.0);
            a += (1.0 - fc) * gVis;
            b += fc * gVis;
        }
    }
    fragColor = vec2(a, b) / float(kSamples);
}
