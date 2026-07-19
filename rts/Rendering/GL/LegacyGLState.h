/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>

#include <glad/glad.h>

#include "System/Matrix44f.h"

namespace GL::Legacy {

/**
 * glPushAttrib/glPopAttrib were removed from Core profiles.  These wrappers
 * retain the native compatibility-profile stack and emulate the Core-profile
 * draw state that still exists in OpenGL 4.1.
 */
void PushAttrib(GLbitfield mask);
void PopAttrib();

// OpenGL 4.1 Core removed the fixed-function matrix stack, while a sizeable
// amount of engine and Lua-facing drawing code still uses it to prepare the
// standard RenderBuffer transform. Keep those call-sites profile agnostic.
void MatrixMode(GLenum mode);
void PushMatrix();
void PopMatrix();
void LoadIdentity();
void LoadMatrixf(const GLfloat* matrix);
void LoadMatrixd(const GLdouble* matrix);
void MultMatrixf(const GLfloat* matrix);
void MultMatrixd(const GLdouble* matrix);
void Translatef(GLfloat x, GLfloat y, GLfloat z);
void Translated(GLdouble x, GLdouble y, GLdouble z);
void Scalef(GLfloat x, GLfloat y, GLfloat z);
void Scaled(GLdouble x, GLdouble y, GLdouble z);
void Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void Rotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearValue, GLdouble farValue);
void Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearValue, GLdouble farValue);
void GetFloatv(GLenum pname, GLfloat* values);
void GetDoublev(GLenum pname, GLdouble* values);
void GetIntegerv(GLenum pname, GLint* values);

[[nodiscard]] const CMatrix44f& ModelViewMatrix();
[[nodiscard]] const CMatrix44f& ProjectionMatrix();
[[nodiscard]] CMatrix44f ModelViewProjectionMatrix();
[[nodiscard]] bool UsesEmulatedMatrixStack();

[[nodiscard]] bool UsesEmulatedAttribStack();
[[nodiscard]] std::size_t AttribStackDepth();
[[nodiscard]] GLbitfield CoreAttribMask();

class ScopedAttrib {
public:
	explicit ScopedAttrib(GLbitfield mask) { PushAttrib(mask); }
	~ScopedAttrib() { PopAttrib(); }

	ScopedAttrib(const ScopedAttrib&) = delete;
	ScopedAttrib& operator=(const ScopedAttrib&) = delete;
};

} // namespace GL::Legacy

// Route existing engine call-sites through the profile-aware implementation.
// The implementation translation unit opts out so it can call GLAD's native
// compatibility-profile entry points directly.
#if !defined(RECOIL_LEGACY_GL_STATE_IMPLEMENTATION)
	#undef glPushAttrib
	#undef glPopAttrib
	#undef glMatrixMode
	#undef glPushMatrix
	#undef glPopMatrix
	#undef glLoadIdentity
	#undef glLoadMatrixf
	#undef glLoadMatrixd
	#undef glMultMatrixf
	#undef glMultMatrixd
	#undef glTranslatef
	#undef glTranslated
	#undef glScalef
	#undef glScaled
	#undef glRotatef
	#undef glRotated
	#undef glOrtho
	#undef glFrustum
	#undef glGetFloatv
	#undef glGetDoublev
	#undef glGetIntegerv
	#define glPushAttrib(mask) ::GL::Legacy::PushAttrib(mask)
	#define glPopAttrib() ::GL::Legacy::PopAttrib()
	#define glMatrixMode(mode) ::GL::Legacy::MatrixMode(mode)
	#define glPushMatrix() ::GL::Legacy::PushMatrix()
	#define glPopMatrix() ::GL::Legacy::PopMatrix()
	#define glLoadIdentity() ::GL::Legacy::LoadIdentity()
	#define glLoadMatrixf(matrix) ::GL::Legacy::LoadMatrixf(matrix)
	#define glLoadMatrixd(matrix) ::GL::Legacy::LoadMatrixd(matrix)
	#define glMultMatrixf(matrix) ::GL::Legacy::MultMatrixf(matrix)
	#define glMultMatrixd(matrix) ::GL::Legacy::MultMatrixd(matrix)
	#define glTranslatef(x, y, z) ::GL::Legacy::Translatef(x, y, z)
	#define glTranslated(x, y, z) ::GL::Legacy::Translated(x, y, z)
	#define glScalef(x, y, z) ::GL::Legacy::Scalef(x, y, z)
	#define glScaled(x, y, z) ::GL::Legacy::Scaled(x, y, z)
	#define glRotatef(angle, x, y, z) ::GL::Legacy::Rotatef(angle, x, y, z)
	#define glRotated(angle, x, y, z) ::GL::Legacy::Rotated(angle, x, y, z)
	#define glOrtho(left, right, bottom, top, nearValue, farValue) ::GL::Legacy::Ortho(left, right, bottom, top, nearValue, farValue)
	#define glFrustum(left, right, bottom, top, nearValue, farValue) ::GL::Legacy::Frustum(left, right, bottom, top, nearValue, farValue)
	#define glGetFloatv(pname, values) ::GL::Legacy::GetFloatv(pname, values)
	#define glGetDoublev(pname, values) ::GL::Legacy::GetDoublev(pname, values)
	#define glGetIntegerv(pname, values) ::GL::Legacy::GetIntegerv(pname, values)
#endif
