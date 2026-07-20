#version 130

in vec2 vertexPos;
in vec2 texCoords;

out vec2 vTexCoords;

// Core profiles removed gl_ModelViewProjectionMatrix. MiniMap.cpp updates
// this from the same projection stack immediately before drawing.
uniform mat4 coreViewProjectionMatrix;

void main()
{
	gl_Position = coreViewProjectionMatrix * vec4(vertexPos, 0.0, 1.0);
	vTexCoords = texCoords;
}
