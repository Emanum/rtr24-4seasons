﻿#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fs_out;

layout(constant_id = 0) const int NUM_SAMPLES = 64;
layout(constant_id = 1) const float RADIUS = 0.5;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;
layout(set = 0, binding = 2) uniform sampler2D gPosition;
layout(set = 0, binding = 3) uniform sampler2D gNormal;
layout(set = 0, binding = 4) uniform sampler2D ssaoNoise;


layout(set = 0, binding = 5) uniform uniformSSAO
{
    int enabled;
    int blur;
    int illumination;
} SSAO;

layout(set = 0, binding = 6) uniform ssaoKernel {
    vec4 samples[NUM_SAMPLES];
} kernel;

layout(set = 0, binding = 7) uniform VPMatrices
{
	mat4 mViewMatrix;
	mat4 mProjectionMatrix;
} vp; 




void main() {
    if (SSAO.enabled == 1) {
        vec3 fragPos = texture(gPosition, texCoord).rgb;
        vec3 normal = normalize(texture(gNormal, texCoord).rgb);

        ivec2 screenDim = textureSize(gPosition, 0);
        ivec2 noiseDim = textureSize(ssaoNoise, 0);
        const vec2 noiseUV = vec2(float(screenDim.x) / noiseDim.x, float(screenDim.y) / noiseDim.y) * texCoord;
        vec3 rvec = texture(ssaoNoise, noiseUV).rgb;

        //TBN matrix
        vec3 tangent = normalize(rvec - normal * dot(rvec, normal));
        vec3 bitangent = cross(tangent, normal);
        mat3 TBN = mat3(tangent, bitangent, normal);

        float occlusion = 0.0;
        const float bias = 0.025;

        for (int i = 0; i < NUM_SAMPLES; i++) {
            vec3 samplePos = TBN * kernel.samples[i].xyz;
            samplePos = fragPos + samplePos * RADIUS;

            vec4 offset = vec4(samplePos, 1.0f);
            offset = vp.mProjectionMatrix * offset;
            offset.xyz /= offset.w;
            offset.xyz = offset.xyz * 0.5f + 0.5f;

            float sampleDepth = texture(gPosition, offset.xy).z;
            float rangeCheck = smoothstep(0.0, 1.0, RADIUS / abs(fragPos.z - sampleDepth));
            occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
        }

        occlusion = 1.0 - (occlusion / NUM_SAMPLES);
        fs_out = vec4(occlusion, 0.0, 0.0, 1.0);
    } else {
        fs_out = texture(screenTexture, texCoord);
    }
}