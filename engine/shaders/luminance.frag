#version 450 core
// Auto-exposure metering: log2 luminance of the HDR image weighted toward the
// centre (rg = weighted log luminance, weight). Mipmapped down to one texel
// this gives the weighted average. The first-person weapon is left out.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uDepth;
uniform bool uMaskOverlay; // depth < 1 = weapon layer
out vec4 fragColor;
void main() {
    vec3 c = texture(uHdr, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    if (isnan(lum) || isinf(lum))
        lum = 0.0;
    vec2 d = (vUV - 0.5) * vec2(1.6, 2.0);
    float w = mix(0.25, 1.0, exp(-dot(d, d) * 2.5)); // centre-weighted metering
    if (uMaskOverlay && texture(uDepth, vUV).r < 1.0)
        w = 0.0;
    fragColor = vec4(log2(max(lum, 1e-4)) * w, w, 0.0, 1.0);
}
