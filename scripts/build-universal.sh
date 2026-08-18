#!/usr/bin/env bash
# Builds jit.rtmp for both arm64 and x86_64 and lipo's the two builds
# together into one universal package, so a single build works on both
# Apple Silicon and Intel Macs. See the "Universal builds" section of
# README.md for the one-time setup this needs.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ARM64_HOMEBREW="${JIT_RTMP_ARM64_HOMEBREW:-/opt/homebrew}"
X86_64_HOMEBREW="${JIT_RTMP_X86_64_HOMEBREW:-/usr/local}"
ARM64_BUILD_DIR="build-arm64.nosync"
X86_64_BUILD_DIR="build-x86_64.nosync"
UNIVERSAL_DIR="build.nosync/jit.rtmp"

for pair in "arm64:$ARM64_HOMEBREW" "x86_64:$X86_64_HOMEBREW"; do
    arch="${pair%%:*}"
    prefix="${pair#*:}"
    if [[ ! -x "$prefix/bin/brew" ]]; then
        cat >&2 <<EOF
error: no Homebrew found at $prefix (needed for the $arch build).

A universal build needs two Homebrew installs: your normal native one, and a
second one under Rosetta for the other architecture. One-time setup for the
Rosetta side (if it's the one missing):

    softwareupdate --install-rosetta --agree-to-license
    arch -x86_64 /bin/bash -c "\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    arch -x86_64 /usr/local/bin/brew install ffmpeg pkg-config

See README.md for details. Override JIT_RTMP_ARM64_HOMEBREW / \
JIT_RTMP_X86_64_HOMEBREW if either Homebrew lives somewhere other than the \
default /opt/homebrew or /usr/local.
EOF
        exit 1
    fi
done

build_arch() {
    local arch="$1" prefix="$2" builddir="$3"
    echo "==> Configuring $arch (FFmpeg from $prefix)"
    rm -rf "$builddir"
    cmake -S . -B "$builddir" \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DJIT_RTMP_HOMEBREW_PREFIX="$prefix"
    echo "==> Building $arch"
    cmake --build "$builddir"
}

build_arch arm64  "$ARM64_HOMEBREW"  "$ARM64_BUILD_DIR"
build_arch x86_64 "$X86_64_HOMEBREW" "$X86_64_BUILD_DIR"

echo "==> Merging into universal package at $UNIVERSAL_DIR"
rm -rf "$UNIVERSAL_DIR"
mkdir -p "$UNIVERSAL_DIR"

# Everything except the compiled externals (symlinked authored folders,
# package-info.json) is architecture-independent - either side's copy is
# identical, so just take arm64's.
for entry in "$ARM64_BUILD_DIR/jit.rtmp"/*; do
    name="$(basename "$entry")"
    [[ "$name" == "externals" ]] && continue
    cp -a "$entry" "$UNIVERSAL_DIR/$name"
done

mkdir -p "$UNIVERSAL_DIR/externals"
for arm64_mxo in "$ARM64_BUILD_DIR/jit.rtmp/externals"/*.mxo; do
    name="$(basename "$arm64_mxo")"
    x86_64_mxo="$X86_64_BUILD_DIR/jit.rtmp/externals/$name"
    if [[ ! -d "$x86_64_mxo" ]]; then
        echo "error: $name was built for arm64 but not for x86_64" >&2
        exit 1
    fi
    echo "==> lipo-merging $name"
    universal_mxo="$UNIVERSAL_DIR/externals/$name"
    cp -a "$arm64_mxo" "$universal_mxo"

    arm64_bin="$(find "$arm64_mxo/Contents/MacOS" -type f)"
    x86_64_bin="$(find "$x86_64_mxo/Contents/MacOS" -type f)"
    universal_bin="$universal_mxo/Contents/MacOS/$(basename "$arm64_bin")"
    lipo -create "$arm64_bin" "$x86_64_bin" -output "$universal_bin"

    # jit.rtmp.server also bundles a real mediamtx binary per-arch - merge
    # that too, so the merged .mxo carries a universal mediamtx as well.
    arm64_mediamtx="$arm64_mxo/Contents/Resources/mediamtx"
    x86_64_mediamtx="$x86_64_mxo/Contents/Resources/mediamtx"
    if [[ -f "$arm64_mediamtx" && -f "$x86_64_mediamtx" ]]; then
        lipo -create "$arm64_mediamtx" "$x86_64_mediamtx" \
            -output "$universal_mxo/Contents/Resources/mediamtx"
    fi

    # lipo invalidates whatever ad-hoc/real signature each single-arch build
    # applied (it's now different bytes) - re-sign the merged bundle so Max
    # will still load it. This repo lives under iCloud-synced ~/Documents
    # (see build.nosync's own comment in .gitignore), and codesign can
    # transiently fail on a bundle iCloud's file provider is still touching
    # right after a `cp -a` - one retry after a beat clears that up without
    # masking a real failure.
    xattr -cr "$universal_mxo"
    if ! codesign -s - -f --deep "$universal_mxo" 2>/tmp/jit-rtmp-codesign-err; then
        echo "codesign failed once, retrying: $(cat /tmp/jit-rtmp-codesign-err)" >&2
        sleep 2
        xattr -cr "$universal_mxo"
        codesign -s - -f --deep "$universal_mxo"
    fi
    rm -f /tmp/jit-rtmp-codesign-err
done

echo "==> Verifying"
# Don't just trust the merge above - confirm every binary that matters
# actually ended up with both architectures before calling this a success.
"$(dirname "${BASH_SOURCE[0]}")/check-universal.sh" "$UNIVERSAL_DIR"
