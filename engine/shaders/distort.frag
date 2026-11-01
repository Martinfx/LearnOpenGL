#version 450 core
#include "common.glsl"
// Air distortion: particles with the "distort" blend and anomaly warps write a screen
// offset (pixels, rg) into their own target, depth-tested against the scene so walls in
// front hide it; post.frag shifts the image by it. vColor: a * g = strength, b < 0 = lens.
in vec2 vUV;
in vec4 vColor;
in float vFrame;
in vec3 vWorld;
layout(binding = 0) uniform sampler2D uSpriteTexture;
uniform bool uSpriteHasTexture;
uniform ivec2 uSpriteGrid;
layout(location = 0) out vec2 outOffset;

void main() {
    vec2 c = vec2(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0); // quad space, +y up
    float r = length(c);
    float mask;
    if (uSpriteHasTexture) {
        int cell = int(vFrame);
        vec2 cellSize = 1.0 / vec2(uSpriteGrid);
        vec2 origin = vec2(cell % uSpriteGrid.x, cell / uSpriteGrid.x) * cellSize;
        mask = texture(uSpriteTexture, origin + vUV * cellSize).a;
    } else {
        mask = 1.0 - smoothstep(0.35, 1.0, r);
    }
    float strength = vColor.a * vColor.g;
    if (mask <= 0.0 || strength <= 0.0)
        discard;
    // Radius of the quad on screen in pixels (fwidth of the quad coordinate per pixel).
    float radiusPx = 0.5 / max(max(fwidth(vUV.x), fwidth(vUV.y)), 1e-5);
    vec2 offset;
    if (vColor.b >= 0.0) {
        // Heat shimmer: fine rising ripples in the quad's own space.
        vec2 p = vUV * 2.0;
        float t = uSkyHorizon.w;
        vec2 wobble = vec2(sin(p.y * 38.0 - t * 9.0 + sin(p.x * 21.0) * 1.7), cos(p.x * 29.0 + t * 6.0 + p.y * 13.0));
        offset = wobble * mask * strength * radiusPx * 0.022;
    } else {
        // Lens: pulls the image toward the middle and twists it a little.
        vec2 dir = c / max(r, 1e-5);
        vec2 swirl = vec2(-dir.y, dir.x);
        float bend = sin(min(r, 1.0) * 3.14159) * mask * strength;
        offset = (dir * 0.16 + swirl * 0.05 * sin(uSkyHorizon.w * 1.3 + r * 6.0)) * bend * radiusPx;
    }
    outOffset = offset;
}
