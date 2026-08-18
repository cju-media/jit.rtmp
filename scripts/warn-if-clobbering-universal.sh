#!/usr/bin/env bash
# Warns (without failing the build) if this rebuild is about to replace a
# universal (fat) binary - assembled by scripts/build-universal.sh - with a
# single-arch one, which a plain `cmake --build` does silently otherwise.
# Purely informational: doesn't touch anything, never exits non-zero.
set -euo pipefail

bundle="$1"
target_archs="$2"   # CMAKE_OSX_ARCHITECTURES, e.g. "arm64" or "arm64;x86_64"
target_set=" ${target_archs//;/ } "

check_binary() {
    local bin="$1"
    [[ -f "$bin" ]] || return 0

    local current_archs
    current_archs="$(lipo -archs "$bin" 2>/dev/null || true)"
    [[ -n "$current_archs" ]] || return 0

    local current_count
    current_count=$(wc -w <<< "$current_archs")
    [[ "$current_count" -gt 1 ]] || return 0   # already thin, nothing to lose

    local missing="" arch
    for arch in $current_archs; do
        [[ "$target_set" == *" $arch "* ]] || missing="$missing $arch"
    done

    if [[ -n "$missing" ]]; then
        echo "warning: $(basename "$bundle")/$(basename "$bin") is currently universal ($current_archs) but this build only targets (${target_archs//;/, }) - about to be replaced with a thin binary, losing:$missing. Run ./scripts/build-universal.sh afterward to restore the universal package." >&2
    fi
}

for bin in "$bundle"/Contents/MacOS/*; do
    check_binary "$bin"
done
check_binary "$bundle/Contents/Resources/mediamtx"
