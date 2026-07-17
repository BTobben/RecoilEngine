/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "RenderSmokeTest.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "LegacyGLState.h"
#include "Rendering/GlobalRenderingInfo.h"
#include "System/Log/ILog.h"

namespace {

constexpr GLsizei SMOKE_SIZE = 32;

struct BindingState {
	GLint program = 0;
	GLint vertexArray = 0;
	GLint arrayBuffer = 0;
	GLint drawFramebuffer = 0;
	GLint readFramebuffer = 0;
	GLint packAlignment = 4;

	void Capture()
	{
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
		glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
	}

	void Restore() const
	{
		glUseProgram(program);
		glBindVertexArray(vertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
		glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
	}
};

struct DrawStateProbe {
	GLboolean blendEnabled = GL_FALSE;
	GLboolean depthTestEnabled = GL_FALSE;
	GLboolean depthWriteMask = GL_TRUE;
	std::array<GLint, 4> viewport = {};
	std::array<GLfloat, 4> clearColor = {};
	GLint activeTexture = GL_TEXTURE0;

	void Capture()
	{
		blendEnabled = glIsEnabled(GL_BLEND);
		depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);
		glGetIntegerv(GL_VIEWPORT, viewport.data());
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor.data());
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	}

	[[nodiscard]] bool operator==(const DrawStateProbe& other) const
	{
		return blendEnabled == other.blendEnabled
			&& depthTestEnabled == other.depthTestEnabled
			&& depthWriteMask == other.depthWriteMask
			&& viewport == other.viewport
			&& clearColor == other.clearColor
			&& activeTexture == other.activeTexture;
	}
};

struct SmokeResources {
	GLuint vertexShader = 0;
	GLuint fragmentShader = 0;
	GLuint program = 0;
	GLuint vertexArray = 0;
	GLuint vertexBuffer = 0;
	GLuint framebuffer = 0;
	GLuint colorTexture = 0;

	~SmokeResources()
	{
		if (program != 0)
			glDeleteProgram(program);
		if (vertexShader != 0)
			glDeleteShader(vertexShader);
		if (fragmentShader != 0)
			glDeleteShader(fragmentShader);
		if (vertexBuffer != 0)
			glDeleteBuffers(1, &vertexBuffer);
		if (vertexArray != 0)
			glDeleteVertexArrays(1, &vertexArray);
		if (framebuffer != 0)
			glDeleteFramebuffers(1, &framebuffer);
		if (colorTexture != 0)
			glDeleteTextures(1, &colorTexture);
	}
};

std::string HexValue(const GLenum value)
{
	std::ostringstream stream;
	stream << "0x" << std::hex << std::uppercase << value;
	return stream.str();
}

std::vector<GLenum> DrainErrors()
{
	std::vector<GLenum> errors;
	for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
		errors.push_back(error);
	return errors;
}

std::string FormatErrors(const std::vector<GLenum>& errors)
{
	std::ostringstream stream;
	for (std::size_t index = 0; index < errors.size(); ++index) {
		if (index != 0)
			stream << ',';
		stream << HexValue(errors[index]);
	}
	return stream.str();
}

