/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#define RECOIL_LEGACY_GL_STATE_IMPLEMENTATION
#include "LegacyGLState.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "Rendering/GlobalRenderingInfo.h"
#include "System/Log/ILog.h"

namespace {

constexpr GLbitfield CORE_ATTRIB_MASK =
	GL_COLOR_BUFFER_BIT |
	GL_DEPTH_BUFFER_BIT |
	GL_ENABLE_BIT |
	GL_LINE_BIT |
	GL_MULTISAMPLE_BIT |
	GL_POINT_BIT |
	GL_POLYGON_BIT |
	GL_SCISSOR_BIT |
	GL_STENCIL_BUFFER_BIT |
	GL_TEXTURE_BIT |
	GL_VIEWPORT_BIT;

struct CapabilityState {
	GLenum capability = GL_NONE;
	GLboolean enabled = GL_FALSE;
};

struct DrawBufferState {
	GLboolean blendEnabled = GL_FALSE;
	GLint blendSrcRGB = GL_ONE;
	GLint blendDstRGB = GL_ZERO;
	GLint blendSrcAlpha = GL_ONE;
	GLint blendDstAlpha = GL_ZERO;
	GLint blendEquationRGB = GL_FUNC_ADD;
	GLint blendEquationAlpha = GL_FUNC_ADD;
	std::array<GLboolean, 4> colorMask = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
	GLint drawBuffer = GL_NONE;
};

struct ColorBufferState {
	std::vector<DrawBufferState> drawBuffers;
	std::array<GLfloat, 4> blendColor = {};
	std::array<GLfloat, 4> clearColor = {};
	GLboolean ditherEnabled = GL_FALSE;
	GLboolean logicOpEnabled = GL_FALSE;
	GLboolean framebufferSRGBEnabled = GL_FALSE;
	GLint logicOpMode = GL_COPY;
	GLint readBuffer = GL_NONE;
};

struct DepthBufferState {
	GLboolean testEnabled = GL_FALSE;
	GLboolean writeMask = GL_TRUE;
	GLint function = GL_LESS;
	GLdouble clearValue = 1.0;
	std::array<GLdouble, 2> range = {0.0, 1.0};
};

struct StencilFaceState {
	GLint function = GL_ALWAYS;
	GLint reference = 0;
	GLint valueMask = -1;
	GLint writeMask = -1;
	GLint failOperation = GL_KEEP;
	GLint depthFailOperation = GL_KEEP;
	GLint depthPassOperation = GL_KEEP;
};

struct StencilBufferState {
	GLboolean testEnabled = GL_FALSE;
	GLint clearValue = 0;
	StencilFaceState front;
	StencilFaceState back;
};

struct PolygonState {
	GLboolean cullEnabled = GL_FALSE;
	GLboolean offsetFillEnabled = GL_FALSE;
	GLboolean offsetLineEnabled = GL_FALSE;
	GLboolean offsetPointEnabled = GL_FALSE;
	GLint cullFace = GL_BACK;
	GLint frontFace = GL_CCW;
	std::array<GLint, 2> mode = {GL_FILL, GL_FILL};
	GLfloat offsetFactor = 0.0f;
	GLfloat offsetUnits = 0.0f;
};

struct ViewportState {
	std::array<GLint, 4> viewport = {};
	std::array<GLdouble, 2> depthRange = {0.0, 1.0};
};

struct ScissorState {
	GLboolean testEnabled = GL_FALSE;
	std::array<GLint, 4> box = {};
};

struct MultisampleState {
	GLboolean multisampleEnabled = GL_FALSE;
	GLboolean alphaToCoverageEnabled = GL_FALSE;
	GLboolean alphaToOneEnabled = GL_FALSE;
	GLboolean sampleCoverageEnabled = GL_FALSE;
	GLboolean sampleMaskEnabled = GL_FALSE;
	GLboolean sampleShadingEnabled = GL_FALSE;
	GLboolean coverageInvert = GL_FALSE;
	GLfloat coverageValue = 1.0f;
	GLfloat minimumSampleShading = 0.0f;
	std::vector<GLuint> sampleMasks;
};

struct TextureUnitState {
	GLint texture1D = 0;
	GLint texture2D = 0;
	GLint texture3D = 0;
	GLint texture1DArray = 0;
	GLint texture2DArray = 0;
	GLint textureRectangle = 0;
	GLint textureCubeMap = 0;
	GLint textureBuffer = 0;
	GLint texture2DMultisample = 0;
	GLint texture2DMultisampleArray = 0;
	GLint sampler = 0;
};

struct TextureState {
	GLint activeTexture = GL_TEXTURE0;
	std::vector<TextureUnitState> units;
};

struct AttribState {
	GLbitfield mask = 0;
	std::vector<CapabilityState> capabilities;
	std::optional<ColorBufferState> colorBuffer;
	std::optional<DepthBufferState> depthBuffer;
	std::optional<StencilBufferState> stencilBuffer;
	std::optional<PolygonState> polygon;
	std::optional<ViewportState> viewport;
	std::optional<ScissorState> scissor;
	std::optional<MultisampleState> multisample;
	std::optional<TextureState> texture;
	std::optional<GLfloat> lineWidth;
	std::optional<GLfloat> pointSize;
};

struct StackEntry {
	bool emulated = false;
	std::optional<AttribState> state;
};

thread_local std::vector<StackEntry> attribStack;
GLbitfield warnedUnsupportedBits = 0;

bool ShouldEmulate()
{
	#ifdef HEADLESS
	return false;
	#else
	return globalRenderingInfo.glContextIsCore || glad_glPushAttrib == nullptr || glad_glPopAttrib == nullptr;
	#endif
}

bool SupportsIndexedBlendState()
{
	return GLAD_GL_VERSION_4_0 || GLAD_GL_ARB_draw_buffers_blend;
}

bool SupportsSampleShading()
{
	return GLAD_GL_VERSION_4_0 || GLAD_GL_ARB_sample_shading;
}

bool SupportsSamplerObjects()
{
	return GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_sampler_objects;
}

void SetCapability(const GLenum capability, const GLboolean enabled)
{
	if (enabled == GL_TRUE)
		glEnable(capability);
	else
		glDisable(capability);
}

void CaptureCapabilities(AttribState& state)
{
	static constexpr std::array capabilities = {
		GL_BLEND,
		GL_COLOR_LOGIC_OP,
		GL_CULL_FACE,
		GL_DEPTH_CLAMP,
		GL_DEPTH_TEST,
		GL_DITHER,
		GL_FRAMEBUFFER_SRGB,
		GL_MULTISAMPLE,
		GL_POLYGON_OFFSET_FILL,
		GL_POLYGON_OFFSET_LINE,
		GL_POLYGON_OFFSET_POINT,
		GL_PRIMITIVE_RESTART,
		GL_PROGRAM_POINT_SIZE,
		GL_RASTERIZER_DISCARD,
		GL_SAMPLE_ALPHA_TO_COVERAGE,
		GL_SAMPLE_ALPHA_TO_ONE,
		GL_SAMPLE_COVERAGE,
		GL_SAMPLE_MASK,
		GL_SCISSOR_TEST,
		GL_STENCIL_TEST,
		GL_TEXTURE_CUBE_MAP_SEAMLESS,
	};

	state.capabilities.reserve(capabilities.size() + 8);
	for (const GLenum capability: capabilities)
		state.capabilities.push_back({capability, glIsEnabled(capability)});
	if (SupportsSampleShading())
		state.capabilities.push_back({GL_SAMPLE_SHADING, glIsEnabled(GL_SAMPLE_SHADING)});

	GLint maxClipDistances = 0;
	glGetIntegerv(GL_MAX_CLIP_DISTANCES, &maxClipDistances);
	maxClipDistances = std::clamp(maxClipDistances, 0, 8);

	for (GLint index = 0; index < maxClipDistances; ++index) {
		const GLenum capability = GL_CLIP_DISTANCE0 + index;
		state.capabilities.push_back({capability, glIsEnabled(capability)});
	}
}

ColorBufferState CaptureColorBuffer()
{
	ColorBufferState state;
	const bool indexedBlendState = SupportsIndexedBlendState();

	GLint globalBlendSrcRGB = GL_ONE;
	GLint globalBlendDstRGB = GL_ZERO;
	GLint globalBlendSrcAlpha = GL_ONE;
	GLint globalBlendDstAlpha = GL_ZERO;
	GLint globalBlendEquationRGB = GL_FUNC_ADD;
	GLint globalBlendEquationAlpha = GL_FUNC_ADD;
	if (!indexedBlendState) {
		glGetIntegerv(GL_BLEND_SRC_RGB, &globalBlendSrcRGB);
		glGetIntegerv(GL_BLEND_DST_RGB, &globalBlendDstRGB);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &globalBlendSrcAlpha);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &globalBlendDstAlpha);
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &globalBlendEquationRGB);
		glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &globalBlendEquationAlpha);
	}

	GLint maxDrawBuffers = 1;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
	maxDrawBuffers = std::clamp(maxDrawBuffers, 1, 64);
	state.drawBuffers.resize(maxDrawBuffers);

	for (GLint index = 0; index < maxDrawBuffers; ++index) {
		auto& drawBuffer = state.drawBuffers[index];
		drawBuffer.blendEnabled = glIsEnabledi(GL_BLEND, index);
		if (indexedBlendState) {
			glGetIntegeri_v(GL_BLEND_SRC_RGB, index, &drawBuffer.blendSrcRGB);
			glGetIntegeri_v(GL_BLEND_DST_RGB, index, &drawBuffer.blendDstRGB);
			glGetIntegeri_v(GL_BLEND_SRC_ALPHA, index, &drawBuffer.blendSrcAlpha);
			glGetIntegeri_v(GL_BLEND_DST_ALPHA, index, &drawBuffer.blendDstAlpha);
			glGetIntegeri_v(GL_BLEND_EQUATION_RGB, index, &drawBuffer.blendEquationRGB);
			glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, index, &drawBuffer.blendEquationAlpha);
		} else {
			drawBuffer.blendSrcRGB = globalBlendSrcRGB;
			drawBuffer.blendDstRGB = globalBlendDstRGB;
			drawBuffer.blendSrcAlpha = globalBlendSrcAlpha;
			drawBuffer.blendDstAlpha = globalBlendDstAlpha;
			drawBuffer.blendEquationRGB = globalBlendEquationRGB;
			drawBuffer.blendEquationAlpha = globalBlendEquationAlpha;
		}
		glGetBooleani_v(GL_COLOR_WRITEMASK, index, drawBuffer.colorMask.data());
		glGetIntegerv(GL_DRAW_BUFFER0 + index, &drawBuffer.drawBuffer);
	}

	glGetFloatv(GL_BLEND_COLOR, state.blendColor.data());
	glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clearColor.data());
	state.ditherEnabled = glIsEnabled(GL_DITHER);
	state.logicOpEnabled = glIsEnabled(GL_COLOR_LOGIC_OP);
	state.framebufferSRGBEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
	glGetIntegerv(GL_LOGIC_OP_MODE, &state.logicOpMode);
	glGetIntegerv(GL_READ_BUFFER, &state.readBuffer);

	return state;
}

