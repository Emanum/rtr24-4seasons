#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) out vec2 texCoord;

void main() {
    gl_Position = vec4(inPosition, 0.5, 1.0);
    texCoord = inPosition * 0.5 + 0.5; // Convert from [-1, 1] to [0, 1]
}