bool ProbeTextureCaptureQueries(std::string& error)
{
	struct Query {
		GLenum parameter;
		const char* name;
	};

	static constexpr std::array queries = {
		Query{GL_TEXTURE_BINDING_1D, "GL_TEXTURE_BINDING_1D"},
		Query{GL_TEXTURE_BINDING_2D, "GL_TEXTURE_BINDING_2D"},
		Query{GL_TEXTURE_BINDING_3D, "GL_TEXTURE_BINDING_3D"},
		Query{GL_TEXTURE_BINDING_1D_ARRAY, "GL_TEXTURE_BINDING_1D_ARRAY"},
		Query{GL_TEXTURE_BINDING_2D_ARRAY, "GL_TEXTURE_BINDING_2D_ARRAY"},
		Query{GL_TEXTURE_BINDING_RECTANGLE, "GL_TEXTURE_BINDING_RECTANGLE"},
		Query{GL_TEXTURE_BINDING_CUBE_MAP, "GL_TEXTURE_BINDING_CUBE_MAP"},
		Query{GL_TEXTURE_BINDING_BUFFER, "GL_TEXTURE_BINDING_BUFFER"},
		Query{GL_TEXTURE_BINDING_2D_MULTISAMPLE, "GL_TEXTURE_BINDING_2D_MULTISAMPLE"},
		Query{GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY, "GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY"},
	};

	GLint activeTexture = GL_TEXTURE0;
	GLint maxTextureUnits = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits);

	auto restoreActiveTexture = [&]() {
		glActiveTexture(activeTexture);
	};
	auto reportErrors = [&](const char* operation, const GLint unit) {
		const auto errors = DrainErrors();
		if (errors.empty())
			return false;

		restoreActiveTexture();
		DrainErrors();
		error = std::string("texture capture query ") + operation
			+ " on unit " + std::to_string(unit)
			+ " generated OpenGL errors: " + FormatErrors(errors);
		return true;
	};

	for (GLint unit = 0; unit < maxTextureUnits; ++unit) {
		glActiveTexture(GL_TEXTURE0 + unit);
		if (reportErrors("glActiveTexture", unit))
			return false;

		for (const Query& query: queries) {
			GLint value = 0;
			glGetIntegerv(query.parameter, &value);
			if (reportErrors(query.name, unit))
				return false;
		}

		GLint sampler = 0;
		glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
		if (reportErrors("GL_SAMPLER_BINDING", unit))
			return false;
	}

	restoreActiveTexture();
	if (const auto errors = DrainErrors(); !errors.empty()) {
		error = "restoring GL_ACTIVE_TEXTURE after texture capture probes generated OpenGL errors: "
			+ FormatErrors(errors);
		return false;
	}

	return true;
}

bool ProbeLegacyAttribGroups(std::string& error)
{
	struct Group {
		GLbitfield mask;
		const char* name;
	};

	static constexpr std::array groups = {
		Group{GL_COLOR_BUFFER_BIT, "GL_COLOR_BUFFER_BIT"},
		Group{GL_DEPTH_BUFFER_BIT, "GL_DEPTH_BUFFER_BIT"},
		Group{GL_ENABLE_BIT, "GL_ENABLE_BIT"},
		Group{GL_POLYGON_BIT, "GL_POLYGON_BIT"},
		Group{GL_SCISSOR_BIT, "GL_SCISSOR_BIT"},
		Group{GL_STENCIL_BUFFER_BIT, "GL_STENCIL_BUFFER_BIT"},
		Group{GL_TEXTURE_BIT, "GL_TEXTURE_BIT"},
		Group{GL_VIEWPORT_BIT, "GL_VIEWPORT_BIT"},
	};

	for (const Group& group: groups) {
		std::vector<GLenum> captureErrors;
		{
			GL::Legacy::ScopedAttrib attrib(group.mask);
			captureErrors = DrainErrors();
		}
		const auto restoreErrors = DrainErrors();

		if (!captureErrors.empty()) {
			error = std::string("legacy capture for ") + group.name
				+ " generated OpenGL errors: " + FormatErrors(captureErrors);
			return false;
		}
		if (!restoreErrors.empty()) {
			error = std::string("legacy restore for ") + group.name
				+ " generated OpenGL errors: " + FormatErrors(restoreErrors);
			return false;
		}
	}

	return true;
}

std::string ShaderLog(const GLuint shader)
{
	GLint length = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
	if (length <= 1)
		return {};

	std::string log(length, '\0');
	GLsizei written = 0;
	glGetShaderInfoLog(shader, length, &written, log.data());
	log.resize(written);
	return log;
}

std::string ProgramLog(const GLuint program)
{
	GLint length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
	if (length <= 1)
		return {};

	std::string log(length, '\0');
	GLsizei written = 0;
	glGetProgramInfoLog(program, length, &written, log.data());
	log.resize(written);
	return log;
}

bool CompileShader(const GLuint shader, const char* source, std::string& error)
{
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled == GL_TRUE)
		return true;

	error = "shader compilation failed: " + ShaderLog(shader);
	return false;
}