void RestoreColorBuffer(const ColorBufferState& state)
{
	std::vector<GLenum> drawBuffers;
	drawBuffers.reserve(state.drawBuffers.size());
	const bool indexedBlendState = SupportsIndexedBlendState();

	for (GLuint index = 0; index < state.drawBuffers.size(); ++index) {
		const auto& drawBuffer = state.drawBuffers[index];
		if (drawBuffer.blendEnabled == GL_TRUE)
			glEnablei(GL_BLEND, index);
		else
			glDisablei(GL_BLEND, index);
		if (indexedBlendState) {
			glBlendFuncSeparatei(index, drawBuffer.blendSrcRGB, drawBuffer.blendDstRGB, drawBuffer.blendSrcAlpha, drawBuffer.blendDstAlpha);
			glBlendEquationSeparatei(index, drawBuffer.blendEquationRGB, drawBuffer.blendEquationAlpha);
		}
		glColorMaski(index, drawBuffer.colorMask[0], drawBuffer.colorMask[1], drawBuffer.colorMask[2], drawBuffer.colorMask[3]);
		drawBuffers.push_back(drawBuffer.drawBuffer);
	}
	if (!indexedBlendState && !state.drawBuffers.empty()) {
		const auto& drawBuffer = state.drawBuffers.front();
		glBlendFuncSeparate(drawBuffer.blendSrcRGB, drawBuffer.blendDstRGB, drawBuffer.blendSrcAlpha, drawBuffer.blendDstAlpha);
		glBlendEquationSeparate(drawBuffer.blendEquationRGB, drawBuffer.blendEquationAlpha);
	}

	glBlendColor(state.blendColor[0], state.blendColor[1], state.blendColor[2], state.blendColor[3]);
	glClearColor(state.clearColor[0], state.clearColor[1], state.clearColor[2], state.clearColor[3]);
	SetCapability(GL_DITHER, state.ditherEnabled);
	SetCapability(GL_COLOR_LOGIC_OP, state.logicOpEnabled);
	SetCapability(GL_FRAMEBUFFER_SRGB, state.framebufferSRGBEnabled);
	glLogicOp(state.logicOpMode);

	// GL_DRAW_BUFFER0 can report one of the default-framebuffer aliases even
	// though glDrawBuffers does not accept those aliases in its buffer array.
	// In particular, a double-buffered macOS Core context reports GL_BACK and
	// turns glDrawBuffers(MAX_DRAW_BUFFERS, {GL_BACK, GL_NONE, ...}) into
	// GL_INVALID_ENUM.  glDrawBuffer is the matching API for restoring these
	// single-selection aliases and also resets all higher draw slots to NONE.
	if (!drawBuffers.empty()) {
		switch (drawBuffers.front()) {
			case GL_FRONT:
			case GL_BACK:
			case GL_LEFT:
			case GL_RIGHT:
			case GL_FRONT_AND_BACK: {
				glDrawBuffer(drawBuffers.front());
			} break;
			default: {
				glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
			} break;
		}
	}
	glReadBuffer(state.readBuffer);
}

