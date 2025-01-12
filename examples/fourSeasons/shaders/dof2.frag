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
    float nearPlane;
    float farPlane;
    vec2[49] bokehKernel;
    vec3[49] gaussianKernel;
} DoF;

void main() {
    float depth = texture(depthTexture, texCoord).r;
    float lowerBoundCenter = max(DoF.focus - DoF.range, 0);
    float upperBoundCenter = min(DoF.focus + DoF.range, 1);
    float lowerBoundTotalOoF = max(DoF.focus - DoF.range - DoF.distOutOfFocus, 0);
    float upperBoundTotalOoF = min(DoF.focus + DoF.range + DoF.distOutOfFocus,1);

    float far = 1.0f;
    float notFar = 0.0f;

    float depthVis = 0.0;
    
    if(depth > upperBoundTotalOoF){
        depthVis = far;
    }else if(depth <= upperBoundTotalOoF && depth > upperBoundCenter){
        float percentInFar = (depth - upperBoundCenter) / (upperBoundTotalOoF - upperBoundCenter);
        depthVis = mix(notFar, far, percentInFar);
    }else{
        depthVis = notFar;
    }
    depthVis = max(0.0, depthVis);
    depthVis = min(1.0, depthVis);
    
    fs_out = mix(vec4(0, 0, 0, 1), texture(screenTexture, texCoord), depthVis);
}