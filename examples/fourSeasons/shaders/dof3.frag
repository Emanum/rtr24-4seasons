#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D ssaoTexture;
layout(set = 0, binding = 1) uniform sampler2D nearTexture;
layout(set = 0, binding = 2) uniform sampler2D farTexture;
layout(set = 0, binding = 3) uniform sampler2D depthTexture;

layout(set = 0, binding = 4) uniform uniformDoF
{
    int enabled;
    int mode;//0-> depth, 1-> gaussian, 2-> bokeh
    float focus;
    float range;
    float distOutOfFocus;
    float nearPlane;
    float farPlane;
    vec2[49] bokehKernel;
} DoF;

// Gaussian kernel
const float kernel[9] = float[](0.027, 0.065, 0.121, 0.194, 0.227, 0.194, 0.121, 0.065, 0.027);

const float blurPower = 40.0;

const vec3 foregroundColor = vec3(1.0, 0.0, 0.0);//red

const vec3 backgroundColor = vec3(0.0, 1.0, 0.0);//green

const vec3 centerColor = vec3(0.0, 0.0, 0.0);//black

void main() {
    if(DoF.enabled == 1)
    {
        float depth = texture(depthTexture, texCoord).r;
        if (DoF.mode == 0)
        {
            vec4 farTextureVal = texture(farTexture, texCoord);
            vec4 nearTextureVal = texture(nearTexture, texCoord);
            fs_out = farTextureVal + nearTextureVal;
        }else if(DoF.mode == 1 || DoF.mode == 2)
        {
            vec4 ogColor = texture(ssaoTexture, texCoord);
            //bokeh
            vec4 blurColor = vec4(0.0);
            fs_out = texture(ssaoTexture, texCoord);
        }

    }else{
        fs_out = texture(ssaoTexture, texCoord);
    }
}