bool CreateProgram(SmokeResources& resources, std::string& error)
{
	static constexpr const char* vertexSource = R"(
#version 410 core
layout(location = 0) in vec2 position;
void main() {
	gl_Position = vec4(position, 0.0, 1.0);
}
)";
	static constexpr const char* fragmentSource = R"(
#version 410 core
layout(location = 0) out vec4 color;
void main() {
	color = vec4(1.0, 0.25, 0.0, 1.0);
}
)";

	resources.vertexShader = glCreateShader(GL_VERTEX_SHADER);
	resources.fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	if (resources.vertexShader == 0 || resources.fragmentShader == 0) {
		error = "glCreateShader returned zero";
		return false;
	}
	if (!CompileShader(resources.vertexShader, vertexSource, error))
		return false;
	if (!CompileShader(resources.fragmentShader, fragmentSource, error))
		return false;

	resources.program = glCreateProgram();
	if (resources.program == 0) {
		error = "glCreateProgram returned zero";
		return false;
	}
	glAttachShader(resources.program, resources.vertexShader);
	glAttachShader(resources.program, resources.fragmentShader);
	glLinkProgram(resources.program);

	GLint linked = GL_FALSE;
	glGetProgramiv(resources.program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE) {
		error = "program link failed: " + ProgramLog(resources.program);
		return false;
	}
	return true;
}

bool CreateRenderTarget(SmokeResources& resources, std::string& error)
{
	glGenTextures(1, &resources.colorTexture);
	glBindTexture(GL_TEXTURE_2D, resources.colorTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SMOKE_SIZE, SMOKE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glGenFramebuffers(1, &resources.framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, resources.framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resources.colorTexture, 0);
	static constexpr GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &drawBuffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status == GL_FRAMEBUFFER_COMPLETE)
		return true;

	error = "framebuffer incomplete: " + HexValue(status);
	return false;
}

bool CreateTriangle(SmokeResources& resources, std::string& error)
{
	static constexpr std::array<GLfloat, 6> vertices = {
		-1.0f, -1.0f,
		 3.0f, -1.0f,
		-1.0f,  3.0f,
	};

	glGenVertexArrays(1, &resources.vertexArray);
	glGenBuffers(1, &resources.vertexBuffer);
	if (resources.vertexArray == 0 || resources.vertexBuffer == 0) {
		error = "failed to allocate VAO/VBO";
		return false;
	}

	glBindVertexArray(resources.vertexArray);
	glBindBuffer(GL_ARRAY_BUFFER, resources.vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
	return true;
}

bool PixelMatches(const std::array<GLubyte, 4>& pixel)
{
	return pixel[0] >= 240
		&& pixel[1] >= 48 && pixel[1] <= 80
		&& pixel[2] <= 10
		&& pixel[3] >= 240;
}

} // namespace

