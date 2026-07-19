#version 410 core

#define textureS3o1 diffuseTex
#define textureS3o2 shadingTex
uniform sampler2D textureS3o1;
uniform sampler2D textureS3o2;
uniform samplerCube specularTex;
uniform samplerCube reflectTex;

uniform vec3 sunDir;
uniform vec3 sunDiffuse;
uniform vec3 sunAmbient;
uniform vec3 sunSpecular;
uniform vec3 coreFogColor;

#if (USE_SHADOWS == 1)
	in vec4 shadowVertexPos;
	uniform sampler2DShadow shadowTex;
	uniform sampler2D shadowColorTex;
	uniform float shadowDensity;
#endif

// In opaque passes teamColor.a is always 1.0; in alpha passes it contains
// either an object's alpha value or a feature distance-fade factor.
uniform vec4 teamColor;
uniform vec4 nanoColor;

in vec4 vertexWorldPos;
in vec3 cameraDir;
in float fogFactor;
in vec3 normalv;
in vec2 texCoord0;

#if (DEFERRED_MODE == 1)
	out vec4 fragData[GBUFFER_ZVALTEX_IDX];
#else
	out vec4 fragColor;
#endif

vec3 GetShadowMult(float NdotL) {
	#if (USE_SHADOWS == 1)
		vec3 shadowCoord = shadowVertexPos.xyz / shadowVertexPos.w;
		float sh = min(textureProj(shadowTex, shadowVertexPos), smoothstep(0.0, 0.35, NdotL));
		vec3 shColor = texture(shadowColorTex, shadowCoord.xy).rgb;
		return mix(1.0, sh, shadowDensity) * shColor;
	#else
		return vec3(1.0);
	#endif
}

#if (MAX_DYNAMIC_MODEL_LIGHTS > 0)
vec3 DynamicLighting(vec3 normal, vec3 diffuse, vec3 specular) {
	vec3 rgb = vec3(0.0);

	// Dynamic lights still use the compatibility light array.  This branch is
	// compiled out on the GL 4.1 compatibility tier, where the configured
	// dynamic-model-light count is zero.
	for (int i = 0; i < MAX_DYNAMIC_MODEL_LIGHTS; i++) {
		vec3 lightVec = gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].position.xyz - vertexWorldPos.xyz;
		vec3 halfVec = gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].halfVector.xyz;

		float lightRadius = gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].constantAttenuation;
		float lightDistance = length(lightVec);
		float lightScale = float(lightDistance <= lightRadius);
		float lightCosAngDiff = clamp(dot(normal, lightVec / lightDistance), 0.0, 1.0);
		float lightCosAngSpec = clamp(dot(normal, normalize(halfVec)), 0.0, 1.0);

		#ifdef OGL_SPEC_ATTENUATION
		float lightAttenuation =
			gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].constantAttenuation +
			gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].linearAttenuation * lightDistance +
			gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].quadraticAttenuation * lightDistance * lightDistance;
		lightAttenuation = 1.0 / max(lightAttenuation, 1.0);
		#else
		float lightAttenuation = 1.0 - min(1.0, (lightDistance * lightDistance) / (lightRadius * lightRadius));
		#endif

		float vectorDot = dot(-lightVec / lightDistance, gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].spotDirection);
		float cutoffDot = gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].spotCosCutoff;
		lightScale *= float(vectorDot >= cutoffDot);

		rgb += lightScale * gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].ambient.rgb;
		rgb += lightScale * lightAttenuation * diffuse * gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].diffuse.rgb * lightCosAngDiff;
		rgb += lightScale * lightAttenuation * specular * gl_LightSource[BASE_DYNAMIC_MODEL_LIGHT + i].specular.rgb * pow(lightCosAngSpec, 4.0);
	}

	return rgb;
}
#endif

void main(void)
{
	vec3 normal = normalize(normalv);
	float NdotLu = dot(normal, sunDir);
	float NdotL = max(NdotLu, 1e-3);
	vec3 light = NdotL * sunDiffuse + sunAmbient;

	vec4 diffuse = texture(textureS3o1, texCoord0);
	vec4 extraColor = texture(textureS3o2, texCoord0);
	vec3 reflectDir = reflect(cameraDir, normal);
	vec3 specular = texture(specularTex, reflectDir).rgb * sunSpecular;
	vec3 reflection = texture(reflectTex, reflectDir).rgb;

	vec3 shadowMult = GetShadowMult(NdotL);
	float alpha = teamColor.a * extraColor.a;

	specular *= extraColor.g * 4.0;
	specular *= shadowMult;
	light = mix(sunAmbient, light, shadowMult);
	reflection = mix(light, reflection, extraColor.g);
	reflection += extraColor.rrr;

#if (DEFERRED_MODE == 0)
	fragColor = diffuse;
	fragColor.rgb = mix(fragColor.rgb, teamColor.rgb, fragColor.a);
	fragColor.rgb = fragColor.rgb * reflection + specular;
#endif

#if (DEFERRED_MODE == 0 && MAX_DYNAMIC_MODEL_LIGHTS > 0)
	fragColor.rgb += DynamicLighting(normal, diffuse.rgb, specular);
#endif

#if (DEFERRED_MODE == 1)
	fragData[GBUFFER_NORMTEX_IDX] = vec4((normal + vec3(1.0)) * 0.5, 1.0);
	fragData[GBUFFER_DIFFTEX_IDX] = vec4(mix(diffuse.rgb, teamColor.rgb, diffuse.a), alpha);
	fragData[GBUFFER_DIFFTEX_IDX] = vec4(mix(fragData[GBUFFER_DIFFTEX_IDX].rgb, nanoColor.rgb, nanoColor.a), alpha);
	fragData[GBUFFER_SPECTEX_IDX] = vec4(extraColor.rgb, alpha);
	fragData[GBUFFER_EMITTEX_IDX] = vec4(0.0);
	fragData[GBUFFER_MISCTEX_IDX] = vec4(0.0);
#else
	fragColor.rgb = mix(coreFogColor, fragColor.rgb, fogFactor);
	fragColor.rgb = mix(fragColor.rgb, nanoColor.rgb, nanoColor.a);
	fragColor.a = alpha;
#endif
}
