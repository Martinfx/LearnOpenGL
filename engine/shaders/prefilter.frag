#version 450 core
// GGX prefiltering of the environment cubemap for one roughness level
// (importance sampling, mip filtered to avoid fireflies; Karis 2013).
in vec2 vUV;
layout(binding = 0) uniform samplerCube uSource;
uniform mat3 uFace;
uniform float uRoughness;
out vec4 fragColor;
const float PI = 3.14159265;

float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec3 importanceSampleGGX(vec2 xi, vec3 n, float rough) {
    float a = rough * rough;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(n.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    return normalize(t * h.x + b * h.y + n * h.z);
}

void main() {
    vec2 p = vUV * 2.0 - 1.0;
    vec3 n = normalize(uFace * vec3(p.x, p.y, 1.0));
    if (uRoughness < 0.01) {
        fragColor = vec4(textureLod(uSource, n, 0.0).rgb, 1.0);
        return;
    }
    const uint kSamples = 96u;
    float resolution = float(textureSize(uSource, 0).x);
    float saTexel = 4.0 * PI / (6.0 * resolution * resolution);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < kSamples; ++i) {
        vec2 xi = vec2(float(i) / float(kSamples), radicalInverse(i));
        vec3 h = importanceSampleGGX(xi, n, uRoughness);
        vec3 l = normalize(2.0 * dot(n, h) * h - n);
        float nl = dot(n, l);
        if (nl > 0.0) {
            float nh = max(dot(n, h), 0.0);
            float a2 = pow(uRoughness, 4.0);
            float d = a2 / (PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
            float pdf = d / 4.0 + 1e-4;
            float saSample = 1.0 / (float(kSamples) * pdf + 1e-4);
            float mip = 0.5 * log2(saSample / saTexel) + 1.0;
            sum += textureLod(uSource, l, mip).rgb * nl;
            weight += nl;
        }
    }
    fragColor = vec4(sum / max(weight, 1e-4), 1.0);
}
