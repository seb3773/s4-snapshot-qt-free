#!/bin/bash
# Shared helpers for the per-user Calamares .deb download cache.
# .deb files are never embedded in S4 Snapshot binaries.

installer_debs_invoke_user() {
    if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ]; then
        printf '%s' "$SUDO_USER"
        return 0
    fi
    if [ "$(id -u)" -ne 0 ]; then
        id -un
        return 0
    fi
    if command -v logname >/dev/null 2>&1; then
        local name
        name="$(logname 2>/dev/null || true)"
        if [ -n "$name" ] && [ "$name" != "root" ]; then
            printf '%s' "$name"
            return 0
        fi
    fi
    printf '%s' "root"
}

installer_debs_invoke_home() {
    local user home
    user="$(installer_debs_invoke_user)"
    if [ "$user" = "root" ]; then
        printf '%s' "${HOME:-/root}"
        return 0
    fi
    home="$(getent passwd "$user" 2>/dev/null | cut -d: -f6)"
    if [ -n "$home" ]; then
        printf '%s' "$home"
        return 0
    fi
    printf '%s' "${HOME:-/tmp}"
}

installer_debs_default_cache_dir() {
    local suite="${1:-bookworm}"
    local home cache_base
    home="$(installer_debs_invoke_home)"
    cache_base="${XDG_CACHE_HOME:-$home/.cache}"
    case "$cache_base" in
        /*) ;;
        *) cache_base="$home/.cache" ;;
    esac
    printf '%s' "$cache_base/s4-snapshot/installer-debs/$suite"
}

installer_debs_ensure_cache_dir() {
    local dir="$1"
    local owner
    mkdir -p "$dir"
    owner="$(installer_debs_invoke_user)"
    if [ "$(id -u)" -eq 0 ] && [ -n "$owner" ] && [ "$owner" != "root" ]; then
        chown -R "$owner:$owner" "$dir"
    fi
}

# Packages that must be present in the cache for offline embed_installer to succeed.
INSTALLER_DEBS_REQUIRED_PACKAGES=(
    calamares-settings-debian
)

installer_debs_cache_lists_package() {
    local dir="$1"
    local pkg="$2"
    local deb base field

    for deb in "$dir"/*.deb; do
        [ -e "$deb" ] || continue
        base="$(basename "$deb")"
        case "$base" in
            ${pkg}_*|${pkg}-*)
                return 0
                ;;
        esac
        if command -v dpkg-deb >/dev/null 2>&1; then
            field="$(dpkg-deb -f "$deb" Package 2>/dev/null || true)"
            if [ "$field" = "$pkg" ]; then
                return 0
            fi
        fi
    done
    return 1
}

installer_debs_cache_has_required_packages() {
    local dir="$1"
    local pkg

    for pkg in "${INSTALLER_DEBS_REQUIRED_PACKAGES[@]}"; do
        if ! installer_debs_cache_lists_package "$dir" "$pkg"; then
            return 1
        fi
    done
    return 0
}

installer_debs_cache_is_complete() {
    local dir="$1"
    local count

    [ -d "$dir" ] || return 1
    count="$(find "$dir" -maxdepth 1 -name '*.deb' 2>/dev/null | wc -l)"
    [ "$count" -gt 0 ] || return 1
    installer_debs_cache_has_required_packages "$dir"
}
