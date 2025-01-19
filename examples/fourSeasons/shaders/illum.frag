#version 460

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D gPosition;
layout(set = 0, binding = 2) uniform sampler2D gNormal;
layout(set = 0, binding = 3) uniform sampler2D gAlbedo;

layout(set = 0, binding = 4) uniform uniformSSAO
{
    int enabled;
    int blur;
    int illumination;
} SSAO;

void main() {
    if (SSAO.illumination == 1) {
        fs_out = vec4(0.0, 1.0, 0.0, 0.0);
    }
    else {
        fs_out = vec4(texture(screenTexture, texCoord).rrr, 1.0);
    }
}