#version 410 core

// The legacy model renderer keeps the view transform on the projection
// stack and the per-piece transform on the model-view stack.  Core profiles
// have neither stack, so the draw path supplies their emulated values here.
layout(location = 0) in vec3 vertexPos;
layout(location = 1) in vec3 vertexNormal;
layout(location = 4) in vec4 vertexTexCoords;

uniform mat4 coreModelMatrix;
uniform mat4 coreViewProjectionMatrix;
uniform vec3 cameraPos;
uniform vec2 coreFogParams; // fog end, fog scale

out vec4 vertexWorldPos;
out vec3 cameraDir;
out float fogFactor;
out vec3 normalv;
out vec2 texCoord0;

#if (USE_SHADOWS == 1)
	uniform mat4 shadowMatrix;
	out vec4 shadowVertexPos;
#endif

void main(void)
{
	vertexWorldPos = coreModelMatrix * vec4(vertexPos, 1.0);
	normalv = mat3(transpose(inverse(coreModelMatrix))) * vertexNormal;
	gl_Position = coreViewProjectionMatrix * vertexWorldPos;
	cameraDir = vertexWorldPos.xyz - cameraPos;

#if (USE_SHADOWS == 1)
	shadowVertexPos = shadowMatrix * vertexWorldPos;
	shadowVertexPos.xy += vec2(0.5);
#endif

	texCoord0 = vertexTexCoords.xy;

#if (DEFERRED_MODE == 0)
	float fogCoord = length(cameraDir);
	fogFactor = clamp((coreFogParams.x - fogCoord) * coreFogParams.y, 0.0, 1.0);
#endif
}
