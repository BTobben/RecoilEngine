#!/bin/bash

set -euo pipefail

ENGINE_URL="${BAR_ENGINE_URL:-https://github.com/BTobben/RecoilEngine.git}"
ENGINE_REF="${BAR_ENGINE_REF:-agent/macos-gl41-ubo-content}"
ENGINE_UPSTREAM_URL="${BAR_ENGINE_UPSTREAM_URL:-https://github.com/beyond-all-reason/RecoilEngine.git}"
BAR_URL="${BAR_CONTENT_URL:-https://github.com/BTobben/Beyond-All-Reason.git}"
BAR_REF="${BAR_CONTENT_REF:-agent/macos-gl41-ubo-content}"
WORKSPACE="${BAR_MACOS_WORKSPACE:-$HOME/BAR-macOS-GL41}"
BUILD_JOBS="${BAR_BUILD_JOBS:-3}"
RUN_SMOKE=1
CREATE_DMG=0
INSTALL_DEPS=1

usage() {
	cat <<'EOF'
Usage: ./macos-build-bar.sh [options]

Build an experimental BAR OpenGL 4.1 app on macOS.

  --dmg             Also create a compressed development DMG.
  --skip-test       Build/package without running the GL 4.1 smoke test.
  --no-install-deps Fail instead of installing missing Homebrew dependencies.
  --workspace PATH  Put sources, build files, and output under PATH.
  -h, --help        Show this help.

Environment overrides:
  BAR_BUILD_JOBS, BAR_ENGINE_URL, BAR_ENGINE_REF, BAR_ENGINE_UPSTREAM_URL,
  BAR_CONTENT_URL, BAR_CONTENT_REF, BAR_MACOS_WORKSPACE
EOF
}

log() {
	printf '\n==> %s\n' "$*"
}

die() {
	printf '\nERROR: %s\n' "$*" >&2
	exit 1
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--dmg) CREATE_DMG=1 ;;
		--skip-test) RUN_SMOKE=0 ;;
		--no-install-deps) INSTALL_DEPS=0 ;;
		--workspace)
			[ "$#" -ge 2 ] || die "--workspace requires a path"
			WORKSPACE="$2"
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*) die "unknown option: $1" ;;
	esac
	shift
done

[ "$(uname -s)" = "Darwin" ] || die "this bootstrap must run on macOS"

mkdir -p "$WORKSPACE" "$WORKSPACE/cache" "$WORKSPACE/build" "$WORKSPACE/dist"

APP_RUNTIME="$WORKSPACE/dist/Beyond All Reason GL41.app/Contents/Resources/runtime/spring"
if pgrep -f "$APP_RUNTIME" >/dev/null 2>&1; then
	die "Beyond All Reason GL41 is still running. Quit the app completely before rebuilding so the next launch cannot reuse the old engine process."
fi

ensure_command_line_tools() {
	if xcode-select -p >/dev/null 2>&1 && xcrun --find clang >/dev/null 2>&1; then
		return
	fi

	[ "$INSTALL_DEPS" -eq 1 ] || die "Apple Command Line Tools are missing; run: xcode-select --install"
	log "Apple Command Line Tools are missing"
	xcode-select --install >/dev/null 2>&1 || true
	printf '%s\n' \
		"macOS should now show Apple's installer." \
		"Finish that installation, then press Return here to continue."
	read -r _

	xcode-select -p >/dev/null 2>&1 || die "Command Line Tools are still unavailable"
	xcrun --find clang >/dev/null 2>&1 || die "Clang is still unavailable through xcrun"
}

load_homebrew() {
	if command -v brew >/dev/null 2>&1; then
		return
	fi
	if [ -x /opt/homebrew/bin/brew ]; then
		eval "$(/opt/homebrew/bin/brew shellenv)"
	elif [ -x /usr/local/bin/brew ]; then
		eval "$(/usr/local/bin/brew shellenv)"
	fi
}

ensure_homebrew() {
	load_homebrew
	if command -v brew >/dev/null 2>&1; then
		return
	fi

	[ "$INSTALL_DEPS" -eq 1 ] || die "Homebrew is missing; see https://brew.sh"
	log "Installing Homebrew from its official installer"
	HOMEBREW_INSTALLER="$WORKSPACE/cache/homebrew-install.sh"
	curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o "$HOMEBREW_INSTALLER"
	/bin/bash "$HOMEBREW_INSTALLER"
	load_homebrew
	command -v brew >/dev/null 2>&1 || die "Homebrew installed but is not available on PATH"
}

ensure_brew_dependencies() {
	local packages missing package
	packages="ccache cmake ninja pkgconf sdl2-compat devil fontconfig freetype expat xz sevenzip git-lfs openal-soft libogg libvorbis"
	missing=""
	for package in $packages; do
		if ! brew list --versions "$package" >/dev/null 2>&1; then
			missing="$missing $package"
		fi
	done

	if [ -n "$missing" ]; then
		[ "$INSTALL_DEPS" -eq 1 ] || die "missing Homebrew packages:$missing"
		log "Installing missing build dependencies:$missing"
		brew install $missing
	fi

	git lfs install --skip-repo >/dev/null
}

