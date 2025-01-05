#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(set = 0, binding = 2) uniform uniformDoF
{
    int enabled;
    float focus;
    float range;
    float distOutOfFocus;
} DoF;

// Gaussian kernel
const float kernel[9] = float[](0.027, 0.065, 0.121, 0.194, 0.227, 0.194, 0.121, 0.065, 0.027);

const float blurPower = 10.0;

void main() {
   //if enabled -> show depth buffer else show screen texture
    if(DoF.enabled == 1)
    {
        vec2 texelSize = 1.0 / textureSize(screenTexture, 0);
        vec3 color = vec3(0.0);

        // Apply Gaussian blur vertically
        for (int i = -4; i <= 4; ++i) {
            color += texture(screenTexture, texCoord + vec2(0.0, texelSize.y * float(i))).rgb * kernel[i + 4];
        }
        

        // Apply Gaussian blur horizontally
        vec3 blurredColor = vec3(0.0);
        for (int i = -4; i <= 4; ++i) {
            blurredColor += color * kernel[i + 4];
        }

        fs_out = vec4(blurredColor, 1.0);
        
    }else{
        fs_out = texture(screenTexture, texCoord);
    }

        
}