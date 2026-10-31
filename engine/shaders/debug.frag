#version 450 core
uniform vec4 uDebugColor; // linear rgb, alpha
out vec4 fragColor;

void main() {
    fragColor = uDebugColor;
}
