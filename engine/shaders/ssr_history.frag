#version 450 core
// Keeps this frame's HDR image for the next frame's screen-space reflections. The weapon
// layer (and a pixel around it, which TAA spreads it into) gets alpha 0, colour premultiplied,
// so that reflections, which read blurred mip levels, never show it.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource;
layout(binding = 1) uniform sampler2D uOverlayDepth;
uniform int uOverlay;
out vec4 fragColor;
void main() {
    vec3 c = min(texture(uSource, vUV).rgb, vec3(1000.0));
    float keep = 1.0;
    if (uOverlay != 0) {
        ivec2 p = ivec2(gl_FragCoord.xy), maxP = textureSize(uOverlayDepth, 0) - 1;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (texelFetch(uOverlayDepth, clamp(p + ivec2(x, y), ivec2(0), maxP), 0).r < 1.0)
                    keep = 0.0;
    }
    fragColor = vec4(c * keep, keep);
}