repo_is_clean() {
	git -C "$1" diff --quiet && git -C "$1" diff --cached --quiet
}

prepare_engine() {
	local script_root
	if [ -n "${BASH_SOURCE[0]:-}" ]; then
		script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd || true)"
	else
		# BASH_SOURCE is unset when this script is executed through `curl | bash`.
		script_root=""
	fi

	if [ -f "$script_root/CMakeLists.txt" ] && [ -e "$script_root/.git" ]; then
		ENGINE_ROOT="$script_root"
	else
		ENGINE_ROOT="$WORKSPACE/RecoilEngine"
	fi

	if [ ! -e "$ENGINE_ROOT/.git" ]; then
		log "Cloning RecoilEngine ($ENGINE_REF)"
		git clone --branch "$ENGINE_REF" --single-branch "$ENGINE_URL" "$ENGINE_ROOT"
	else
		# Keep the checkout reusable when our temporary Intel-macOS pr-downloader
		# compatibility patch was left applied by an earlier build.
		if [ -e "$ENGINE_ROOT/tools/pr-downloader/.git" ] && \
			git -C "$ENGINE_ROOT/tools/pr-downloader" apply --reverse --check \
				"$ENGINE_ROOT/installer/Mac/pr-downloader-macos-x64.patch" >/dev/null 2>&1; then
			git -C "$ENGINE_ROOT/tools/pr-downloader" apply --reverse \
				"$ENGINE_ROOT/installer/Mac/pr-downloader-macos-x64.patch"
		fi
		repo_is_clean "$ENGINE_ROOT" || die "RecoilEngine has local changes: $ENGINE_ROOT"
		log "Updating RecoilEngine ($ENGINE_REF)"
		git -C "$ENGINE_ROOT" fetch origin "$ENGINE_REF"
		git -C "$ENGINE_ROOT" checkout "$ENGINE_REF"
		git -C "$ENGINE_ROOT" merge --ff-only "origin/$ENGINE_REF"
	fi

	log "Fetching upstream RecoilEngine version tags"
	if [ "$(git -C "$ENGINE_ROOT" rev-parse --is-shallow-repository)" = "true" ]; then
		git -C "$ENGINE_ROOT" fetch --no-recurse-submodules --unshallow --force --tags "$ENGINE_UPSTREAM_URL" master
	else
		git -C "$ENGINE_ROOT" fetch --no-recurse-submodules --force --tags "$ENGINE_UPSTREAM_URL" master
	fi
	git -C "$ENGINE_ROOT" describe --tags --match '[0-9]*' --long >/dev/null \
		|| die "no numeric upstream RecoilEngine version tag is reachable from $ENGINE_REF"

	git -C "$ENGINE_ROOT" submodule update --init --recursive

	# The pinned upstream downloader currently identifies Apple Silicon only.
	# Apply the small x86_64 platform addition until that change is available in
	# the submodule itself; this keeps Intel macOS builds correctly labelled.
	if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "x86_64" ]; then
		git -C "$ENGINE_ROOT/tools/pr-downloader" apply --check \
			"$ENGINE_ROOT/installer/Mac/pr-downloader-macos-x64.patch"
		git -C "$ENGINE_ROOT/tools/pr-downloader" apply \
			"$ENGINE_ROOT/installer/Mac/pr-downloader-macos-x64.patch"
	fi
}

prepare_bar_content() {
	APP="$WORKSPACE/dist/Beyond All Reason GL41.app"
	CONTENTS="$APP/Contents"
	RESOURCES="$CONTENTS/Resources"
	BAR_ROOT="$RESOURCES/BAR-content"
	BAR_GIT_DIR="$WORKSPACE/cache/bar-content.git"
	RUNTIME_ROOT="$RESOURCES/runtime"

	mkdir -p "$CONTENTS/MacOS" "$RESOURCES"

	if [ ! -e "$BAR_ROOT/.git" ]; then
		[ ! -e "$BAR_ROOT" ] || die "BAR content path exists but is not managed by Git: $BAR_ROOT"
		log "Cloning BAR content ($BAR_REF); Git LFS can make this download large"
		git clone --depth 1 --branch "$BAR_REF" --single-branch \
			--separate-git-dir "$BAR_GIT_DIR" "$BAR_URL" "$BAR_ROOT"
	else
		repo_is_clean "$BAR_ROOT" || die "BAR content has local changes: $BAR_ROOT"
		log "Updating BAR content ($BAR_REF)"
		git -C "$BAR_ROOT" fetch origin "$BAR_REF"
		git -C "$BAR_ROOT" checkout "$BAR_REF"
		git -C "$BAR_ROOT" merge --ff-only "origin/$BAR_REF"
	fi

	git -C "$BAR_ROOT" lfs pull origin "$BAR_REF"
	[ -f "$BAR_ROOT/common/configs/macos-gl41.cfg" ] || die "paired BAR macOS config is missing"
}

