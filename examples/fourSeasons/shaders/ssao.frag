#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(set = 0, binding = 2) uniform uniformSSAO
{
    int enabled;
    int blur;
} SSAO;

float near = 0.3f;  
float far = 1000.0f;

// https://stackoverflow.com/questions/51108596/linearize-depth
float linearizeDepth(float depth) {
    return (near * far) / (far + depth * (near - far));
}

void main() {
    if (SSAO.enabled == 1) {
        float linDepth = linearizeDepth(texture(depthTexture, texCoord).r);
        fs_out = vec4(1.0, 0.0, 0.0, 1.0) * linDepth;
        if (SSAO.blur == 1) {
            fs_out = vec4(0.0, 1.0, 0.0, 1.0) * linDepth;
        }
    }
    
    fs_out = texture(screenTexture, texCoord);
}