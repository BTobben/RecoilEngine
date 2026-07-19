#!/usr/bin/env bash

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

status=0

# These extension entry points were promoted to Core before OpenGL 4.1. Apple
# may provide only the unsuffixed symbol in a Core context.
if rg -n --glob '!rts/lib/**' \
  'gl(ActiveTexture|CompressedTexImage[123]D|DrawBuffers|GenerateMipmap|BindFramebuffer|DeleteFramebuffers|GenFramebuffers|CheckFramebufferStatus|FramebufferTexture(1D|2D|3D|Layer)|FramebufferRenderbuffer|BlitFramebuffer|IsFramebuffer|BindRenderbuffer|DeleteRenderbuffers|GenRenderbuffers|IsRenderbuffer|RenderbufferStorage(Multisample)?|GetRenderbufferParameteriv|GetFramebufferAttachmentParameteriv)(ARB|EXT)\s*\(' \
  rts; then
  echo 'error: Core-promoted OpenGL calls still use ARB/EXT entry points' >&2
  status=1
fi

# These operations require OpenGL 4.2/4.3 (or matching extensions). Every
# occurrence must remain behind the engine capability layer.
for symbol in \
  glBindImageTexture glMemoryBarrier glDispatchCompute \
  glShaderStorageBlockBinding glGetActiveAtomicCounterBufferiv \
  glMultiDrawElementsIndirect; do
  matches="$(rg -n --glob '!rts/lib/**' "$symbol" rts || true)"
  if [[ -n "$matches" ]]; then
    echo "audit: $symbol"
    echo "$matches"
  fi
done

exit "$status"
