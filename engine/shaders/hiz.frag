#version 450 core
// Occlusion culling: farthest depth of each block of the depth pre-pass, read back
// to the CPU (a frame or two later) to skip objects hidden behind nearer ones.
layout(binding = 0) uniform sampler2D uDepth;
uniform ivec2 uSrcSize;
uniform ivec2 uDstSize;
out float oDepth;

void main() {
    ivec2 dst = ivec2(gl_FragCoord.xy);
    ivec2 a = dst * uSrcSize / uDstSize;
    ivec2 b = min(max(a + 1, ((dst + 1) * uSrcSize + uDstSize - 1) / uDstSize), uSrcSize);
    float far = 0.0;
    for (int y = a.y; y < b.y; ++y)
        for (int x = a.x; x < b.x; ++x)
            far = max(far, texelFetch(uDepth, ivec2(x, y), 0).r);
    oDepth = far;
}
