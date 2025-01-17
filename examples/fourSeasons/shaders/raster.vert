#version 460

layout (location = 0) in vec3 inPosition; 
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
	mat4 mModelMatrix;
	int mMaterialIndex;
} pushConstants;

layout(set = 0, binding = 1) uniform CameraTransform
{
	mat4 mViewProjMatrix;
} ubo;

layout(set = 0, binding = 2) uniform VPMatrices
{
	mat4 mViewMatrix;
	mat4 mProjectionMatrix;
} vp;

layout (location = 0) out vec3 positionWS;
layout (location = 1) out vec3 normalWS;
layout (location = 2) out vec2 texCoord;
layout (location = 3) flat out int materialIndex;
layout (location = 4) out float fragDepth;
layout (location = 5) out vec3 pos;
layout (location = 6) out vec3 normal;

void main() {
	vec4 pos4 = vec4(inPosition.xyz, 1.0);
	vec4 posWS = pushConstants.mModelMatrix * pos4;
	positionWS = posWS.xyz;

    texCoord = inTexCoord;

	normalWS = mat3(pushConstants.mModelMatrix) * inNormal;
	materialIndex = pushConstants.mMaterialIndex;

    gl_Position = ubo.mViewProjMatrix * posWS;
	fragDepth = gl_Position.z / gl_Position.w; // Pass the depth value

	// Position for g-buffer
	pos = vec3(vp.mViewMatrix * pushConstants.mModelMatrix * pos4);

	// Normals for g-buffer
	mat3 normalMatrix = transpose(inverse(mat3(vp.mViewMatrix * pushConstants.mModelMatrix)));
	normal = normalMatrix * inNormal;
}
