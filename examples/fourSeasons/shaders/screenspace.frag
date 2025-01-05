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

const vec3 foregroundColor = vec3(1.0, 0.0, 0.0);//red

const vec3 backgroundColor = vec3(0.0, 1.0, 0.0);//green

const vec3 centerColor = vec3(0.0, 0.0, 0.0);//black

void main() {
   //if enabled -> show depth buffer else show screen texture
    if(DoF.enabled == 1)
    {
        // for demo purpose we visualize the depth buffer
        // we split the depth value into 3 groups
        // 1. foreground -> between 0 and uniformDoF.focus - uniformDoF.range
        // 2. focus -> between (uniformDoF.focus - uniformDoF.range) and (uniformDoF.focus + uniformDoF.range)
        // 3. background -> between (uniformDoF.focus + uniformDoF.range) and 1
        //But the jump between the groups should be smooth so we use the distOutOfFocus to make the transition smooth
        
        float depth = texture(depthTexture, texCoord).r;
        vec3 depthColor = vec3(0.0);

        float mdistOutOfFocus = DoF.distOutOfFocus;
        float mrange = DoF.range;
        
        float lowerBoundCenter = DoF.focus - mrange;
        float upperBoundCenter = DoF.focus + mrange;
        float lowerBoundTotalOoF = DoF.focus - mrange - mdistOutOfFocus;
        float upperBoundTotalOoF = DoF.focus + mrange + mdistOutOfFocus;
        
        if(depth < lowerBoundTotalOoF){
            //total out of focus -> foreground
            depthColor = foregroundColor;
        } else if(depth < lowerBoundCenter)
        {
            //mix between foreground and center
            float mixerg = ((lowerBoundTotalOoF-depth) / (lowerBoundTotalOoF - lowerBoundCenter));
            depthColor = mix(foregroundColor, centerColor, mixerg);
        } else if(depth > lowerBoundCenter && depth < upperBoundCenter)
        {
            //center -> complete in focus
            depthColor = centerColor;
        } else if(depth < upperBoundTotalOoF)
        {
            //mix between center and background
            float test = (depth - upperBoundCenter) / (upperBoundCenter - upperBoundTotalOoF);
            depthColor = mix(centerColor, backgroundColor, (depth - upperBoundCenter) / (upperBoundTotalOoF - upperBoundCenter));
        } else
        {
            //total out of focus -> background
            depthColor = backgroundColor;
        }
        
        fs_out = vec4(depthColor, 1.0);
        
//        vec2 texelSize = 1.0 / textureSize(screenTexture, 0);
//        vec3 color = vec3(0.0);
//
//        // Apply Gaussian blur vertically
//        for (int i = -4; i <= 4; ++i) {
//            color += texture(screenTexture, texCoord + vec2(0.0, texelSize.y * float(i))).rgb * kernel[i + 4];
//        }
//        
//
//        // Apply Gaussian blur horizontally
//        vec3 blurredColor = vec3(0.0);
//        for (int i = -4; i <= 4; ++i) {
//            blurredColor += color * kernel[i + 4];
//        }
//
//        fs_out = vec4(blurredColor, 1.0);
        
    }else{
        fs_out = texture(screenTexture, texCoord);
    }

        
}