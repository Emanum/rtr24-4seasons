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
} DoF;

const vec3 nearColor = vec3(1.0, 0.0, 0.0);//red

const vec3 centerColor = vec3(0.0, 0.0, 0.0);//black

void main() {
    float depth = texture(depthTexture, texCoord).r;
    float lowerBoundCenter = max(DoF.focus - DoF.range, 0);
    float upperBoundCenter = min(DoF.focus + DoF.range, 1);
    float lowerBoundTotalOoF = max(DoF.focus - DoF.range - DoF.distOutOfFocus, 0);
    float upperBoundTotalOoF = min(DoF.focus + DoF.range + DoF.distOutOfFocus,1);

    vec3 depthVis = vec3(0.0);
    vec4 notCenter = vec4(0, 0, 0, 1);
    if (lowerBoundTotalOoF <= depth && depth <= upperBoundTotalOoF)
    {
        if (depth >= upperBoundCenter)
        {
            //mix between center and background
            depthVis = mix(centerColor, nearColor, (depth - upperBoundCenter) / (upperBoundTotalOoF - upperBoundCenter));
        } else {
            //mix between foreground and center
            depthVis = mix(nearColor, centerColor, (depth - lowerBoundTotalOoF) / (lowerBoundCenter - lowerBoundTotalOoF));
        }
        notCenter = texture(screenTexture, texCoord);
    }
    else {
        depthVis = centerColor;
    }
    float centerFieldAmount =  depthVis.r;
    fs_out = mix(vec4(0, 0, 0, 1), notCenter, centerFieldAmount);
}