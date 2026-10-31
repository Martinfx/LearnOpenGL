#version 450 core
// Bloom upsample with a 3x3 tent filter, added onto the larger level.
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSource;
uniform float uRadius;
out vec3 fragColor;
void main() {
    vec2 t = uRadius / vec2(textureSize(uSource, 0));
    vec3 s = texture(uSource, vUV).rgb * 4.0;
    s += (texture(uSource, vUV + vec2(-t.x, 0)).rgb + texture(uSource, vUV + vec2(t.x, 0)).rgb +
          texture(uSource, vUV + vec2(0, -t.y)).rgb + texture(uSource, vUV + vec2(0, t.y)).rgb) * 2.0;
    s += texture(uSource, vUV + vec2(-t.x, -t.y)).rgb + texture(uSource, vUV + vec2(t.x, -t.y)).rgb +
         texture(uSource, vUV + vec2(-t.x, t.y)).rgb + texture(uSource, vUV + vec2(t.x, t.y)).rgb;
    fragColor = s / 16.0;
}