configure_and_build() {
	local brew_prefix cmake_prefix openal_prefix pkg_path
	brew_prefix="$(brew --prefix)"
	openal_prefix="$(brew --prefix openal-soft)"
	cmake_prefix="$brew_prefix;$(brew --prefix expat);$(brew --prefix openal-soft);$(brew --prefix libogg);$(brew --prefix libvorbis)"
	pkg_path="$brew_prefix/lib/pkgconfig:$(brew --prefix expat)/lib/pkgconfig:$(brew --prefix libogg)/lib/pkgconfig:$(brew --prefix libvorbis)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

	BUILD_ROOT="$WORKSPACE/build/RecoilEngine"
	log "Configuring native RecoilEngine build"
	CMAKE_PREFIX_PATH="$cmake_prefix" PKG_CONFIG_PATH="$pkg_path" \
		cmake -S "$ENGINE_ROOT" -B "$BUILD_ROOT" -G Ninja \
			-DCMAKE_BUILD_TYPE=RELEASE \
			-DCMAKE_INSTALL_PREFIX="$RUNTIME_ROOT" \
			-DCMAKE_PREFIX_PATH="$cmake_prefix" \
			-DOPENAL_INCLUDE_DIR="$openal_prefix/include/AL" \
			-DOPENAL_LIBRARY="$openal_prefix/lib/libopenal.dylib" \
			-DCMAKE_C_COMPILER_LAUNCHER=ccache \
			-DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
			-DMACOSX_BUNDLE=OFF \
			-DINSTALL_PORTABLE=ON \
			-DPREFER_STATIC_LIBS=OFF \
			-DNO_SOUND=OFF \
			-DENABLE_STREFLOP=ON \
			-DAI_TYPES=NONE \
			-DBUILD_spring-dedicated=OFF \
			-DBUILD_spring-headless=OFF

	log "Building and installing RecoilEngine"
	cmake --build "$BUILD_ROOT" --target install-spring-legacy --parallel "$BUILD_JOBS"
	[ -x "$RUNTIME_ROOT/spring" ] || die "installed engine executable was not produced"
}

write_app_bundle() {
	local launcher plist
	launcher="$CONTENTS/MacOS/Beyond All Reason GL41"
	plist="$CONTENTS/Info.plist"

	cp "$ENGINE_ROOT/installer/Mac/bar-gl41-launcher.sh" "$launcher"
	chmod +x "$launcher"
	cp "$ENGINE_ROOT/installer/Mac/BAR-GL41-Info.plist" "$plist"

	plutil -lint "$plist" >/dev/null
	codesign --force --deep --sign - "$APP" >/dev/null
}

run_smoke_test() {
	local smoke_dir smoke_config
	smoke_dir="$WORKSPACE/smoke-write"
	smoke_config="$WORKSPACE/build/gl41-smoke.cfg"
	mkdir -p "$smoke_dir"
	cp "$BAR_ROOT/common/configs/macos-gl41.cfg" "$smoke_config"
	printf '%s\n' \
		'XResolutionWindowed=128' \
		'YResolutionWindowed=128' \
		'WindowPosX=32' \
		'WindowPosY=32' \
		>> "$smoke_config"

	log "Running the native OpenGL 4.1 startup/render smoke test"
	SPRING_DATADIR="$RUNTIME_ROOT:$BAR_ROOT" "$RUNTIME_ROOT/spring" \
		--gl-smoke-test \
		--window \
		--isolation \
		--isolation-dir="$RUNTIME_ROOT" \
		--write-dir="$smoke_dir" \
		--config="$smoke_config"

	grep -F '[GLSmoke] PASS context=4.1 Core' "$smoke_dir/infolog.txt" >/dev/null \
		|| die "the engine exited without the expected GL4.1 PASS marker"
}

create_dmg() {
	local dmg
	dmg="$WORKSPACE/Beyond-All-Reason-GL41-experimental.dmg"
	log "Creating compressed development DMG"
	hdiutil create \
		-volname "Beyond All Reason GL41" \
		-srcfolder "$WORKSPACE/dist" \
		-ov -format UDZO "$dmg"
	printf 'DMG: %s\n' "$dmg"
}

ensure_command_line_tools
ensure_homebrew
ensure_brew_dependencies
prepare_engine
prepare_bar_content
configure_and_build
write_app_bundle

if [ "$RUN_SMOKE" -eq 1 ]; then
	run_smoke_test
fi
if [ "$CREATE_DMG" -eq 1 ]; then
	create_dmg
fi

log "Done"
printf '%s\n' \
	"App: $APP" \
	"Open it from Finder, or run:" \
	"  open \"$APP\"" \
	"Smoke log: $WORKSPACE/smoke-write/infolog.txt"
