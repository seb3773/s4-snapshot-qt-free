#!/bin/bash
# Run shell-level integration tests for embed-installer (no C++/Qt required).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

run_test() {
    local script="$1"
    echo "=========================================="
    echo "Shell test: $(basename "$script")"
    echo "=========================================="
    bash "$script"
    echo ""
}

run_test "$ROOT_DIR/tests/scripts/test_installer_debs_cache.sh"

run_test "$ROOT_DIR/tests/scripts/test_embed_installer_dpkg_root.sh"

if [ "${RUN_ROOT_TESTS:-0}" = "1" ]; then
    echo "Note: RUN_ROOT_TESTS=1 enables embed-installer end-to-end inside test_embed_installer_dpkg_root.sh (requires root)."
fi

if [ "${RUN_NETWORK_TESTS:-0}" = "1" ]; then
    NET_CACHE="$(mktemp -d)"
    trap 'rm -rf "$NET_CACHE"' EXIT
    echo "=========================================="
    echo "Network test: fetch-installer-debs.sh"
    echo "=========================================="
    "$ROOT_DIR/scripts/fetch-installer-debs.sh" bookworm "$NET_CACHE"
    DEB_COUNT="$(find "$NET_CACHE" -maxdepth 1 -name '*.deb' | wc -l)"
    if [ "$DEB_COUNT" -eq 0 ]; then
        echo "FAIL: fetch-installer-debs.sh did not download any .deb files" >&2
        exit 1
    fi
    echo "Downloaded $DEB_COUNT .deb file(s) to $NET_CACHE"
    echo ""
fi

echo "All shell tests passed."
