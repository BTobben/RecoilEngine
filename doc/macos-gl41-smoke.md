# macOS OpenGL 4.1 startup/render smoke test

The graphical engine has a dedicated `--gl-smoke-test` startup mode. It takes
the normal `SpringApp` path through native window/context creation, renderer
initialization, fonts, filesystem setup, Lua drawing initialization, and the
other core subsystems. Before loading a menu or game it performs a bounded
render test and exits.

The test is deliberately stricter than “the process opened a window”:

1. require an OpenGL 4.1-or-newer Core context;
2. verify that startup left no OpenGL errors;
3. require the engine-owned Core-profile attribute and matrix stacks;
4. compile and link `#version 410 core` vertex and fragment shaders;
5. bind a `std140` UBO without GLSL `layout(binding=...)` through
   `glUniformBlockBinding`;
6. create a VAO/VBO and RGBA8 framebuffer;
7. render a known full-screen triangle whose color comes from that UBO;
8. read its center pixel with `glReadPixels` and validate the color;
9. verify that draw state, transforms, and stack depths were restored; and
10. exit zero only when every check passed.

This explicitly exercises the OpenGL 4.1 fallback used when
`ARB_shading_language_420pack` is unavailable. Drivers exposing 420pack keep
using explicit layout bindings.

A successful log contains a machine-readable line similar to:

```text
[GLSmoke] PASS context=4.1 Core renderer="Apple M1" pixel=<255,64,0,255> uboBindingFallback=1 stateRestored=1 matrixStack=1
```

## GitHub Actions

`.github/workflows/macos-gl41-smoke.yml` builds and runs the graphical engine
on GitHub's standard Apple Silicon `macos-15` runner. This is a real M1 macOS
host and not a Linux cross-compile. The job requests 4.1 Core explicitly and
uploads the stdout log, `infolog.txt`, configuration, and CMake diagnostics
even after a failure.

The smoke build intentionally disables sound, native AIs, and non-graphical
engine variants. It keeps the ARM64/NEON streflop path enabled so the graphical
binary is built with the normal synchronized-math integration. Passing this
startup test is still not proof of replay or cross-architecture multiplayer
determinism.

## Local run

Install the native dependencies with Homebrew:

```sh
brew install cmake ninja pkgconf sdl2-compat devil fontconfig freetype expat xz
```

Configure and build from the repository root:

```sh
cmake -S . -B build-macos-gl41 -G Ninja \
  -DCMAKE_BUILD_TYPE=RELEASE \
  -DCMAKE_PREFIX_PATH="$(brew --prefix);$(brew --prefix expat)" \
  -DMACOSX_BUNDLE=OFF \
  -DINSTALL_PORTABLE=ON \
  -DPREFER_STATIC_LIBS=OFF \
  -DNO_SOUND=ON \
  -DENABLE_STREFLOP=ON \
  -DAI_TYPES=NONE \
  -DBUILD_spring-dedicated=OFF \
  -DBUILD_spring-headless=OFF

cmake --build build-macos-gl41 --target engine-legacy --parallel
```

Create a writable directory and a minimal config containing
`ForceCoreContext=1`, `GLContextMajorVersion=4`,
`GLContextMinorVersion=1`, `ForceDisableGL4=1`, and windowed 128x128 geometry.
Then run:

```sh
SPRING_DATADIR="$PWD/build-macos-gl41" \
  ./build-macos-gl41/spring \
  --gl-smoke-test \
  --window \
  --isolation \
  --isolation-dir="$PWD/build-macos-gl41" \
  --write-dir="$PWD/build-macos-gl41/smoke-write" \
  --config="$PWD/build-macos-gl41/gl41-smoke.cfg"
```

This test proves native startup, Core-context creation, a minimal buffered
GLSL draw, framebuffer readback, and migrated attribute state. It does not yet
prove that every gameplay renderer or BAR Lua widget is free of remaining
fixed-function calls.
