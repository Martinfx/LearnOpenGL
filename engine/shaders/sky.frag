#version 450 core
#include "common.glsl"
// Background: procedural sky drawn where nothing else was (depth = 1).
in vec2 vUV;
out vec4 fragColor;
void main() {
    vec4 p = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - uCameraPos.xyz);
    vec3 color = skyColor(dir, true);
    // Fog thickens toward the horizon so distant geometry blends into the sky.
    float horizon = 1.0 - smoothstep(0.0, 0.25, abs(dir.y));
    float haze = clamp(uFogColor.w * 30.0, 0.0, 1.0);
    color = mix(color, uFogColor.rgb, horizon * haze * 0.8);
    // Below the horizon there is no modeled terrain: fade the ground into the fog.
    color = mix(color, uFogColor.rgb * 0.8, haze * 0.85 * smoothstep(0.02, -0.08, dir.y));
    // Volumetric fog over its whole range (lamps and the flashlight glow against the sky too).
    if (uVolumetric.x > 0.5) {
        vec4 volume = texture(uVolumeFog, vec3(vUV, 1.0));
        color = color * volume.a + volume.rgb;
    }
    fragColor = vec4(color, 1.0);
}