DepthBufferState CaptureDepthBuffer()
{
	DepthBufferState state;
	state.testEnabled = glIsEnabled(GL_DEPTH_TEST);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &state.writeMask);
	glGetIntegerv(GL_DEPTH_FUNC, &state.function);
	glGetDoublev(GL_DEPTH_CLEAR_VALUE, &state.clearValue);
	glGetDoublev(GL_DEPTH_RANGE, state.range.data());
	return state;
}

void RestoreDepthBuffer(const DepthBufferState& state)
{
	SetCapability(GL_DEPTH_TEST, state.testEnabled);
	glDepthMask(state.writeMask);
	glDepthFunc(state.function);
	glClearDepth(state.clearValue);
	glDepthRange(state.range[0], state.range[1]);
}

StencilFaceState CaptureStencilFace(const bool back)
{
	StencilFaceState state;
	glGetIntegerv(back ? GL_STENCIL_BACK_FUNC : GL_STENCIL_FUNC, &state.function);
	glGetIntegerv(back ? GL_STENCIL_BACK_REF : GL_STENCIL_REF, &state.reference);
	glGetIntegerv(back ? GL_STENCIL_BACK_VALUE_MASK : GL_STENCIL_VALUE_MASK, &state.valueMask);
	glGetIntegerv(back ? GL_STENCIL_BACK_WRITEMASK : GL_STENCIL_WRITEMASK, &state.writeMask);
	glGetIntegerv(back ? GL_STENCIL_BACK_FAIL : GL_STENCIL_FAIL, &state.failOperation);
	glGetIntegerv(back ? GL_STENCIL_BACK_PASS_DEPTH_FAIL : GL_STENCIL_PASS_DEPTH_FAIL, &state.depthFailOperation);
	glGetIntegerv(back ? GL_STENCIL_BACK_PASS_DEPTH_PASS : GL_STENCIL_PASS_DEPTH_PASS, &state.depthPassOperation);
	return state;
}

