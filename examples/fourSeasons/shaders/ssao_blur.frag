#version 450

layout(set = 0, binding = 0) uniform sampler2D ssaoTexture;
layout(set = 0, binding = 1) uniform uniformSSAO
{
    int enabled;
    int blur;
} SSAO;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fs_out;

void main() {
	if (SSAO.blur == 1) {
		const int blurRange = 2;
		int n = 0;
		vec2 texSize = 1.0 / vec2(textureSize(ssaoTexture, 0));
		float br = 0.0;
		for (int x = -blurRange; x <= blurRange; x++) {
			for (int y = -blurRange; y <= blurRange; y++) {
				vec2 offset = vec2(float(x), float(y)) * texSize;
				br += texture(ssaoTexture, texCoord + offset).r;
				n++;
			}
		}
		fs_out = vec4(br / n, 0.0, 0.0, 1.0);
	}
	else {
		fs_out = texture(ssaoTexture, texCoord);
	}
}