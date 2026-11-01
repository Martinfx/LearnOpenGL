// Local lights, clustered: the screen is split into tiles and depth slices; each
// cluster lists the lights whose range reaches it (built on the CPU every frame).
struct LocalLight {
    vec4 posRange;   // xyz, w: range
    vec4 colorType;  // rgb linear * intensity, w: 0 point, 1 spot
    vec4 dirShadow;  // xyz spot axis, w: first shadow layer (-1 = none)
    vec4 cone;       // x: cos outer, y: 1 / (cos inner - cos outer), z: shadow normal offset per m, w: 1 = flashlight cookie
};
layout(std430, binding = 5) readonly buffer LocalLights { LocalLight uLights[]; };
layout(std430, binding = 6) readonly buffer LightGrid {
    vec4 uClusterParams;  // x: slice scale, y: slice bias (slice = log(depth) * x + y), z: near
    ivec4 uClusterDims;   // x, y, z, w: light count
    uvec2 uClusters[];    // offset into uLightIndices, count
};
layout(std430, binding = 7) readonly buffer LightIndices { uint uLightIndices[]; };
layout(std430, binding = 8) readonly buffer LightShadows { mat4 uLightShadowMatrices[]; };
layout(binding = 13) uniform sampler2DArrayShadow uLocalShadowMap;

// Shadow of a local light (spot: one layer, point: the cube face the fragment is on).
float localShadow(LocalLight light, vec3 worldPos, vec3 geomNormal, float dist) {
    int layer = int(light.dirShadow.w);
    if (light.colorType.w < 0.5) {
        vec3 d = worldPos - light.posRange.xyz;
        vec3 a = abs(d);
        if (a.x >= a.y && a.x >= a.z)
            layer += d.x > 0.0 ? 0 : 1;
        else if (a.y >= a.z)
            layer += d.y > 0.0 ? 2 : 3;
        else
            layer += d.z > 0.0 ? 4 : 5;
    }
    vec3 offsetPos = worldPos + geomNormal * dist * light.cone.z * 1.5;
    vec4 p = uLightShadowMatrices[layer] * vec4(offsetPos, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (any(lessThan(c, vec3(0.0))) || any(greaterThan(c, vec3(1.0))))
        return 1.0;
    vec2 size = vec2(textureSize(uLocalShadowMap, 0).xy);
    float sum = 0.0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            sum += texture(uLocalShadowMap, vec4(c.xy + (vec2(x, y) - 0.5) / size, float(layer), c.z - 0.00005));
    return sum * 0.25;
}

// Cluster (offset into uLightIndices, count) of a screen position (0..1) at a view depth.
uvec2 lightCluster(vec2 screenUv, float viewDepth) {
    ivec3 cell;
    cell.xy = clamp(ivec2(screenUv * vec2(uClusterDims.xy)), ivec2(0), uClusterDims.xy - 1);
    cell.z = clamp(int(floor(log(max(viewDepth, uClusterParams.z)) * uClusterParams.x + uClusterParams.y)), 0, uClusterDims.z - 1);
    return uClusters[cell.x + cell.y * uClusterDims.x + cell.z * uClusterDims.x * uClusterDims.y];
}

// Flashlight reflector: hot centre, a darker ring, a faint bright rim.
float flashlightCookie(LocalLight light, float cd) {
    float t = acos(clamp(cd, -1.0, 1.0)) / max(acos(light.cone.x), 1e-3); // 1 at the edge
    return 0.55 + 0.9 * exp(-t * t * 18.0) - 0.18 * exp(-pow((t - 0.42) * 9.0, 2.0)) + 0.12 * exp(-pow((t - 0.72) * 10.0, 2.0));
}
