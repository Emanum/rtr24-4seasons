#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(set = 0, binding = 2) uniform uniformDoF
{
    int enabled;
    int mode;//0-> depth, 1-> gaussian, 2-> bokeh
    float focus;
    float range;
    float distOutOfFocus;
} DoF;

// Gaussian kernel
const float kernel[9] = float[](0.027, 0.065, 0.121, 0.194, 0.227, 0.194, 0.121, 0.065, 0.027);

const float blurPower = 40.0;

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

        float mdistOutOfFocus = DoF.distOutOfFocus;
        float mrange = DoF.range;

        float lowerBoundCenter = max(DoF.focus - mrange,0);
        float upperBoundCenter = min(DoF.focus + mrange,1);
        float lowerBoundTotalOoF = max(DoF.focus - mrange - mdistOutOfFocus,0);
        float upperBoundTotalOoF = min(DoF.focus + mrange + mdistOutOfFocus,1);

        vec3 depthVis = vec3(0.0);
        if(depth <= lowerBoundCenter)
        {
            //mix between foreground and center
            float mixerg = ((lowerBoundTotalOoF-depth) / (lowerBoundTotalOoF - lowerBoundCenter));
            depthVis = mix(foregroundColor, centerColor, mixerg);
        } else if(depth > lowerBoundCenter && depth < upperBoundCenter)
        {
            //center -> complete in focus
            depthVis = centerColor;
        } else if(depth <= upperBoundTotalOoF)
        {
            //mix between center and background
            float test = (depth - upperBoundCenter) / (upperBoundCenter - upperBoundTotalOoF);
            depthVis = mix(centerColor, backgroundColor, (depth - upperBoundCenter) / (upperBoundTotalOoF - upperBoundCenter));
        }


        //take the highest of the r or g value from the outOfFocus vector
        float outOfFocusAmount = max(depthVis.r, depthVis.g);
        
        //now we have the outOfFocusAmount we can blur the screen texture using gaussian blur
        //0-> no blur, 1-> full blur
        float blurAmount = pow(outOfFocusAmount, blurPower) -1.0;
        
        vec4 ogColor = texture(screenTexture, texCoord);
        vec4 blurColor = vec4(0.0);
        for(int i = 0; i < 9; i++)
        {
            vec2 offset = vec2(float(i%3) - 1.0, float(i/3) - 1.0);
            blurColor += texture(screenTexture, texCoord + offset / textureSize(screenTexture, 0)) * kernel[i];
        }
        
        if (DoF.mode == 0)
        {
            //depth
            fs_out = vec4(depthVis, 1.0);
        }else if(DoF.mode == 1 || DoF.mode == 2)
        {
            //gaussian
            fs_out = mix(ogColor, blurColor, blurAmount);
        }

    }else{
        fs_out = texture(screenTexture, texCoord);
    }

        
}