#!/bin/bash
# Download Calamares installer packages into the per-user cache.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=installer-debs-cache.sh
. "$SCRIPT_DIR/installer-debs-cache.sh"

SUITE="${1:-bookworm}"
OUT_DIR="${2:-}"

APT_GET="/usr/bin/apt-get"
if [ ! -x "$APT_GET" ]; then
    echo "ERROR: $APT_GET not found" >&2
    exit 1
fi

PACKAGES=(
    calamares-settings-debian
)

usage() {
    cat <<EOF
usage: $(basename "$0") [suite] [output-dir]

Download Calamares installer .deb files using the host apt sources.
Packages are stored in the invoking user's cache for offline reuse.

  suite       Debian suite label for cache layout (default: detected codename or bookworm)
  output-dir  Destination directory (default: ~/.cache/s4-snapshot/installer-debs/<suite>)

Network access is required. The host system is not modified (download-only).

Example:
  $(basename "$0")
  $(basename "$0") trixie
  $(basename "$0") bookworm /path/to/custom/cache
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if [ -z "$OUT_DIR" ]; then
    OUT_DIR="$(installer_debs_default_cache_dir "$SUITE")"
fi

installer_debs_ensure_cache_dir "$OUT_DIR"

TMP_APT_CACHE="$(mktemp -d)"
trap 'rm -rf "$TMP_APT_CACHE"' EXIT

HOST_SUITE=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    HOST_SUITE="${VERSION_CODENAME:-}"
fi
if [ -n "$HOST_SUITE" ] && [ "$HOST_SUITE" != "$SUITE" ]; then
    echo "WARNING: host suite is $HOST_SUITE but cache key is $SUITE — pinning apt to $SUITE" >&2
    echo "WARNING: build host should match the target snapshot suite for consistent .deb versions" >&2
fi

echo "Fetching installer packages (host apt sources, suite cache key: $SUITE)"
echo "Output directory: $OUT_DIR"

export DEBIAN_FRONTEND=noninteractive

"$APT_GET" update

APT_EXTRA_OPTS=()
if [ -n "$SUITE" ]; then
    APT_EXTRA_OPTS+=(-o "APT::Default-Release=$SUITE")
fi

"$APT_GET" \
    "${APT_EXTRA_OPTS[@]}" \
    -o Dir::Cache::archives="$OUT_DIR" \
    -o Dir::Cache="$TMP_APT_CACHE" \
    -o APT::Keep-Downloaded-Packages=true \
    install --download-only -y \
    "${PACKAGES[@]}"

find "$OUT_DIR" -name '*.deb' -size 0 -delete 2>/dev/null || true

DEB_COUNT="$(find "$OUT_DIR" -maxdepth 1 -name '*.deb' 2>/dev/null | wc -l)"
if [ "$DEB_COUNT" -eq 0 ]; then
    echo "ERROR: no .deb files downloaded to $OUT_DIR" >&2
    exit 1
fi

if ! installer_debs_cache_has_required_packages "$OUT_DIR"; then
    echo "ERROR: required package calamares-settings-debian missing from $OUT_DIR" >&2
    exit 1
fi

installer_debs_ensure_cache_dir "$OUT_DIR"

echo "Downloaded $DEB_COUNT .deb file(s) to $OUT_DIR"
du -sh "$OUT_DIR" | awk '{print "Total size:", $1}'
