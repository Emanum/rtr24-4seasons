#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D textures[];

struct MaterialGpuData
{
	vec4 mDiffuseReflectivity;
	vec4 mAmbientReflectivity;
	vec4 mSpecularReflectivity;
	vec4 mEmissiveColor;
	vec4 mTransparentColor;
	vec4 mReflectiveColor;
	vec4 mAlbedo;

	float mOpacity;
	float mBumpScaling;
	float mShininess;
	float mShininessStrength;
	
	float mRefractionIndex;
	float mReflectivity;
	float mMetallic;
	float mSmoothness;
	
	float mSheen;
	float mThickness;
	float mRoughness;
	float mAnisotropy;
	
	vec4 mAnisotropyRotation;
	vec4 mCustomData;
	
	int mDiffuseTexIndex;
	int mSpecularTexIndex;
	int mAmbientTexIndex;
	int mEmissiveTexIndex;
	int mHeightTexIndex;
	int mNormalsTexIndex;
	int mShininessTexIndex;
	int mOpacityTexIndex;
	int mDisplacementTexIndex;
	int mReflectionTexIndex;
	int mLightmapTexIndex;
	int mExtraTexIndex;
	
	vec4 mDiffuseTexOffsetTiling;
	vec4 mSpecularTexOffsetTiling;
	vec4 mAmbientTexOffsetTiling;
	vec4 mEmissiveTexOffsetTiling;
	vec4 mHeightTexOffsetTiling;
	vec4 mNormalsTexOffsetTiling;
	vec4 mShininessTexOffsetTiling;
	vec4 mOpacityTexOffsetTiling;
	vec4 mDisplacementTexOffsetTiling;
	vec4 mReflectionTexOffsetTiling;
	vec4 mLightmapTexOffsetTiling;
	vec4 mExtraTexOffsetTiling;
};

layout(set = 1, binding = 0) buffer Material 
{
	MaterialGpuData materials[];
} matSsbo;

layout (location = 0) in vec3 positionWS;
layout (location = 1) in vec3 normalWS;
layout (location = 2) in vec2 texCoord;
layout (location = 3) flat in int materialIndex;
layout (location = 4) in float fragDepth;
layout (location = 5) in vec3 pos;
layout (location = 6) in vec3 normal;

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec4 gPosition;
layout (location = 2) out vec4 gNormal;
layout (location = 3) out vec4 gPositionWS;
layout (location = 4) out vec4 gNormalWS;



layout(constant_id = 0) const int NUM_LIGHTS = 1000;
layout(constant_id = 1) const float LINEAR = 0.5;
layout(constant_id = 2) const float QUADRATIC = 0.2;

layout(set = 2, binding = 0) uniform Lighting {
    vec3 sunColor;
    int deferred;
} lighting;

layout(set = 2, binding = 1) uniform LightPositions {
    vec3 data[NUM_LIGHTS];
} lightPositions;


float near = 0.3f;  
float far = 1000.0f;

// https://stackoverflow.com/questions/51108596/linearize-depth
float linearizeDepth(float depth) {
    return (near * far) / (far + depth * (near - far));
}


void main() 
{
	int matIndex = materialIndex;

	int diffuseTexIndex = matSsbo.materials[matIndex].mDiffuseTexIndex;
    vec3 color = texture(textures[diffuseTexIndex], texCoord).rgb;
	vec3 diffuse = matSsbo.materials[matIndex].mDiffuseReflectivity.rgb;

	vec3 albedo = color * diffuse;
	
	if (lighting.deferred == 0) {
		vec3 sunDir = vec3(0.5, 0.7, 1.0);
		//vec3 light = max(dot(normalize(normalWS), sunDir), 0.0) * albedo * lighting.sunColor;
		vec3 light = vec3(0.0);

		float ambient = 0.1 / NUM_LIGHTS;
		for (int i = 0; i < NUM_LIGHTS; i++) {
			vec3 lightDir = lightPositions.data[i] - positionWS;
            vec3 lightDirN = normalize(lightDir);
            float d = length(lightDir);
            float att = 1.0 / (1.0 + LINEAR * d + QUADRATIC * d * d);
			vec3 diffuseC = max(0.0, dot(normalize(normalWS), lightDirN)) * albedo * att;// * vec3(1.0, 0.7, 0.0);
			vec3 ambientC = vec3(ambient) * att;
			light += diffuseC * 0.1 + ambientC;
		}

		albedo = light;
	}
	
	gAlbedo = vec4(albedo, 1.0);
	gPosition = vec4(pos, linearizeDepth(fragDepth));
	gPositionWS = vec4(positionWS, 1.0);
	gNormal = vec4(normalize(normal), 1.0);
	gNormalWS = vec4(normalize(normalWS), 1.0);
}