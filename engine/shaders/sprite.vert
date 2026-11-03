#version 450 core
#include "common.glsl"
#include "lights.glsl"
// Particles and decals: one quad per instance, corners = position ± axisX ± axisY.
// Lit particles get their light per corner: the sun (with its cascaded shadow), the sky and
// every local light near them (a fire lights its own smoke from below) are gathered into six
// directions of the sprite's frame (right, left, up, down, toward the eye, away from it).
// sprite.frag mixes them with a six-way light map (smoke rendered with self-shadowing from
// those six sides) or with a soft fake one.
struct Sprite {
    vec4 positionFrame; // xyz, w: flipbook cell
    vec4 axisX;
    vec4 axisY;
    vec4 color;         // linear rgb, alpha
};
layout(std430, binding = 3) readonly buffer Sprites { Sprite sprites[]; };
layout(binding = 6) uniform sampler2DArray uShadowMap; // raw depth
uniform int uSpriteBase;
uniform mat4 uSpriteViewProj;
uniform float uDecalLift; // m toward the camera (decals against z-fighting)
uniform int uSpriteLit;
uniform int uSpriteDecal;
uniform float uSpriteScatter; // forward scattering: smoke between the eye and a light glows
out vec2 vUV;
out vec4 vColor;
out float vFrame;
out vec3 vWorld;
out vec3 vNormal;
// Light arriving from the six sides of the sprite: right (+X), left, up (+Y), down,
// toward the eye (+Z, the side the viewer sees), away from it.
out vec3 vLightRight;
out vec3 vLightLeft;
out vec3 vLightUp;
out vec3 vLightDown;
out vec3 vLightFront;
out vec3 vLightBack;

const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

// Sun shadow at a point in the air: 4 bilinear taps of the cascade it is in.
float airShadow(vec3 p) {
    if (uSunColor.w < 0.5)
        return 1.0;
    float viewDepth = -(uView * vec4(p, 1.0)).z;
    int count = int(uCascadeSplits.w);
    if (count <= 0 || viewDepth > uCascadeSplits[count - 1])
        return 1.0;
    int cascade = count - 1;
    for (int i = 0; i < count; ++i)
        if (viewDepth < uCascadeSplits[i]) { cascade = i; break; }
    vec4 c4 = uShadowMatrices[cascade] * vec4(p, 1.0);
    vec3 c = c4.xyz / c4.w * 0.5 + 0.5;
    if (c.z >= 1.0 || any(lessThan(c.xy, vec2(0.0))) || any(greaterThan(c.xy, vec2(1.0))))
        return 1.0;
    vec2 size = vec2(textureSize(uShadowMap, 0).xy);
    float ref = c.z - uShadowParams.x * 2.0 * (1.0 + float(cascade));
    float lit = 0.0;
    for (int y = -1; y <= 1; y += 2)
        for (int x = -1; x <= 1; x += 2) {
            vec4 d = textureGather(uShadowMap, vec3(c.xy + vec2(x, y) * 1.5 / size, float(cascade)));
            lit += dot(step(vec4(ref), d), vec4(0.25));
        }
    float fade = smoothstep(uCascadeSplits[count - 1] * 0.85, uCascadeSplits[count - 1], viewDepth);
    return mix(lit * 0.25, 1.0, fade);
}

vec3 L[6]; // +X, -X, +Y, -Y, +Z, -Z in the sprite frame

void addLight(vec3 dirInSprite, vec3 radiance) {
    vec3 w = dirInSprite * dirInSprite; // squared: weights sum to 1 for any direction
    if (dirInSprite.x > 0.0) L[0] += radiance * w.x; else L[1] += radiance * w.x;
    if (dirInSprite.y > 0.0) L[2] += radiance * w.y; else L[3] += radiance * w.y;
    if (dirInSprite.z > 0.0) L[4] += radiance * w.z; else L[5] += radiance * w.z;
}

// Sky and ground light arriving from a world direction.
vec3 ambient(vec3 d, float sky) {
    vec3 zenith = uSkyZenith.rgb, horizon = uSkyHorizon.rgb;
    vec3 ground = uGroundColor.rgb * (horizon + zenith) * 0.5 * 0.6;
    vec3 a = d.y >= 0.0 ? mix(horizon, zenith, d.y) : mix(horizon * 0.7, ground, -d.y);
    return a * uGroundColor.w * sky;
}

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

    for (int i = 0; i < 6; ++i)
        L[i] = vec3(0.0);
    if (uSpriteLit != 0 && uSpriteDecal == 0) {
        vec3 R = normalize(s.axisX.xyz), U = normalize(s.axisY.xyz);
        vec3 F = cross(R, U); // toward the eye
        mat3 toSprite = transpose(mat3(R, U, F));
        // Light is gathered half way between the corner and the centre: smoother over the quad.
        vec3 q = mix(s.positionFrame.xyz, p, 0.6);
        vec3 eyeDir = normalize(q - uCameraPos.xyz);
        float sky = interiorFactors(q).x;
        // Sky: each of the six sides sees the sky (or the ground) in its direction.
        L[0] = ambient(R, sky);
        L[1] = ambient(-R, sky);
        L[2] = ambient(U, sky);
        L[3] = ambient(-U, sky);
        L[4] = ambient(F, sky);
        L[5] = ambient(-F, sky);
        for (int i = 0; i < 6; ++i)
            L[i] *= 0.35; // sky light per side (the six maps of a thin wisp add up to about 3-4)
        // Sun (or moon) with its shadow; forward scattering when the eye looks toward it.
        vec3 toSun = -normalize(uSunDir.xyz);
        float forward = pow(max(dot(eyeDir, toSun), 0.0), 6.0);
        vec3 sun = uSunColor.rgb * uSunDir.w / PI * airShadow(q) * (1.0 + uSpriteScatter * forward);
        addLight(toSprite * toSun, sun);
        // Local lights (clustered like the opaque pass; no local shadows in the air).
        if (uClusterDims.w > 0) {
            float depth = -(uView * vec4(q, 1.0)).z;
            vec4 clip = uSpriteViewProj * vec4(q, 1.0);
            vec2 screen = clamp(clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5, 0.0, 1.0);
            uvec2 cluster = lightCluster(screen, depth);
            for (uint i = 0u; i < cluster.y; ++i) {
                LocalLight light = uLights[uLightIndices[cluster.x + i]];
                vec3 toLight = light.posRange.xyz - q;
                float d2 = dot(toLight, toLight);
                float r2 = light.posRange.w * light.posRange.w;
                if (d2 >= r2)
                    continue;
                vec3 l = toLight * inversesqrt(max(d2, 1e-6));
                float window = clamp(1.0 - (d2 / r2) * (d2 / r2), 0.0, 1.0);
                float atten = window * window / (d2 + 1.0);
                if (light.colorType.w > 0.5) {
                    float cd = dot(-l, light.dirShadow.xyz);
                    float cone = clamp((cd - light.cone.x) * light.cone.y, 0.0, 1.0);
                    atten *= cone * cone;
                }
                if (atten <= 0.0)
                    continue;
                float fwd = pow(max(dot(eyeDir, l), 0.0), 6.0);
                // Half: light that reaches a cloud of soot is partly absorbed before it scatters back.
                addLight(toSprite * l, light.colorType.rgb * atten * (0.5 / PI) * (1.0 + 0.5 * uSpriteScatter * fwd));
            }
        }
    }
    vLightRight = L[0];
    vLightLeft = L[1];
    vLightUp = L[2];
    vLightDown = L[3];
    vLightFront = L[4];
    vLightBack = L[5];
}
