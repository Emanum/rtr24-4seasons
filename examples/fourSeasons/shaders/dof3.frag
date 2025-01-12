#version 450

layout (location = 0) in vec2 texCoord;
layout (location = 0) out vec4 fs_out;

layout (set = 0, binding = 0) uniform sampler2D ssaoTexture;
layout (set = 0, binding = 1) uniform sampler2D nearTexture;
layout (set = 0, binding = 2) uniform sampler2D centerTexture;
layout (set = 0, binding = 3) uniform sampler2D farTexture;
layout (set = 0, binding = 4) uniform sampler2D depthTexture;
layout (set = 0, binding = 5) uniform uniformDoF
{
    int enabled;
    int mode;//0-> depth, 1-> gaussian, 2-> bokeh
    float focus;
    float range;
    float distOutOfFocus;
    float nearPlane;
    float farPlane;
    vec2[49] bokehKernel;
    vec2[36] gaussianKernel;
} DoF;

// Gaussian kernel
const float kernel[9] = float[](0.027, 0.065, 0.121, 0.194, 0.227, 0.194, 0.121, 0.065, 0.027);

const float blurPower = 40.0;

const vec3 foregroundColor = vec3(1.0, 0.0, 0.0);//red

const vec3 backgroundColor = vec3(0.0, 1.0, 0.0);//green

const vec3 centerColor = vec3(0.0, 0.0, 0.0);//black

void main() {
    if (DoF.enabled == 1)
    {
        if (DoF.mode == 1) {//near field
            fs_out = texture(nearTexture, texCoord);
        } else if (DoF.mode == 2) {//center field
            fs_out = texture(centerTexture, texCoord);
        } else if (DoF.mode == 3) {//far field
            fs_out = texture(farTexture, texCoord);
        } else if (DoF.mode == 0) {//blur
            //apply gaussian blur to near field
            vec4 nearBlur = vec4(0.0);
            //apply a box blur with 10x10 kernel
            for (int i = 0; i < 9; i++)
            {
                for (int j = 0; j < 9; j++)
                {
                    nearBlur += texture(nearTexture, texCoord + vec2(i - 4, j - 4) / 512.0) * kernel[i] * kernel[j];
                }
            }
//            nearBlur = pow(nearBlur, vec4(blurPower));
            
            vec4 farBlur = vec4(0.0);
            //apply a box blur with 10x10 kernel
            for (int i = 0; i < 9; i++)
            {
                for (int j = 0; j < 9; j++)
                {
                    farBlur += texture(farTexture, texCoord + vec2(i - 4, j - 4) / 512.0) * kernel[i] * kernel[j];
                }
            }
            
            fs_out = nearBlur + farBlur + texture(centerTexture, texCoord);
        }
    } else {
        fs_out = texture(ssaoTexture, texCoord);
    }
}