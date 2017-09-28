#version 330 core

varying vec3 color;
varying vec2 TexCoord;
uniform sampler2D texture1;
void main()
{
    gl_FragColor = texture(texture1, TexCoord);
}
