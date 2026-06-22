#!/bin/bash
# Install Calamares into a bind-root chroot from the per-user .deb cache.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=installer-debs-cache.sh
. "$SCRIPT_DIR/installer-debs-cache.sh"

BIND_ROOT=""
DEBS_DIR=""
SUITE="bookworm"
FETCH_MISSING=true

APT_GET="/usr/bin/apt-get"
DPKG="/usr/bin/dpkg"

usage() {
    cat <<EOF
usage: $(basename "$0") --bind-root=PATH [options]

Inject Calamares into a snapshot bind-root chroot from cached .deb files.
The host installed system is not modified.

Options:
  --bind-root=PATH     Bind-root overlay path (required)
  --debs-dir=PATH      Override .deb cache directory (optional)
  --suite=NAME         Cache subdirectory name (default: bookworm)
  --fetch-missing      Download packages when cache is empty (default)
  --no-fetch           Fail if cache is empty (offline/air-gapped without pre-fetch)
  -h, --help           Show this help

Default cache: ~/.cache/s4-snapshot/installer-debs/<suite>/
First use requires network access unless installer_debs_dir is pre-populated.

EOF
}

log() {
    echo "[embed-installer] $*"
}

die() {
    echo "[embed-installer] ERROR: $*" >&2
    exit 1
}

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        die "must run as root"
    fi
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --bind-root=*)
                BIND_ROOT="${1#*=}"
                ;;
            --debs-dir=*)
                DEBS_DIR="${1#*=}"
                ;;
            --suite=*)
                SUITE="${1#*=}"
                ;;
            --fetch-missing)
                FETCH_MISSING=true
                ;;
            --no-fetch)
                FETCH_MISSING=false
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                die "unknown argument: $1"
                ;;
        esac
        shift
    done

    if [ -z "$BIND_ROOT" ]; then
        die "--bind-root is required"
    fi
    if [ ! -d "$BIND_ROOT" ]; then
        die "bind-root does not exist: $BIND_ROOT"
    fi
}

resolve_debs_dir() {
    if [ -n "$DEBS_DIR" ]; then
        installer_debs_ensure_cache_dir "$DEBS_DIR"
        return
    fi

    DEBS_DIR="$(installer_debs_default_cache_dir "$SUITE")"
}

count_debs() {
    find "${1:-$DEBS_DIR}" -maxdepth 1 -name '*.deb' 2>/dev/null | wc -l
}

maybe_fetch_debs() {
    resolve_debs_dir
    installer_debs_ensure_cache_dir "$DEBS_DIR"

    if installer_debs_cache_is_complete; then
        log "using complete cache: $(count_debs) .deb file(s) in $DEBS_DIR"
        return
    fi

    if [ "$(count_debs)" -gt 0 ]; then
        log "cache incomplete in $DEBS_DIR (missing required packages) — re-fetching"
    fi

    if [ "$FETCH_MISSING" != true ]; then
        die "installer cache incomplete in $DEBS_DIR (pre-fetch with scripts/fetch-installer-debs.sh or enable fetch)"
    fi

    log "downloading packages (network required, host apt sources, suite=$SUITE)"
    "$SCRIPT_DIR/fetch-installer-debs.sh" "$SUITE" "$DEBS_DIR"

    if ! installer_debs_cache_is_complete; then
        die "cache still incomplete after fetch in $DEBS_DIR (required: calamares-settings-debian and dependencies)"
    fi
}

mount_chroot_fs() {
    mkdir -p "$BIND_ROOT/proc" "$BIND_ROOT/sys" "$BIND_ROOT/dev"
    mountpoint -q "$BIND_ROOT/proc" || mount --bind /proc "$BIND_ROOT/proc"
    mountpoint -q "$BIND_ROOT/sys"  || mount --bind /sys  "$BIND_ROOT/sys"
    mountpoint -q "$BIND_ROOT/dev"  || mount --bind /dev  "$BIND_ROOT/dev"
    if [ -f /etc/resolv.conf ]; then
        cp -f /etc/resolv.conf "$BIND_ROOT/etc/resolv.conf"
    fi
}