bool GL::RunStartupRenderSmokeTest(std::string& report)
{
	#ifdef HEADLESS
	report = "graphical OpenGL smoke test is unavailable in a headless build";
	LOG_L(L_ERROR, "[GLSmoke] FAIL %s", report.c_str());
	return false;
	#else
	report.clear();

	const int contextVersion = globalRenderingInfo.glContextVersion.x * 10 + globalRenderingInfo.glContextVersion.y;
	if (contextVersion < 41 || !globalRenderingInfo.glContextIsCore) {
		report = "requires an OpenGL 4.1-or-newer Core context; received "
			+ std::to_string(globalRenderingInfo.glContextVersion.x) + "."
			+ std::to_string(globalRenderingInfo.glContextVersion.y)
			+ (globalRenderingInfo.glContextIsCore ? " Core" : " Compatibility");
		LOG_L(L_ERROR, "[GLSmoke] FAIL %s", report.c_str());
		return false;
	}
	if (!Legacy::UsesEmulatedAttribStack()) {
		report = "Core context did not select the engine-owned attribute stack";
		LOG_L(L_ERROR, "[GLSmoke] FAIL %s", report.c_str());
		return false;
	}

	const auto startupErrors = DrainErrors();
	if (!startupErrors.empty()) {
		report = "engine startup left OpenGL errors: " + FormatErrors(startupErrors);
		LOG_L(L_ERROR, "[GLSmoke] FAIL %s", report.c_str());
		return false;
	}
	if (!ProbeTextureCaptureQueries(report)) {
		LOG_L(L_ERROR, "[GLSmoke] FAIL %s", report.c_str());
		return false;
	}
	if (!ProbeLegacyAttribGroups(report)) {
		LOG_L(L_ERROR, "[GLSmoke] FAIL %s", report.c_str());
		return false;
	}

	BindingState bindings;
	bindings.Capture();
	DrawStateProbe stateBefore;
	stateBefore.Capture();
	const std::size_t stackDepthBefore = Legacy::AttribStackDepth();

	bool rendered = false;
	std::string error;
	std::array<GLubyte, 4> centerPixel = {};

	{
		Legacy::ScopedAttrib attrib(
			GL_COLOR_BUFFER_BIT |
			GL_DEPTH_BUFFER_BIT |
			GL_ENABLE_BIT |
			GL_POLYGON_BIT |
			GL_SCISSOR_BIT |
			GL_STENCIL_BUFFER_BIT |
			GL_TEXTURE_BIT |
			GL_VIEWPORT_BIT
		);
		SmokeResources resources;

		do {
			if (!CreateProgram(resources, error))
				break;
			if (!CreateRenderTarget(resources, error))
				break;
			if (!CreateTriangle(resources, error))
				break;

			glViewport(0, 0, SMOKE_SIZE, SMOKE_SIZE);
			glDisable(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_FRAMEBUFFER_SRGB);
			if (stateBefore.blendEnabled == GL_TRUE)
				glDisable(GL_BLEND);
			else
				glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ZERO);
			glBlendEquation(GL_FUNC_ADD);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			glUseProgram(resources.program);
			glBindVertexArray(resources.vertexArray);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glFinish();

			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(SMOKE_SIZE / 2, SMOKE_SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel.data());
			const auto drawErrors = DrainErrors();
			if (!drawErrors.empty()) {
				error = "render generated OpenGL errors: " + FormatErrors(drawErrors);
				break;
			}
			if (!PixelMatches(centerPixel)) {
				error = "center pixel did not contain the GLSL triangle";
				break;
			}

			rendered = true;
		} while (false);

		// Restore object bindings before resources are destroyed and before the
		// attribute snapshot restores draw-buffer state on the original FBO.
		bindings.Restore();
	}

	DrawStateProbe stateAfter;
	stateAfter.Capture();
	const bool stateRestored = (stateBefore == stateAfter) && Legacy::AttribStackDepth() == stackDepthBefore;
	const auto restoreErrors = DrainErrors();

	if (!rendered) {
		report = error;
	} else if (!stateRestored) {
		report = "profile-aware attribute stack did not restore the startup draw state";
	} else if (!restoreErrors.empty()) {
		report = "state restoration generated OpenGL errors: " + FormatErrors(restoreErrors);
	} else {
		std::ostringstream stream;
		stream << "PASS context=" << globalRenderingInfo.glContextVersion.x << '.' << globalRenderingInfo.glContextVersion.y
			<< " Core renderer=\"" << globalRenderingInfo.glRenderer << "\""
			<< " pixel=<" << static_cast<unsigned>(centerPixel[0])
			<< ',' << static_cast<unsigned>(centerPixel[1])
			<< ',' << static_cast<unsigned>(centerPixel[2])
			<< ',' << static_cast<unsigned>(centerPixel[3]) << ">"
			<< " stateRestored=1";
		report = stream.str();
		LOG("[GLSmoke] %s", report.c_str());
		return true;
	}

	LOG_L(L_ERROR, "[GLSmoke] FAIL %s pixel=<%u,%u,%u,%u> stateRestored=%d",
		report.c_str(),
		static_cast<unsigned>(centerPixel[0]),
		static_cast<unsigned>(centerPixel[1]),
		static_cast<unsigned>(centerPixel[2]),
		static_cast<unsigned>(centerPixel[3]),
		stateRestored);
	return false;
	#endif
}
