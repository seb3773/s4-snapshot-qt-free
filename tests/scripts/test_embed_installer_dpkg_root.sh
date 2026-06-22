#!/bin/bash
# Integration test: verify embed-installer uses dpkg --root against DEBS_DIR (host glob).
# Default build: stub dpkg --root only (needs root or fakeroot). Full embed-installer is opt-in.
set -euo pipefail

# sudo often strips sbin from PATH; dpkg --root needs ldconfig/start-stop-daemon there.
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${PATH:-}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

BIND_ROOT="$TMP_DIR/bind-root"
DEBS_DIR="$TMP_DIR/debs"
PKG_DIR="$TMP_DIR/pkg-staging"

log() {
    echo "[test-embed-installer-dpkg-root] $*"
}

fail() {
    echo "[test-embed-installer-dpkg-root] FAIL: $*" >&2
    exit 1
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

DPKG="/usr/bin/dpkg"
DPKG_DEB="/usr/bin/dpkg-deb"
[ -x "$DPKG" ] || DPKG="$(command -v dpkg)"
[ -x "$DPKG_DEB" ] || DPKG_DEB="$(command -v dpkg-deb)"
require_cmd "$DPKG_DEB"
require_cmd "$DPKG"

mkdir -p "$BIND_ROOT/var/lib/dpkg/info" "$DEBS_DIR"
touch "$BIND_ROOT/var/lib/dpkg/status"

mkdir -p "$PKG_DIR/usr/share/s4-snapshot/scripts"
cat >"$PKG_DIR/usr/share/s4-snapshot/scripts/configure_debian_calamares.sh" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$PKG_DIR/usr/share/s4-snapshot/scripts/configure_debian_calamares.sh"

mkdir -p "$PKG_DIR/DEBIAN"
cat >"$PKG_DIR/DEBIAN/control" <<EOF
Package: calamares-settings-debian
Version: 99.0-test
Architecture: all
Maintainer: s4-snapshot-test
Description: stub package for embed-installer dpkg --root test
EOF

"$DPKG_DEB" --build "$PKG_DIR" "$DEBS_DIR/calamares-settings-debian_99.0-test_all.deb" >/dev/null

DEB_COUNT="$(find "$DEBS_DIR" -maxdepth 1 -name '*.deb' | wc -l)"
[ "$DEB_COUNT" -ge 1 ] || fail "expected at least one .deb in $DEBS_DIR"

mapfile -t DEBS < <(find "$DEBS_DIR" -maxdepth 1 -name '*.deb' -print)

dpkg_root_install() {
    if [ "$(id -u)" -eq 0 ]; then
        "$DPKG" --root="$BIND_ROOT" -i "${DEBS[@]}"
        return $?
    fi
    if command -v fakeroot >/dev/null 2>&1; then
        log "using fakeroot for dpkg --root (non-root user)"
        fakeroot "$DPKG" --root="$BIND_ROOT" -i "${DEBS[@]}"
        return $?
    fi
    return 2
}

log "installing stub .deb with dpkg --root (same path as embed-installer-debs.sh)"
if dpkg_root_install >/dev/null; then
    rc=0
else
    rc=$?
fi
if [ "$rc" -ne 0 ]; then
    if [ "$rc" -eq 2 ]; then
        log "SKIP dpkg --root (needs root or fakeroot); stub .deb build verified"
        log "PASS (offline checks only)"
        exit 0
    fi
    fail "dpkg --root install failed (exit $rc)"
fi

"$DPKG" --root="$BIND_ROOT" -s calamares-settings-debian >/dev/null \
    || fail "calamares-settings-debian not reported installed under bind-root"

[ -x "$BIND_ROOT/usr/share/s4-snapshot/scripts/configure_debian_calamares.sh" ] \
    || fail "configure script missing in bind-root after dpkg --root install"

if [ "${RUN_ROOT_TESTS:-0}" = "1" ] && [ "$(id -u)" -eq 0 ]; then
    log "RUN_ROOT_TESTS=1: running embed-installer-debs.sh end-to-end (--no-fetch)"
    "$ROOT_DIR/scripts/embed-installer-debs.sh" \
        --bind-root="$BIND_ROOT" \
        --debs-dir="$DEBS_DIR" \
        --suite=bookworm \
        --no-fetch >/dev/null
    "$DPKG" --root="$BIND_ROOT" -s calamares-settings-debian >/dev/null \
        || fail "calamares-settings-debian missing after embed-installer script"
else
    log "SKIP full embed-installer-debs.sh (set RUN_ROOT_TESTS=1 as root to enable)"
fi

log "PASS"