StencilBufferState CaptureStencilBuffer()
{
	StencilBufferState state;
	state.testEnabled = glIsEnabled(GL_STENCIL_TEST);
	glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &state.clearValue);
	state.front = CaptureStencilFace(false);
	state.back = CaptureStencilFace(true);
	return state;
}

void RestoreStencilFace(const GLenum face, const StencilFaceState& state)
{
	glStencilFuncSeparate(face, state.function, state.reference, state.valueMask);
	glStencilMaskSeparate(face, state.writeMask);
	glStencilOpSeparate(face, state.failOperation, state.depthFailOperation, state.depthPassOperation);
}

void RestoreStencilBuffer(const StencilBufferState& state)
{
	SetCapability(GL_STENCIL_TEST, state.testEnabled);
	glClearStencil(state.clearValue);
	RestoreStencilFace(GL_FRONT, state.front);
	RestoreStencilFace(GL_BACK, state.back);
}

PolygonState CapturePolygon()
{
	PolygonState state;
	state.cullEnabled = glIsEnabled(GL_CULL_FACE);
	state.offsetFillEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	state.offsetLineEnabled = glIsEnabled(GL_POLYGON_OFFSET_LINE);
	state.offsetPointEnabled = glIsEnabled(GL_POLYGON_OFFSET_POINT);
	glGetIntegerv(GL_CULL_FACE_MODE, &state.cullFace);
	glGetIntegerv(GL_FRONT_FACE, &state.frontFace);
	glGetIntegerv(GL_POLYGON_MODE, state.mode.data());
	glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &state.offsetFactor);
	glGetFloatv(GL_POLYGON_OFFSET_UNITS, &state.offsetUnits);
	return state;
}

