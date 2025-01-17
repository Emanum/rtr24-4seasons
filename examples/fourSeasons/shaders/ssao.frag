#version 450

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
        vec3 normal = normalize(texture(gNormal, texCoord).rgb * 2.0 - 1.0);

        ivec2 screenDim = textureSize(screenTexture, 0);
        ivec2 noiseDim = textureSize(ssaoNoise, 0);
        const vec2 noiseUV = vec2(float(screenDim.x) / float(noiseDim.x), float(screenDim.y) / float(noiseDim.y));
        vec3 rvec = texture(ssaoNoise, noiseUV).rgb * 2.0 - 1.0;

        //TBN matrix
        vec3 tangent = normalize(rvec - normal * dot(rvec, normal));
        vec3 bitangent = cross(normal, tangent);
        mat3 TBN = mat3(tangent, bitangent, normal);

        float occlusion = 0.0f;
        //const float bias = 0.025f;

        for (int i = 0; i < NUM_SAMPLES; i++) {
            vec3 samplePos = TBN * kernel.samples[i].xyz;
            samplePos = fragPos + samplePos * RADIUS;

            vec4 offset = vec4(samplePos, 1.0f);
            offset = vp.mProjectionMatrix * offset;
            offset.xyz /= offset.w;
            offset.xyz = offset.xyz * 0.5f + 0.5f;

            float sampleDepth = texture(gPosition, offset.xy).w;
            float rangeCheck = abs(fragPos.z - sampleDepth) < RADIUS ? 1.0 : 0.0;
            occlusion += (sampleDepth <= samplePos.z ? 1.0f : 0.0f) * rangeCheck;
        }

        occlusion = 1.0 - (occlusion / float(NUM_SAMPLES));
        //fs_out = vec4(vec3(occlusion), 1.0f);
        fs_out = vec4(rvec, 1.0);

        /*if (SSAO.blur == 1) {
            fs_out = vec4(0.0, 1.0, 0.0, 1.0);
        }*/
    } else {
        fs_out = texture(ssaoNoise, texCoord);
    }
}