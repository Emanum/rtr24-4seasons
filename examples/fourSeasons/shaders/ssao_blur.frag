#version 450

layout(binding = 0) uniform sampler2D ssaoTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fs_out;

void main() {
	const int blurRange = 2;
	int n = blurRange * 2 + 1;
	vec2 texSize = 1.0 / vec2(textureSize(ssaoTexture, 0));
	float br = 0.0;
	for (int x = -blurRange; x <= blurRange; x++) {
		for (int y = -blurRange; y <= blurRange; y++) {
			vec2 offset = vec2(float(x), float(y)) * texSize;
			br += texture(ssaoTexture, texCoord + offset).r;
		}
	}
	fs_out = vec4(br / n, 0.0, 0.0, 1.0);
}