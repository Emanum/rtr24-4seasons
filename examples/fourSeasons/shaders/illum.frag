#version 460

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fs_out;

layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D gPositionWS;
layout(set = 0, binding = 2) uniform sampler2D gNormal;
layout(set = 0, binding = 3) uniform sampler2D gAlbedo;

layout(set = 0, binding = 4) uniform uniformSSAO
{
    int enabled;
    int blur;
    int illumination;
} SSAO;

layout(set = 0, binding = 5) uniform Camera {
    vec3 position;
} camera;

void main() {
    if (SSAO.illumination == 1) {
        vec3 fragPos = texture(gPositionWS, texCoord).rgb;
        vec3 normal = texture(gNormal, texCoord).rgb;
        vec3 diffuse = texture(gAlbedo, texCoord).rgb;
        float ao = texture(screenTexture, texCoord).r;

        vec3 ambientC = vec3(0.1 * diffuse * ao);
        vec3 color = ambientC;
        vec3 viewDir = normalize(-fragPos);

        const vec3 lightColor = vec3(1.0);

        vec3 lightDir = vec3(0.5, 0.7, 1.0);
        vec3 diffuseC = max(dot(normal, lightDir), 0.0) * diffuse * lightColor;

        //vec3 halfwayDir = normalize(lightDir + viewDir);
        //float specular = pow(max(dot(normal, halfwayDir), 0.0), 8.0);
        //vec3 specularC = lightColor * specular;

        color += diffuseC;
        fs_out = vec4(color, 1.0);
    }
    else {
        fs_out = vec4(texture(screenTexture, texCoord).rrr, 1.0);
    }
}