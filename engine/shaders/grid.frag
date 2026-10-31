#version 450 core
#include "common.glsl"
// Editor ground grid: 1 m minor and 10 m major lines on the plane through the
// origin (y = 0, or x = 0 / z = 0 for side views), depth-tested against the scene.
in vec2 vUV;
uniform int uGridAxis;      // plane normal: 0 X, 1 Y, 2 Z
uniform float uGridFade;    // distance where the grid has faded out (m)
uniform mat4 uGridViewProj;
out vec4 fragColor;

float lines(vec2 p, float spacing) {
    vec2 c = p / spacing;
    vec2 w = max(fwidth(c), vec2(1e-4));
    vec2 g = abs(fract(c - 0.5) - 0.5) / w;
    // Lines narrower than a pixel fade out instead of shimmering.
    float density = clamp(1.6 - max(w.x, w.y) * 3.0, 0.0, 1.0);
    return (1.0 - min(min(g.x, g.y), 1.0)) * density;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 n = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 f = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 a = n.xyz / n.w;
    vec3 d = f.xyz / f.w - a;
    float dn = d[uGridAxis];
    if (abs(dn) < 1e-6)
        discard;
    float t = -a[uGridAxis] / dn;
    if (t < 0.0 || t > 1.0)
        discard;
    vec3 p = a + d * t;
    vec2 q = uGridAxis == 1 ? p.xz : (uGridAxis == 0 ? p.zy : p.xy);

    float minor = lines(q, 1.0);
    float major = lines(q, 10.0);
    float fade = 1.0 - smoothstep(uGridFade * 0.4, uGridFade, length(p - uCameraPos.xyz));
    vec3 color = mix(vec3(0.35), vec3(0.75), major);
    float alpha = max(minor * 0.35, major * 0.7);
    // World axes through the origin: X red, Z blue (Y green in side views).
    vec2 w = fwidth(q);
    float axisU = 1.0 - min(abs(q.y) / max(w.y, 1e-4), 1.0);  // line along the first plane axis
    float axisV = 1.0 - min(abs(q.x) / max(w.x, 1e-4), 1.0);
    vec3 colorU = uGridAxis == 0 ? vec3(0.2, 0.35, 1.0) : vec3(1.0, 0.2, 0.2);
    vec3 colorV = uGridAxis == 1 ? vec3(0.2, 0.35, 1.0) : vec3(0.2, 1.0, 0.3);
    color = mix(color, colorU, axisU);
    color = mix(color, colorV, axisV);
    alpha = max(alpha, max(axisU, axisV) * 0.9) * fade;
    if (alpha < 0.01)
        discard;
    // Depth of the grid point, nudged 2 cm toward the camera so floors at y = 0 do not hide it.
    vec4 clip = uGridViewProj * vec4(p - normalize(d) * 0.02, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
    fragColor = vec4(color * 0.6, alpha);
}
