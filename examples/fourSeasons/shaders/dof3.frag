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

//inverse of the lottes tone mapping
//convert from SDR to HDR
vec4 inverse_lottes(vec4 y) {
    vec3 x = y.rgb;
     // Parameters for the Lottes tone mapping
    const float A = 0.22;  // Shoulder strength
    const float B = 0.30;  // Linear strength
    const float C = 0.10;  // Linear angle
    const float D = 0.20;  // Toe strength
    const float E = 0.01;  // Toe numerator
    const float F = 0.30;  // Linear white point scale

    // Normalized luminance term
    vec3 W = vec3(11.2); // Luminance of the white point
    vec3 numerator = x * (A * W + C * B * W + D * E);
    vec3 denominator = F * W - x * (A + C * B + D);

    // Reconstruct HDR values
    vec3 hdr = numerator / max(denominator, 1e-5); // Avoid division by zero
    vec3 erg_x = max(hdr, vec3(0.0)); // Ensure non-negative HDR values
    return vec4(erg_x, y.a);
}


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
            //apply gaussian blur entire image
            vec4 nearBlur = vec4(0.0);
            
            ivec2 screenDimensions = textureSize(nearTexture,0);//should all have the same size
            //integer of the gaussian kernel (x,y) are the coordinates of the kernel, z is the weight
            for (int i = 0; i < 49; i++){
              vec2 offset = vec2(gaussianKernel.gaussianKernel[i].x / screenDimensions.x,gaussianKernel.gaussianKernel[i].y / screenDimensions.y);;
              vec4 og_value = inverse_lottes(texture(ssaoTexture, texCoord + offset));
              nearBlur += og_value * gaussianKernel.gaussianKernel[i].z;
            }
                    
            vec4 farBlur = vec4(0.0);
            for (int i = 0; i < 48; i++){
                vec2 offset = vec2(bokehKernel.bokehKernel[i].x / screenDimensions.x,bokehKernel.bokehKernel[i].y / screenDimensions.y);
                farBlur += inverse_lottes(texture(ssaoTexture, texCoord + offset)) * bokehKernel.bokehKernel[i].z;
            }
            farBlur = farBlur / 48.0;
            
            vec4 centerValue = texture(centerTexture, texCoord);
 
            float near = texture(nearTexture, texCoord).r;
            float far = texture(nearTexture, texCoord).b;
            float center = texture(nearTexture, texCoord).g;
            center = 1 - near - far;
            far = 1 - near - center;
            nearBlur = nearBlur * near;
            farBlur = farBlur * far;
            vec4 centerBlur = texture(ssaoTexture, texCoord) * center;
                   
            
            vec4 blend = nearBlur + farBlur + centerBlur;
            //cap blend to 1
            fs_out = blend;
        }
    } else {
        fs_out = texture(ssaoTexture, texCoord);
    }
}