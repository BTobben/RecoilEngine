#version 410 core

// VS input attributes
in vec2 pos;
in vec3 uvw;
in vec4 color;

uniform mat4 coreViewProjectionMatrix = mat4(1.0);

// VS output attributes
out vec3 vuvw;
out vec4 vColor;

void main() {
	vuvw = uvw;
	vColor = color;
	gl_Position = coreViewProjectionMatrix * vec4(pos.x, pos.y, 0.0, 1.0);
}