void RestorePolygon(const PolygonState& state)
{
	SetCapability(GL_CULL_FACE, state.cullEnabled);
	SetCapability(GL_POLYGON_OFFSET_FILL, state.offsetFillEnabled);
	SetCapability(GL_POLYGON_OFFSET_LINE, state.offsetLineEnabled);
	SetCapability(GL_POLYGON_OFFSET_POINT, state.offsetPointEnabled);
	glCullFace(state.cullFace);
	glFrontFace(state.frontFace);
	glPolygonMode(GL_FRONT_AND_BACK, state.mode[0]);
	glPolygonOffset(state.offsetFactor, state.offsetUnits);
}

ViewportState CaptureViewport()
{
	ViewportState state;
	glGetIntegerv(GL_VIEWPORT, state.viewport.data());
	glGetDoublev(GL_DEPTH_RANGE, state.depthRange.data());
	return state;
}

void RestoreViewport(const ViewportState& state)
{
	glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
	glDepthRange(state.depthRange[0], state.depthRange[1]);
}

ScissorState CaptureScissor()
{
	ScissorState state;
	state.testEnabled = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_SCISSOR_BOX, state.box.data());
	return state;
}

void RestoreScissor(const ScissorState& state)
{
	SetCapability(GL_SCISSOR_TEST, state.testEnabled);
	glScissor(state.box[0], state.box[1], state.box[2], state.box[3]);
}

MultisampleState CaptureMultisample()
{
	MultisampleState state;
	state.multisampleEnabled = glIsEnabled(GL_MULTISAMPLE);
	state.alphaToCoverageEnabled = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
	state.alphaToOneEnabled = glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE);
	state.sampleCoverageEnabled = glIsEnabled(GL_SAMPLE_COVERAGE);
	state.sampleMaskEnabled = glIsEnabled(GL_SAMPLE_MASK);
	glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &state.coverageInvert);
	glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &state.coverageValue);
	if (SupportsSampleShading()) {
		state.sampleShadingEnabled = glIsEnabled(GL_SAMPLE_SHADING);
		glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE, &state.minimumSampleShading);
	}

	GLint maxSampleMaskWords = 0;
	glGetIntegerv(GL_MAX_SAMPLE_MASK_WORDS, &maxSampleMaskWords);
	maxSampleMaskWords = std::clamp(maxSampleMaskWords, 0, 64);
	state.sampleMasks.resize(maxSampleMaskWords);
	for (GLint index = 0; index < maxSampleMaskWords; ++index) {
		GLint value = 0;
		glGetIntegeri_v(GL_SAMPLE_MASK_VALUE, index, &value);
		state.sampleMasks[index] = value;
	}

	return state;
}

