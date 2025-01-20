#version 460

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fs_out;

layout(constant_id = 0) const int NUM_LIGHTS = 10;
layout(constant_id = 1) const float CONSTANT = 1.0;
layout(constant_id = 2) const float LINEAR = 0.22;
layout(constant_id = 3) const float QUADRATIC = 0.2;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D gPositionWS;
layout(set = 0, binding = 2) uniform sampler2D gNormalWS;
layout(set = 0, binding = 3) uniform sampler2D gAlbedo;
layout(set = 0, binding = 4) uniform sampler2D depthTexture;

layout(set = 0, binding = 5) uniform uniformSSAO
{
    int enabled;
    int blur;
    int illumination;
} SSAO;

layout(set = 0, binding = 6) uniform Camera {
    vec3 position;
} camera;

layout(set = 0, binding = 7) uniform Lighting {
    vec3 sunColor;
} lighting;

layout(set = 0, binding = 8) uniform LightPositions {
    vec3 data[NUM_LIGHTS];
} lightPositions;

void main() {
    if (SSAO.illumination == 1) {
        vec3 fragPos = texture(gPositionWS, texCoord).rgb;
        vec3 normal = texture(gNormalWS, texCoord).rgb;
        vec3 diffuse = texture(gAlbedo, texCoord).rgb;
        float ao = texture(screenTexture, texCoord).r;
        
        vec4 depth = texture(depthTexture, texCoord);
        
        if (SSAO.enabled != 1) {
            ao = 1.0f;
        }

        vec3 viewDir = normalize(camera.position - fragPos);
        vec3 sunDir = vec3(0.5, 0.7, 1.0);

        vec3 light = max(dot(normal, sunDir), 0.0) * diffuse * lighting.sunColor;
        for (int i = 0; i < NUM_LIGHTS; i++) {
            vec3 lightDir = lightPositions.data[i] - fragPos;
            vec3 lightDirN = normalize(lightDir);
            float d = length(lightDir);
            float att = 1.0 / (CONSTANT + LINEAR * d + QUADRATIC * d * d);
            vec3 diffuseC = max(dot(normal, lightDirN), 0.0) * diffuse * vec3(1.0, 0.7, 0.0) * att;
            light += diffuseC;
        }

        vec4 erg = vec4(light * ao, 1.0);
        //skybox has high depth value; if depth is high, use skybox color (diffuse) instead of erg)
        if(depth.r < 0.9999) {
            fs_out = erg;
        }else{
            fs_out = vec4(diffuse, 1.0);
        }

    }
    else {
        fs_out = vec4(texture(screenTexture, texCoord).rrr, 1.0);
        //fs_out = texture(gNormalWS, texCoord);
    }
}