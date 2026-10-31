#version 450 core
#include "common.glsl"
// Particles and decals: one quad per instance, corners = position ± axisX ± axisY.
struct Sprite {
    vec4 positionFrame; // xyz, w: flipbook cell
    vec4 axisX;
    vec4 axisY;
    vec4 color;         // linear rgb, alpha
};
layout(std430, binding = 3) readonly buffer Sprites { Sprite sprites[]; };
uniform int uSpriteBase;
uniform mat4 uSpriteViewProj;
uniform float uDecalLift; // m toward the camera (decals against z-fighting)
out vec2 vUV;
out vec4 vColor;
out float vFrame;
out vec3 vWorld;
out vec3 vNormal;

const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    Sprite s = sprites[uSpriteBase + gl_InstanceID];
    vec2 c = kCorners[gl_VertexID];
    vec3 p = s.positionFrame.xyz + c.x * s.axisX.xyz + c.y * s.axisY.xyz;
    vNormal = normalize(cross(s.axisX.xyz, s.axisY.xyz));
    if (uDecalLift > 0.0)
        p += normalize(uCameraPos.xyz - p) * uDecalLift;
    vWorld = p;
    vUV = vec2(c.x * 0.5 + 0.5, 0.5 - c.y * 0.5); // image top = quad top
    vColor = s.color;
    vFrame = s.positionFrame.w;
    gl_Position = uSpriteViewProj * vec4(p, 1.0);
}
