/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <string>

namespace GL {

/**
 * Exercise the initialized engine context with a GLSL 4.10 Core draw into an
 * FBO and verify the result through glReadPixels. The caller owns startup and
 * shutdown; this function restores all state and object bindings it changes.
 */
[[nodiscard]] bool RunStartupRenderSmokeTest(std::string& report);

} // namespace GL
