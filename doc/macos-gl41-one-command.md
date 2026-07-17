# One-command BAR build on macOS

The experimental OpenGL 4.1 branches include a bootstrap that downloads the
paired engine and BAR content, installs missing build dependencies, compiles a
native engine, creates a Finder-launchable app, and runs the startup/render
smoke test.

## Fastest route

Open Terminal on the Mac and run:

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/BTobben/RecoilEngine/agent/macos-gl41-ubo-content/macos-build-bar.sh)"
```

The default workspace is `~/BAR-macOS-GL41`. The final application is:

```text
~/BAR-macOS-GL41/dist/Beyond All Reason GL41.app
```

On a new Mac, the same command opens Apple's Command Line Tools installer and
waits for the user to finish it. It then installs Homebrew, if necessary, and
only the missing formulae. Full Xcode is not required when the standalone
Command Line Tools provide `clang` and the macOS SDK.

The BAR checkout uses Git LFS and can be a large download. The app contains
the installed engine, engine runtime data, BAR content, and the paired
`common/configs/macos-gl41.cfg`. It is ad-hoc signed for local development; it
is not notarized for redistribution.

## Inspect before running

For users who do not want to execute a downloaded script directly:

```sh
git clone --branch agent/macos-gl41-ubo-content \
  https://github.com/BTobben/RecoilEngine.git
cd RecoilEngine
./macos-build-bar.sh
```

Useful options:

```sh
./macos-build-bar.sh --dmg
./macos-build-bar.sh --skip-test
./macos-build-bar.sh --workspace "$HOME/my-bar-build"
```

`--dmg` creates a large development disk image containing the self-contained
app. For building and immediately testing on the same Mac, the `.app` is the
faster output. A generally redistributable build would additionally need
dependency bundling, Developer ID signing, notarization, and testing on a
clean second Mac.

## What the smoke test proves

The default run requests a native OpenGL 4.1 Core context, loads the paired
BAR data path and configuration, exercises the program-bound UBO fallback,
draws through a VAO/VBO into an FBO, validates the read-back pixel, and checks
state restoration. A pass is recorded in:

```text
~/BAR-macOS-GL41/smoke-write/infolog.txt
```

This is still an experimental port. Passing the bounded startup test does not
prove that every BAR widget, map, unit shader, sound path, or complete match is
working.
