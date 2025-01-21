#version 460

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fs_out;

layout(constant_id = 0) const int NUM_LIGHTS = 1000;
layout(constant_id = 2) const float LINEAR = 0.5;
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
    int deferred;
} lighting;

layout(set = 0, binding = 8) uniform LightPositions {
    vec3 data[NUM_LIGHTS];
} lightPositions;

void main() {
    if (SSAO.illumination == 1) {
        if (lighting.deferred == 1) {
            vec3 fragPos = texture(gPositionWS, texCoord).rgb;
            vec3 normal = texture(gNormalWS, texCoord).rgb;
            vec3 albedo = texture(gAlbedo, texCoord).rgb;
            float ao = texture(screenTexture, texCoord).r;
        
            vec4 depth = texture(depthTexture, texCoord);
        
            if (SSAO.enabled != 1) {
                ao = 1.0f;
            }

            vec3 sunDir = vec3(0.5, 0.7, 1.0);
            //vec3 light = max(dot(normal, sunDir), 0.0) * albedo * lighting.sunColor;
            vec3 light = vec3(0.0);

            for (int i = 0; i < NUM_LIGHTS; i++) {
                vec3 lightDir = lightPositions.data[i] - fragPos;
                vec3 lightDirN = normalize(lightDir);
                float d = length(lightDir);
                float att = 1.0 / (1.0 + LINEAR * d + QUADRATIC * d * d);
                vec3 diffuseC = max(dot(normal, lightDirN), 0.0) * albedo * att;// * vec3(1.0, 0.7, 0.0);
                light += 0.1 / NUM_LIGHTS * ao * att;
                light += diffuseC * 0.1;
            }

            vec4 erg = vec4(light, 1.0);
            //skybox has high depth value; if depth is high, use skybox color (albedo) instead of erg)
            if(depth.r < 0.9999) {
                fs_out = erg;
            }else{
                fs_out = vec4(albedo, 1.0);
            }
        }
        else {
            fs_out = texture(gAlbedo, texCoord);
        }
    }
    else {
        fs_out = vec4(texture(screenTexture, texCoord).rrr, 1.0);
        //fs_out = texture(gNormalWS, texCoord);
    }
}