void RestoreMultisample(const MultisampleState& state)
{
	SetCapability(GL_MULTISAMPLE, state.multisampleEnabled);
	SetCapability(GL_SAMPLE_ALPHA_TO_COVERAGE, state.alphaToCoverageEnabled);
	SetCapability(GL_SAMPLE_ALPHA_TO_ONE, state.alphaToOneEnabled);
	SetCapability(GL_SAMPLE_COVERAGE, state.sampleCoverageEnabled);
	SetCapability(GL_SAMPLE_MASK, state.sampleMaskEnabled);
	glSampleCoverage(state.coverageValue, state.coverageInvert);
	if (SupportsSampleShading()) {
		SetCapability(GL_SAMPLE_SHADING, state.sampleShadingEnabled);
		glMinSampleShading(state.minimumSampleShading);
	}
	for (GLuint index = 0; index < state.sampleMasks.size(); ++index)
		glSampleMaski(index, state.sampleMasks[index]);
}

TextureState CaptureTexture()
{
	TextureState state;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);

	GLint maxTextureUnits = 1;
	glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
	maxTextureUnits = std::clamp(maxTextureUnits, 1, 192);
	state.units.resize(maxTextureUnits);

	for (GLint index = 0; index < maxTextureUnits; ++index) {
		auto& unit = state.units[index];
		glActiveTexture(GL_TEXTURE0 + index);
		glGetIntegerv(GL_TEXTURE_BINDING_1D, &unit.texture1D);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &unit.texture2D);
		glGetIntegerv(GL_TEXTURE_BINDING_3D, &unit.texture3D);
		glGetIntegerv(GL_TEXTURE_BINDING_1D_ARRAY, &unit.texture1DArray);
		glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &unit.texture2DArray);
		glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &unit.textureRectangle);
		glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &unit.textureCubeMap);
		glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &unit.textureBuffer);
		glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE, &unit.texture2DMultisample);
		glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY, &unit.texture2DMultisampleArray);
		if (SupportsSamplerObjects())
			glGetIntegerv(GL_SAMPLER_BINDING, &unit.sampler);
	}

	glActiveTexture(state.activeTexture);
	return state;
}

void RestoreTexture(const TextureState& state)
{
	for (GLuint index = 0; index < state.units.size(); ++index) {
		const auto& unit = state.units[index];
		glActiveTexture(GL_TEXTURE0 + index);
		glBindTexture(GL_TEXTURE_1D, unit.texture1D);
		glBindTexture(GL_TEXTURE_2D, unit.texture2D);
		glBindTexture(GL_TEXTURE_3D, unit.texture3D);
		glBindTexture(GL_TEXTURE_1D_ARRAY, unit.texture1DArray);
		glBindTexture(GL_TEXTURE_2D_ARRAY, unit.texture2DArray);
		glBindTexture(GL_TEXTURE_RECTANGLE, unit.textureRectangle);
		glBindTexture(GL_TEXTURE_CUBE_MAP, unit.textureCubeMap);
		glBindTexture(GL_TEXTURE_BUFFER, unit.textureBuffer);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, unit.texture2DMultisample);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, unit.texture2DMultisampleArray);
		if (SupportsSamplerObjects())
			glBindSampler(index, unit.sampler);
	}

	glActiveTexture(state.activeTexture);
}

