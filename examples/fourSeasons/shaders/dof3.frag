﻿#version 450

layout (location = 0) in vec2 texCoord;
layout (location = 0) out vec4 fs_out;

layout (set = 0, binding = 0) uniform sampler2D ssaoTexture;
layout (set = 0, binding = 1) uniform sampler2D nearTexture;//does not contain color value instead only white for near field and black for not near field
layout (set = 0, binding = 2) uniform sampler2D centerTexture;//already contains color value
layout (set = 0, binding = 3) uniform sampler2D farTexture;//already contains color value
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

layout(set = 1, binding = 0) buffer StorageBufferObjectGaussian
{
    vec4 gaussianKernel[49];
} gaussianKernel;

layout(set = 1, binding = 1) buffer StorageBufferObjectBokeh
{
    vec4 bokehKernel[48];
} bokehKernel;


void main() {
    if (DoF.enabled == 1){
        if (DoF.mode == 1) {//near field
            vec4 og_value = texture(ssaoTexture, texCoord);
            vec4 near_value = texture(nearTexture, texCoord); //(near -> (1,1,1) not near -> (0,0,0)) so we can use it as a mask
            fs_out = og_value * near_value;
        } else if (DoF.mode == 2) {//center field
            fs_out = texture(centerTexture, texCoord);
        } else if (DoF.mode == 3) {//far field
            fs_out = texture(farTexture, texCoord);
        } else if (DoF.mode == 0) {//blur
            //apply gaussian blur to near field
            vec4 nearBlur = vec4(0.0);
            
            ivec2 screenDimensions = textureSize(nearTexture,0);//show all have the same size
            //integer of the gaussian kernel (x,y) are the coordinates of the kernel, z is the weight
            for (int i = 0; i < 49; i++){
              vec2 offset = vec2( gaussianKernel.gaussianKernel[i].x / screenDimensions.x,gaussianKernel.gaussianKernel[i].y / screenDimensions.y);
              vec4 amount = texture(nearTexture, texCoord + offset) * gaussianKernel.gaussianKernel[i].z;
              vec4 og_value = texture(ssaoTexture, texCoord + offset);
              nearBlur += amount * og_value;
            }
                    
            vec4 farBlur = vec4(0.0);
            for (int i = 0; i < 48; i++){
                vec2 offset = vec2(bokehKernel.bokehKernel[i].x / screenDimensions.x,bokehKernel.bokehKernel[i].y / screenDimensions.y);
                farBlur += texture(farTexture, texCoord + offset) * bokehKernel.bokehKernel[i].z;
            }
            farBlur = farBlur / 48.0;
            
            vec4 centerValue = texture(centerTexture, texCoord);
            
            //now that we bleeded the near field we have the situation that there are pixels that are near and far
            //this leads to a problem where when blending them we adding power to the image (so it gets brighter)
            //to fix this we need to subtract the near field from the far field
            //the result is that we only have the far field left
            //this is the same as the near field being black and the far field being white
            //so we can use the near field as a mask
            
            
            
            vec4 blend = nearBlur + farBlur + centerValue;
            //cap blend to 1
            fs_out = min(blend, vec4(1.0));
        }
    } else {
        fs_out = texture(ssaoTexture, texCoord);
    }
}