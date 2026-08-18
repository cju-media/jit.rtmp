#!/usr/bin/env bash
# Verifies that a built jit.rtmp package is genuinely universal (arm64 +
# x86_64) in every binary that matters: each external's main binary, and
# jit.rtmp.server's bundled mediamtx. Exits non-zero with specifics on any
# binary that isn't; prints an OK line per binary and exits 0 otherwise.
#
# Usage: scripts/check-universal.sh [path-to-jit.rtmp-package]
#        (defaults to build/jit.rtmp, i.e. what's actually installed into Max
#        if you've symlinked it per the README)
set -euo pipefail

package_dir="${1:-build/jit.rtmp}"
required_archs=(arm64 x86_64)

if [[ ! -d "$package_dir" ]]; then
    echo "error: no package at $package_dir - build it first (./scripts/build-universal.sh)." >&2
    exit 1
fi

fail=0

check_binary() {
    local bin="$1"
    [[ -f "$bin" ]] || return 0   # e.g. no bundled mediamtx at all - not this script's concern

    local archs
    archs="$(lipo -archs "$bin" 2>/dev/null || true)"
    if [[ -z "$archs" ]]; then
        echo "FAIL: $bin - not a valid Mach-O binary" >&2
        fail=1
        return
    fi

    local missing="" want
    for want in "${required_archs[@]}"; do
        [[ " $archs " == *" $want "* ]] || missing="$missing $want"
    done

    if [[ -n "$missing" ]]; then
        echo "FAIL: $bin is ($archs) - missing:$missing" >&2
        fail=1
    else
        echo "OK:   $bin ($archs)"
    fi
}

shopt -s nullglob
found_any=0
for mxo in "$package_dir"/externals/*.mxo; do
    found_any=1
    for bin in "$mxo"/Contents/MacOS/*; do
        check_binary "$bin"
    done
    check_binary "$mxo/Contents/Resources/mediamtx"
done

if [[ "$found_any" -eq 0 ]]; then
    echo "error: no .mxo externals found under $package_dir/externals" >&2
    exit 1
fi

if [[ "$fail" -ne 0 ]]; then
    echo "error: $package_dir is not fully universal - see FAIL lines above." >&2
    exit 1
fi

echo "$package_dir is universal (arm64 + x86_64) throughout."
