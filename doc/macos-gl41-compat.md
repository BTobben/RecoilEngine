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

## Known limitations

This foundation does **not** yet make the graphical engine playable on macOS.
The engine and Lua drawing API still contain compatibility-profile operations
such as immediate-mode drawing, matrix stacks, attribute stacks, alpha test,
and client-state vertex arrays. Those entry points are removed from a 4.1 Core
context and must be migrated to the existing buffered/shader drawing helpers
before a macOS graphical smoke test can pass.

Additional work remains outside this capability layer:

- audit BAR game shaders and widgets and disable or replace optional 4.2/4.3
  effects;
- complete and continuously test the native macOS dependency/build pipeline;
- package, sign, and notarize an application bundle;
- prove replay and multiplayer determinism across x86-64 and ARM64.

The current ARM64/NEON build support is separate from renderer compatibility.
Compiling on ARM64 is not proof of cross-architecture lockstep parity.

## Validation

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
`ForceDisableGL4=1`. A real macOS 4.1 Core test remains required for every
compatibility-profile migration.

## Long-term direction

OpenGL 4.1 is a compatibility bridge, not the preferred permanent renderer.
A renderer boundary that consumes explicit capabilities also prepares the
engine for a future Vulkan backend. On macOS that backend can be evaluated
through MoltenVK/Metal after the OpenGL fallback and determinism work are
measurable independently.
