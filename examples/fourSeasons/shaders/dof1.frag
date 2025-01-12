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
    //    float upperBoundTotalOoF = min(DoF.focus + DoF.range + DoF.distOutOfFocus,1);

    vec3 depthVis = vec3(0.0);
    if (depth <= lowerBoundCenter)
    {
        //mix between foreground and center
        float mixerg = ((lowerBoundTotalOoF - depth) / (lowerBoundTotalOoF - lowerBoundCenter));
        depthVis = mix(nearColor, centerColor, mixerg);
    } else {
        depthVis = centerColor;
    }
    float nearFieldAmount = depthVis.r;
    vec4 screenColor = texture(screenTexture, texCoord);
    fs_out = mix(vec4(0, 0, 0, 1), screenColor, nearFieldAmount);
}