AttribState CaptureState(const GLbitfield requestedMask)
{
	AttribState state;
	state.mask = requestedMask & CORE_ATTRIB_MASK;

	if ((state.mask & GL_ENABLE_BIT) != 0)
		CaptureCapabilities(state);
	if ((state.mask & GL_COLOR_BUFFER_BIT) != 0)
		state.colorBuffer = CaptureColorBuffer();
	if ((state.mask & GL_DEPTH_BUFFER_BIT) != 0)
		state.depthBuffer = CaptureDepthBuffer();
	if ((state.mask & GL_STENCIL_BUFFER_BIT) != 0)
		state.stencilBuffer = CaptureStencilBuffer();
	if ((state.mask & GL_POLYGON_BIT) != 0)
		state.polygon = CapturePolygon();
	if ((state.mask & GL_VIEWPORT_BIT) != 0)
		state.viewport = CaptureViewport();
	if ((state.mask & GL_SCISSOR_BIT) != 0)
		state.scissor = CaptureScissor();
	if ((state.mask & GL_MULTISAMPLE_BIT) != 0)
		state.multisample = CaptureMultisample();
	if ((state.mask & GL_TEXTURE_BIT) != 0)
		state.texture = CaptureTexture();
	if ((state.mask & GL_LINE_BIT) != 0) {
		GLfloat lineWidth = 1.0f;
		glGetFloatv(GL_LINE_WIDTH, &lineWidth);
		state.lineWidth = lineWidth;
	}
	if ((state.mask & GL_POINT_BIT) != 0) {
		GLfloat pointSize = 1.0f;
		glGetFloatv(GL_POINT_SIZE, &pointSize);
		state.pointSize = pointSize;
	}

	return state;
}

void RestoreState(const AttribState& state)
{
	if (state.colorBuffer)
		RestoreColorBuffer(*state.colorBuffer);
	if (state.depthBuffer)
		RestoreDepthBuffer(*state.depthBuffer);
	if (state.stencilBuffer)
		RestoreStencilBuffer(*state.stencilBuffer);
	if (state.polygon)
		RestorePolygon(*state.polygon);
	if (state.viewport)
		RestoreViewport(*state.viewport);
	if (state.scissor)
		RestoreScissor(*state.scissor);
	if (state.multisample)
		RestoreMultisample(*state.multisample);
	if (state.texture)
		RestoreTexture(*state.texture);
	if (state.lineWidth)
		glLineWidth(*state.lineWidth);
	if (state.pointSize)
		glPointSize(*state.pointSize);

	// Restore this last because GL_ENABLE_BIT overlaps several narrower
	// attribute groups and is the authoritative snapshot when requested.
	for (const auto& capability: state.capabilities)
		SetCapability(capability.capability, capability.enabled);
}

} // namespace

void GL::Legacy::PushAttrib(const GLbitfield mask)
{
	if (!ShouldEmulate()) {
		glad_glPushAttrib(mask);
		attribStack.push_back({false, std::nullopt});
		return;
	}

	const GLbitfield unsupportedBits = mask & ~CORE_ATTRIB_MASK;
	const GLbitfield newlyUnsupportedBits = unsupportedBits & ~warnedUnsupportedBits;
	if (newlyUnsupportedBits != 0) {
		warnedUnsupportedBits |= newlyUnsupportedBits;
		LOG_L(L_WARNING,
			"[GL::Legacy] Core-profile glPushAttrib emulation cannot preserve removed legacy state bits 0x%08x; core draw state remains protected",
			newlyUnsupportedBits);
	}

	attribStack.push_back({true, CaptureState(mask)});
}

void GL::Legacy::PopAttrib()
{
	if (attribStack.empty()) {
		LOG_L(L_ERROR, "[GL::Legacy] glPopAttrib called with an empty profile-aware attribute stack");
		return;
	}

	StackEntry entry = std::move(attribStack.back());
	attribStack.pop_back();

	if (!entry.emulated) {
		if (glad_glPopAttrib != nullptr)
			glad_glPopAttrib();
		else
			LOG_L(L_ERROR, "[GL::Legacy] native glPopAttrib entry point disappeared before stack restoration");
		return;
	}

	RestoreState(*entry.state);
}

bool GL::Legacy::UsesEmulatedAttribStack()
{
	return ShouldEmulate();
}

std::size_t GL::Legacy::AttribStackDepth()
{
	return attribStack.size();
}

GLbitfield GL::Legacy::CoreAttribMask()
{
	return CORE_ATTRIB_MASK;
}
