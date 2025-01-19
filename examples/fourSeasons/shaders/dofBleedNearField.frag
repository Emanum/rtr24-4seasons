#version 450

layout (location = 0) in vec2 texCoord;
layout (location = 0) out vec4 fs_out;

layout (set = 0, binding = 0) uniform sampler2D screenTexture;

const int kernelSize = 5;

//we simply apply a maxFilter to the texture, with a kernel of 5x5
void main() {
  ivec2 screenDimensions = textureSize(screenTexture,0);
  vec4 color = vec4(0.0);
    for(int x = -kernelSize; x <= kernelSize; x++) {
        for(int y = -kernelSize; y <= kernelSize; y++) {
            color = max(color, texture(screenTexture, texCoord + vec2(x, y) / screenDimensions));
        }
    }
  fs_out = color;
}