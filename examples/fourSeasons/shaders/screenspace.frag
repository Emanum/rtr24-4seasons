#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;

void main() {
    vec4 color = texture(screenTexture, texCoord);
    fs_out = color;
}