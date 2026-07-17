/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>

#include <glad/glad.h>

namespace GL::Legacy {

/**
 * glPushAttrib/glPopAttrib were removed from Core profiles.  These wrappers
 * retain the native compatibility-profile stack and emulate the Core-profile
 * draw state that still exists in OpenGL 4.1.
 */
void PushAttrib(GLbitfield mask);
void PopAttrib();

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
	#define glPushAttrib(mask) ::GL::Legacy::PushAttrib(mask)
	#define glPopAttrib() ::GL::Legacy::PopAttrib()
#endif
