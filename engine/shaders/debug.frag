#version 450 core
in vec4 vColor; // linear rgb, alpha
out vec4 fragColor;

void main() {
    fragColor = vColor;
}
