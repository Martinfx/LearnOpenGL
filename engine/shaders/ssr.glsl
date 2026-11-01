// Screen-space reflections, traced inside pbr.frag (the renderer is forward: no G-buffer).
// The ray marches through the depth of this frame's pre-pass; the colour at the hit comes
// from the previous frame's HDR image, reprojected (the current one isn't drawn yet).
// Where the ray leaves the screen or finds nothing, the probe / sky reflection stays.
layout(binding = 20) uniform sampler2D uSsrDepth; // this frame's opaque depth (pre-pass)
layout(binding = 21) uniform sampler2D uSsrColor; // previous frame's HDR, a = 0 under the weapon

float ssrViewZ(float depth) { // view-space z (negative) from window depth, perspective only
    return -uProj[3][2] / ((depth * 2.0 - 1.0) + uProj[2][2]);
}

// rgb: reflected colour, a: how much of it to use (0 = keep the probe / sky reflection).
vec4 screenReflection(vec3 worldPos, vec3 rWorld, float rough, float nv) {
    float strength = uSsr.z * (1.0 - smoothstep(uSsr.w * 0.6, uSsr.w, rough));
    if (uSsr.x < 0.5 || strength <= 0.0)
        return vec4(0.0);
    vec3 origin = (uView * vec4(worldPos, 1.0)).xyz;
    vec3 dir = mat3(uView) * rWorld;
    // Rays toward the camera only see what's behind it, which isn't on screen.
    float toward = smoothstep(0.2, 0.6, dir.z);
    if (toward >= 1.0)
        return vec4(0.0);
    float maxDist = uSsr.y;
    float stepLen = max(0.08, -origin.z * 0.015);
    float t = stepLen * (0.5 + hash12(gl_FragCoord.xy + uSkyHorizon.w * 61.0)); // fewer bands
    float lastT = 0.0;
    bool hit = false;
    const int kSteps = 28;
    for (int i = 0; i < kSteps && t < maxDist; ++i) {
        vec3 p = origin + dir * t;
        if (p.z > -0.05)
            break; // behind the camera
        vec4 clip = uProj * vec4(p, 1.0);
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            break;
        float depth = textureLod(uSsrDepth, uv, 0.0).r;
        float behind = ssrViewZ(depth) - p.z; // > 0: the ray passed behind the surface
        if (depth < 1.0 && behind > 0.0 && behind < max(stepLen * 2.0, 0.25 + t * 0.03)) {
            // Refine between the last point in front and this one.
            float a = lastT, b = t;
            for (int j = 0; j < 5; ++j) {
                float m = 0.5 * (a + b);
                vec3 q = origin + dir * m;
                vec4 qc = uProj * vec4(q, 1.0);
                float qd = textureLod(uSsrDepth, qc.xy / qc.w * 0.5 + 0.5, 0.0).r;
                if (ssrViewZ(qd) - q.z > 0.0)
                    b = m;
                else
                    a = m;
            }
            t = b;
            hit = true;
            break;
        }
        lastT = t;
        t += stepLen;
        stepLen *= 1.18;
    }
    if (!hit)
        return vec4(0.0);
    vec3 hitWorld = worldPos + rWorld * t;
    vec4 prev = uPrevViewProj * vec4(hitWorld, 1.0);
    if (prev.w <= 0.0)
        return vec4(0.0);
    vec2 prevUV = prev.xy / prev.w * 0.5 + 0.5;
    vec2 edge = min(prevUV, 1.0 - prevUV);
    float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y));
    vec4 clip = uProj * vec4(origin + dir * t, 1.0);
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    vec2 edgeNow = min(uv, 1.0 - uv);
    fade *= smoothstep(0.0, 0.08, min(edgeNow.x, edgeNow.y));
    fade *= 1.0 - smoothstep(maxDist * 0.6, maxDist, t);
    fade *= 1.0 - toward;
    vec4 c = textureLod(uSsrColor, prevUV, rough * 5.0); // premultiplied: a = 0 under the weapon
    fade *= smoothstep(0.5, 0.9, c.a);
    return vec4(c.rgb / max(c.a, 1e-3), clamp(fade * strength, 0.0, 1.0));
}
