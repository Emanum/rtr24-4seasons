// copied from https://github.com/SaschaWillems/Vulkan/blob/master/data/shaders/glsl/texturecubemap/skybox.frag
#version 450

layout (binding = 1) uniform samplerCube samplerCubeMap;

layout (location = 0) in vec3 inUVW;

layout (location = 0) out vec4 outFragColor;
layout (location = 1) out vec4 outPos;
layout (location = 2) out vec4 outNormal;
layout (location = 3) out vec4 outPosWS;
layout (location = 4) out vec4 outNormalWS;

void main() 
{
	outFragColor = texture(samplerCubeMap, inUVW);
	outPos = vec4(1.0);
	outNormal = vec4(1.0);
	outPosWS = vec4(1.0);
	outNormalWS = vec4(1.0);
}
