#version 450 core
// Camera under a water surface: the scene fades into the water's colour with distance.
#include "common.glsl"
in vec2 vUV;
layout(binding = 0) uniform sampler2D uSceneColor;
layout(binding = 1) uniform sampler2D uSceneDepth;
uniform vec3 uWaterColor;
uniform float uClarity;
uniform float uSurface; // water level (above the camera)
out vec4 fragColor;
void main() {
    float t = uSkyHorizon.w;
    // A little wobble, like looking through moving water.
    vec2 uv = vUV + vec2(sin(vUV.y * 40.0 + t * 2.0), cos(vUV.x * 33.0 + t * 1.7)) * 0.0025;
    float depth = texture(uSceneDepth, uv).r;
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 world = p.xyz / p.w;
    float dist = depth >= 1.0 ? 1e4 : length(world - uCameraPos.xyz);
    vec3 absorb = vec3(1.0, 0.55, 0.38) / max(uClarity, 0.05);
    vec3 transmit = exp(-absorb * dist);
    vec3 sunDir = -normalize(uSunDir.xyz);
    // Darker the deeper the camera is.
    float depthLight = exp(-max(uSurface - uCameraPos.y, 0.0) / max(uClarity * 2.0, 0.1));
    float daylight = (uSunDir.w * max(sunDir.y, 0.0) * 0.12 + uGroundColor.w * 0.35) * depthLight;
    vec3 inscatter = uWaterColor * (uSunColor.rgb * daylight + uSkyZenith.rgb * uGroundColor.w * 0.4 * depthLight) * 4.0;
    vec3 color = texture(uSceneColor, uv).rgb * transmit + inscatter * (1.0 - transmit);
    fragColor = vec4(color, 1.0);
}
