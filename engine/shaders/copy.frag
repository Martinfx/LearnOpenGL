#version 450 core
// Plain copy (used when FXAA is off).
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uSource, vUV).rgb, 1.0); }
