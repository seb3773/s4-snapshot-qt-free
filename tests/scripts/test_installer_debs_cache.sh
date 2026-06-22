#!/bin/bash
# Verify installer-debs cache completeness helpers reject partial caches.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../scripts/installer-debs-cache.sh
. "$ROOT_DIR/scripts/installer-debs-cache.sh"

fail() {
    echo "[test-installer-debs-cache] FAIL: $*" >&2
    exit 1
}

log() {
    echo "[test-installer-debs-cache] $*"
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

log "empty cache must be incomplete"
installer_debs_cache_is_complete "$TMP" && fail "empty dir must not be complete"

log "partial cache without calamares-settings-debian must be incomplete"
touch "$TMP/unrelated_1.0_amd64.deb"
installer_debs_cache_is_complete "$TMP" && fail "partial cache must not be complete"

log "cache with stub calamares-settings-debian .deb must be complete"
cat >"$TMP/calamares-settings-debian_1.0_all.deb" <<'EOF'
not a real deb
EOF
installer_debs_cache_is_complete "$TMP" || fail "cache with required package name must be complete"

log "all checks passed"