umount_chroot_fs() {
    for d in dev sys proc; do
        if mountpoint -q "$BIND_ROOT/$d" 2>/dev/null; then
            umount "$BIND_ROOT/$d" || true
        fi
    done
}

install_debs_in_chroot() {
    if [ ! -x "$DPKG" ]; then
        die "$DPKG not found"
    fi

    local debs=( "$DEBS_DIR"/*.deb )
    if [ ${#debs[@]} -eq 0 ] || [ ! -e "${debs[0]}" ]; then
        die "no .deb files available in $DEBS_DIR"
    fi

    log "installing ${#debs[@]} .deb file(s) into $BIND_ROOT"

    local dpkg_log
    dpkg_log="$(mktemp)"
    if ! "$DPKG" --root="$BIND_ROOT" -i "${debs[@]}" >"$dpkg_log" 2>&1; then
        log "dpkg --root install failed (see below)"
        sed 's/^/[embed-installer] dpkg: /' "$dpkg_log" >&2 || true
        rm -f "$dpkg_log"
        die "dpkg --root install failed; ensure the cache contains all dependencies at $DEBS_DIR"
    fi
    rm -f "$dpkg_log"

    if [ -x "$APT_GET" ]; then
        local apt_log
        apt_log="$(mktemp)"
        if ! chroot "$BIND_ROOT" "$APT_GET" \
            -o Dir::Etc::sourcelist=/dev/null \
            -o Dir::Etc::sourceparts=/dev/null \
            install -f -y >"$apt_log" 2>&1; then
            log "apt-get -f in chroot could not fix all dependencies (offline; no apt sources in chroot)"
            sed 's/^/[embed-installer] apt: /' "$apt_log" >&2 || true
        fi
        rm -f "$apt_log"
    fi

    if ! "$DPKG" --root="$BIND_ROOT" -s calamares-settings-debian >/dev/null 2>&1; then
        die "calamares-settings-debian is not installed in bind-root after offline dpkg install. "
            "All dependencies must already be present in the cache at $DEBS_DIR "
            "(${#debs[@]} .deb file(s) found). "
            "Re-download with network: scripts/fetch-installer-debs.sh $SUITE, "
            "or run: apt-get install --download-only calamares-settings-debian into that directory. "
            "Note: apt-get install -f inside the chroot cannot fetch missing packages without apt sources."
    fi
}

configure_calamares_in_chroot() {
    local cfg="/usr/share/s4-snapshot/scripts/configure_debian_calamares.sh"
    if chroot "$BIND_ROOT" test -x "$cfg"; then
        log "running $cfg in chroot"
        chroot "$BIND_ROOT" dash "$cfg" || die "configure_debian_calamares.sh failed in bind-root"
        return
    fi

    local runtime_cfg="$SCRIPT_DIR/configure_debian_calamares.sh"
    if [ -f "$runtime_cfg" ]; then
        log "copying configure script into chroot"
        mkdir -p "$BIND_ROOT/usr/share/s4-snapshot/scripts"
        cp -f "$runtime_cfg" "$BIND_ROOT/usr/share/s4-snapshot/scripts/"
        chmod a+x "$BIND_ROOT/usr/share/s4-snapshot/scripts/configure_debian_calamares.sh"
        chroot "$BIND_ROOT" dash "$cfg" || die "configure_debian_calamares.sh failed in bind-root"
        return
    fi

    die "configure_debian_calamares.sh not found; Calamares would not work on the live ISO"
}

main() {
    parse_args "$@"
    require_root
    maybe_fetch_debs
    mount_chroot_fs

    trap 'umount_chroot_fs' EXIT

    install_debs_in_chroot
    configure_calamares_in_chroot

    log "installer embedded successfully"
}

main "$@"
