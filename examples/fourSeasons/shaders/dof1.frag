#version 450

layout (location = 0) in vec2 texCoord;
layout (location = 0) out vec4 fs_out;

layout (set = 0, binding = 0) uniform sampler2D screenTexture;
layout (set = 0, binding = 1) uniform sampler2D depthTexture;

layout (set = 0, binding = 2) uniform uniformDoF
{
    int enabled;
    int mode;//0-> depth, 1-> gaussian, 2-> bokeh
    float focus;
    float range;
    float distOutOfFocus;
    float nearPlane;
    float farPlane;
    vec2[49] bokehKernel;
    vec3[49] gaussianKernel;
} DoF;

vec4 near = vec4(1,0,0,1);
vec4 center = vec4(0,1,0,1);
vec4 far = vec4(0,0,1,1);

void main() {
    float depth = texture(depthTexture, texCoord).r;
    float lowerBoundCenter = max(DoF.focus - DoF.range, 0);
    float upperBoundCenter = min(DoF.focus + DoF.range, 1);
    float lowerBoundTotalOoF = max(DoF.focus - DoF.range - DoF.distOutOfFocus, 0);
    float upperBoundTotalOoF = min(DoF.focus + DoF.range + DoF.distOutOfFocus,1);

    vec4 depthVis = vec4(0,0,0,1);

    if(depth < lowerBoundTotalOoF){
        depthVis = near;
    }else if(depth >= lowerBoundTotalOoF && depth < lowerBoundCenter){
        float percentInNear = (depth - lowerBoundTotalOoF) / (lowerBoundCenter - lowerBoundTotalOoF);
        depthVis = mix(near, center, percentInNear);
    }else if (depth >= lowerBoundCenter && depth < upperBoundCenter){
        depthVis = center;
    } else if(depth >= upperBoundCenter && depth < upperBoundTotalOoF){
        float percentInFar = (depth - upperBoundCenter) / (upperBoundTotalOoF - upperBoundCenter);
        depthVis = mix(center, far, percentInFar);
    }
    else{
        depthVis = far;
    }
    
   fs_out = depthVis;
}