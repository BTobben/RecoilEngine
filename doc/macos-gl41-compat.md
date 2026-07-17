# macOS OpenGL 4.1 compatibility status

Recoil's experimental macOS OpenGL path targets an OpenGL 4.1 Core context.
Apple's system OpenGL does not expose OpenGL 4.2 or 4.3, so the enhanced GL4
renderer cannot be selected there in its current form.

## Context and capability selection

On macOS the default requested context is 4.1 Core. An explicit
`GLContextMajorVersion`/`GLContextMinorVersion` override is still respected,
but context creation only accepts a Core-profile fallback on macOS. Other
platforms retain their compatibility-profile preference unless
`ForceCoreContext` is enabled.

The renderer publishes individual capabilities instead of requiring callers
to infer them from `haveGL4`:

- `supportComputeShaders`
- `supportShaderStorageBuffers`
- `supportImageLoadStore`
- `supportAtomicCounterBuffers`
- `supportMultiDrawIndirect`
- `supportGL41Core`

The same values are exposed to Lua as `Platform.glSupport*` fields. Calls that
need compute shaders or image load/store are not registered when unavailable,
and their implementations also reject unsupported use defensively.

`ForceDisableGL4=1` disables the OpenGL 4.2/4.3 feature tier, including the
capabilities above, while leaving the ordinary OpenGL renderer available. It
is the preferred way to exercise degraded feature selection on a machine that
normally exposes OpenGL 4.3 or newer.

## Existing fallback

The enhanced model renderer requires both shader-storage buffers and
multi-draw indirect. When those are unavailable, `haveGL4` remains false and
Recoil selects its existing GLSL model renderer. `ModelsDataUploader` then
does not create or bind its SSBO streams. This is a real fallback path; no
unsupported operation is reported as successful.

VBO range alignment no longer queries SSBO-only state on an unsupported
context. Lua VBO clearing also has a mapped-buffer fallback when
`glClearBufferData` is unavailable.

The baseline Lua VAO API remains available without multi-draw indirect, which
allows ordinary and instanced draws on OpenGL 4.1. `VAO:Submit()` reports
multi-draw indirect as unavailable, and draw requests with a non-zero base
instance report the missing OpenGL 4.2 capability instead of calling a null
entry point.

## Core-profile legacy draw state

Existing `glPushAttrib`/`glPopAttrib` call sites are routed through a
profile-aware stack. Compatibility contexts keep using the native OpenGL
stack. Core contexts use an engine-owned stack that snapshots and restores
the OpenGL 4.1 state which still exists: enable, color, depth, stencil,
polygon, viewport, scissor, multisample, texture-binding, line, and point
state. Nested pushes are supported and stack underflow is logged instead of
dereferencing an unavailable entry point.

Removed fixed-function groups such as current color, lighting, fog, matrix
transform, display-list, and pixel-transfer state cannot be represented in a
Core context. A request containing those bits emits a one-time warning; it
does not claim that removed state was restored. Those callers still need to
move their data to shaders and buffered geometry.

Startup no longer requires compatibility-only texture-environment support,
does not call `glShadeModel` in a Core context, and avoids removed Core-profile
limit queries. The splash renderer also skips the obsolete texture-target
enable while retaining its shader texture binding.

The buffered startup and font shaders select GLSL 4.10 Core sources instead
of compatibility-profile built-ins. Their transitional Core transform is an
identity uniform, which is correct for the normalized splash path and keeps
startup testable. It is not a replacement for the remaining world/UI matrix
stack migration.

## Known limitations

This foundation does **not** yet make the graphical engine playable on macOS.
The attribute stack has a Core-safe implementation, but the engine and Lua
drawing API still contain compatibility-profile operations such as
immediate-mode drawing, matrix stacks, alpha test, and client-state vertex
arrays. Those entry points are removed from a 4.1 Core context and must be
migrated to the existing buffered/shader drawing helpers.

Additional work remains outside this capability layer:

- audit BAR game shaders and widgets and disable or replace optional 4.2/4.3
  effects;
- complete and continuously test the native macOS dependency/build pipeline;
- package, sign, and notarize an application bundle;
- prove replay and multiplayer determinism across x86-64 and ARM64.

The current ARM64/NEON build support is separate from renderer compatibility.
Compiling on ARM64 is not proof of cross-architecture lockstep parity.

## Validation

The native `--gl-smoke-test` mode and macOS Actions job are documented in
[`macos-gl41-smoke.md`](macos-gl41-smoke.md). They exercise the real graphical
engine startup, GLSL 4.10 compilation, a VAO/VBO draw, FBO readback, and Core
attribute-state restoration.

Useful local checks are:

```sh
rg -n --glob '!rts/lib/**' \
  'glDispatchCompute|GL_COMPUTE_SHADER|glBindImageTexture|GL_SHADER_STORAGE_BUFFER|GL_ATOMIC_COUNTER_BUFFER' rts

rg -n \
  'supportComputeShaders|supportShaderStorageBuffers|supportImageLoadStore|supportAtomicCounterBuffers|supportMultiDrawIndirect|haveGL4|ForceDisableGL4' \
  rts
```

A Linux build verifies that the existing GL4.3 path still compiles. Runtime
validation should cover both the normal configuration and
`ForceDisableGL4=1`. The macOS smoke job provides a repeatable Core 4.1 gate;
full menu, game, and BAR widget coverage remains a separate milestone.

## Long-term direction

OpenGL 4.1 is a compatibility bridge, not the preferred permanent renderer.
A renderer boundary that consumes explicit capabilities also prepares the
engine for a future Vulkan backend. On macOS that backend can be evaluated
through MoltenVK/Metal after the OpenGL fallback and determinism work are
measurable independently.
