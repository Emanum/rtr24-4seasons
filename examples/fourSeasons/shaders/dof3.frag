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
} DoF;

const float kernel2[9] = float[](0.027, 0.065, 0.121, 0.194, 0.227, 0.194, 0.121, 0.065, 0.027);


layout(set = 1, binding = 0) buffer UniformBufferObject
{
    vec4 gaussianKernel[49];
    vec4 bokehKernel[48];
} kernel;


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

           for (int i = 0; i < 9; i++)
          {
              for (int j = 0; j < 9; j++)
              {
                  nearBlur += texture(nearTexture, texCoord + vec2(i - 4, j - 4) / 512.0) * kernel2[i] * kernel2[j];
              }
          }
           //            nearBlur = pow(nearBlur, vec4(blurPower));

           vec4 farBlur = vec4(0.0);
           //apply a box blur with 10x10 kernel
           for (int i = 0; i < 9; i++)
              {
                  for (int j = 0; j < 9; j++)
                  {
                      farBlur += texture(farTexture, texCoord + vec2(i - 4, j - 4) / 512.0) * kernel2[i] * kernel2[j];
                  }
              }

           fs_out = nearBlur + farBlur + texture(centerTexture, texCoord);            
            
//            ivec2 screenDimensions = textureSize(nearTexture,0);//show all have the same size
//            //interate of the gaussian kernel (x,y) are the coordinates of the kernel, z is the weight
//            for (int i = 0; i < 49; i++)
//            {
//                vec2 offset = vec2( kernel.gaussianKernel[i].x / screenDimensions.x,kernel.gaussianKernel[i].y / screenDimensions.y);
//                
//                nearBlur += texture(nearTexture, texCoord + offset) * kernel.gaussianKernel[i].z ;
//            }
////            nearBlur = clamp(nearBlur, 0.0, 1.0);
//            fs_out = nearBlur;
        }
    } else {
        fs_out = texture(ssaoTexture, texCoord);
    }
}