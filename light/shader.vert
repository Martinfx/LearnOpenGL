#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
attribute vec3 vCol;
attribute vec3 vPos;
attribute vec2 aTexCoord;
out vec3 color;
out vec2 TexCoord;
void main()
{
	gl_Position = projection * view * model * vec4(vPos, 1.0);
    TexCoord = vec2(aTexCoord.x, aTexCoord.y);
    color = vCol;
}
