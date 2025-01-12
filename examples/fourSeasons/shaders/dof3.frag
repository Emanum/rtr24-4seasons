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
        //In theory we should linearize the depth value becase the depth value is not linear 

        float mdistOutOfFocus = DoF.distOutOfFocus;
        float mrange = DoF.range;

        float lowerBoundCenter = max(DoF.focus - mrange,0);
        float upperBoundCenter = min(DoF.focus + mrange,1);
        float lowerBoundTotalOoF = max(DoF.focus - mrange - mdistOutOfFocus,0);
        float upperBoundTotalOoF = min(DoF.focus + mrange + mdistOutOfFocus,1);

        vec3 depthVis = vec3(0.0);
        //  TotalOoF<lowerBoundCenter>Center<upperBoundCenter>TotalOoF
        if(depth <= lowerBoundCenter)
        {
            //mix between foreground and center
            float mixerg = ((lowerBoundTotalOoF-depth) / (lowerBoundTotalOoF - lowerBoundCenter));
            depthVis = mix(foregroundColor, centerColor, mixerg);
        } else if(depth > lowerBoundCenter && depth < upperBoundCenter)
        {
            //center -> complete in focus
            depthVis = centerColor;
        } else
        {
            //mix between center and background
            float test = (depth - upperBoundCenter) / (upperBoundCenter - upperBoundTotalOoF);
            depthVis = mix(centerColor, backgroundColor, (depth - upperBoundCenter) / (upperBoundTotalOoF - upperBoundCenter));
        }


        //take the highest of the r or g value from the outOfFocus vector
        float outOfFocusAmount = max(depthVis.r, depthVis.g);
        
        //now we have the outOfFocusAmount we can blur the screen texture using gaussian blur
        //0-> no blur, 1-> full blur
        float blurAmount = pow(outOfFocusAmount, blurPower);
        
        vec4 ogColor = texture(ssaoTexture, texCoord);
        vec4 blurColor = vec4(0.0);
        //we sample on each bokeh kernel point; sum it up and then divide it by the sum of the kernel
        //bokeh kernel has 49 points
        //each sample point is a vec2 with x and y value offset in pixel
        for (int i = 0; i < 49; i++)
        {
            blurColor += texture(ssaoTexture, texCoord + DoF.bokehKernel[i]);
        }
        blurColor /= 49.0;
        
        
        if (DoF.mode == 0)
        {
            //depth
            fs_out = vec4(depthVis, 1.0);
        }else if(DoF.mode == 1 || DoF.mode == 2)
        {
            //bokeh
//            fs_out = mix(ogColor, blurColor, blurAmount);
            fs_out = mix(ogColor, blurColor, outOfFocusAmount);
        }

    }else{
        fs_out = texture(ssaoTexture, texCoord);
    }

        
}