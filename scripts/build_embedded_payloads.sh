#!/bin/bash
# Build embedded asset payloads (live-files + iso-templates + runtime scripts).
# Step 1 of the monolithic binary asset pipeline.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${EMBED_BUILD_DIR:-$ROOT_DIR/build-embed}"
STAGING_DIR="$BUILD_DIR/staging"
GENERATED_DIR="$BUILD_DIR/generated"
TOOLS_DIR="$BUILD_DIR/tools"

LIVE_FILES_SRC="$ROOT_DIR/data/live-files"
ISO_TEMPLATES_SRC="$ROOT_DIR/data/iso-templates"
ISO_TEMPLATE_DIR="$ISO_TEMPLATES_SRC/iso-template"
INITRD_TEMPLATE_DIR="$ISO_TEMPLATES_SRC/template-initrd"
RUNTIME_SCRIPTS_SRC="$ROOT_DIR/scripts"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $1" >&2
        exit 1
    fi
}

generate_payload_header() {
    local stem="$1"
    local lz4_path="$2"
    local uncomp_size="$3"
    local comp_size="$4"
    local out_header="$5"

    local tmp_bin="$BUILD_DIR/payload.bin"
    local tmp_inc="$BUILD_DIR/${stem}.inc"

    cp "$lz4_path" "$tmp_bin"
    (cd "$BUILD_DIR" && xxd -i payload.bin >"${stem}.inc")

    local array_name="embedded_${stem}_payload"
    sed -i "s/^unsigned char .*\\[\\]/static const unsigned char ${array_name}[]/" "$tmp_inc"
    sed -i '/^unsigned int /d' "$tmp_inc"

    cat >"$out_header" <<EOF
#pragma once

#include <cstddef>
#include <cstdint>

static constexpr std::uint32_t embedded_${stem}_uncompressed_size = ${uncomp_size}u;
static constexpr std::uint32_t embedded_${stem}_compressed_size = ${comp_size}u;
EOF

    cat "$tmp_inc" >>"$out_header"
    rm -f "$tmp_bin" "$tmp_inc"
}

prepare_staging() {
    echo "Preparing staging directories..."

    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR/iso-templates" "$STAGING_DIR/runtime-scripts"

    if [ ! -d "$LIVE_FILES_SRC/files" ] || [ ! -d "$LIVE_FILES_SRC/general-files" ]; then
        echo "ERROR: invalid live-files source at $LIVE_FILES_SRC" >&2
        exit 1
    fi

    if [ ! -d "$ISO_TEMPLATE_DIR" ] || [ ! -d "$INITRD_TEMPLATE_DIR" ]; then
        echo "ERROR: missing ISO template trees under $ISO_TEMPLATES_SRC" >&2
        exit 1
    fi

    cp -a "$LIVE_FILES_SRC" "$STAGING_DIR/live-files"
    cp -a "$ISO_TEMPLATE_DIR" "$STAGING_DIR/iso-templates/iso-template"
    cp -a "$INITRD_TEMPLATE_DIR" "$STAGING_DIR/iso-templates/template-initrd"
    cp -a "$RUNTIME_SCRIPTS_SRC/snapshot-bootparameter.sh" "$STAGING_DIR/runtime-scripts/"
    cp -a "$RUNTIME_SCRIPTS_SRC/configure_debian_calamares.sh" "$STAGING_DIR/runtime-scripts/"
    cp -a "$RUNTIME_SCRIPTS_SRC/copy-initrd-modules" "$STAGING_DIR/runtime-scripts/"
    cp -a "$RUNTIME_SCRIPTS_SRC/copy-initrd-programs" "$STAGING_DIR/runtime-scripts/"

    echo "  live-files:      $(du -sh "$STAGING_DIR/live-files" | awk '{print $1}')"
    echo "  iso-template:    $(du -sh "$STAGING_DIR/iso-templates/iso-template" | awk '{print $1}')"
    echo "  template-initrd: $(du -sh "$STAGING_DIR/iso-templates/template-initrd" | awk '{print $1}')"
    echo "  runtime-scripts: $(du -sh "$STAGING_DIR/runtime-scripts" | awk '{print $1}')"
}

build_tools() {
    echo "Building asset pack/unpack tools..."
    mkdir -p "$TOOLS_DIR"

    gcc -O2 -std=c11 -Wall -Wextra \
        -I"$ROOT_DIR/compressor/lz4" \
        -o "$TOOLS_DIR/asset_pack" \
        "$ROOT_DIR/tools/asset_pack.c" \
        "$ROOT_DIR/compressor/lz4/lz4.c" \
        "$ROOT_DIR/compressor/lz4/lz4hc.c" \
        "$ROOT_DIR/compressor/lz4/lz4_compress_wrapper.c"

    gcc -O2 -std=c11 -Wall -Wextra \
        -I"$ROOT_DIR/decompressor/lz4" \
        -o "$TOOLS_DIR/asset_unpack" \
        "$ROOT_DIR/tools/asset_unpack.c" \
        "$ROOT_DIR/decompressor/lz4/codec_lz4.c"
}

build_payload() {
    local stem="$1"
    local tar_root="$2"

    local tar_path="$BUILD_DIR/${stem}.tar"
    local lz4_path="$BUILD_DIR/${stem}.tar.lz4"
    local sizes_path="$BUILD_DIR/${stem}.sizes"
    local out_header="$GENERATED_DIR/${stem}_payload.h"

    echo "Building payload: $stem"

    rm -f "$tar_path" "$lz4_path" "$sizes_path"
    tar --format=ustar -cf "$tar_path" -C "$STAGING_DIR" "$tar_root"

    "$TOOLS_DIR/asset_pack" "$tar_path" "$lz4_path" "$sizes_path"

    local uncomp_size comp_size
    read -r uncomp_size comp_size <"$sizes_path"

    generate_payload_header "$stem" "$lz4_path" "$uncomp_size" "$comp_size" "$out_header"

    local verify_tar="$BUILD_DIR/${stem}.verify.tar"
    "$TOOLS_DIR/asset_unpack" "$lz4_path" "$uncomp_size" "$verify_tar"

    if ! cmp -s "$tar_path" "$verify_tar"; then
        echo "ERROR: round-trip verification failed for $stem" >&2
        exit 1
    fi

    echo "  tar:        $tar_path ($(du -h "$tar_path" | awk '{print $1}'))"
    echo "  lz4:        $lz4_path ($(du -h "$lz4_path" | awk '{print $1}'))"
    echo "  header:     $out_header"
}

main() {
    require_cmd gcc
    require_cmd tar
    require_cmd xxd
    require_cmd cmp

    mkdir -p "$BUILD_DIR" "$GENERATED_DIR"

    prepare_staging
    build_tools
    build_payload "live_files" "live-files"
    build_payload "iso_templates" "iso-templates"
    build_payload "runtime_scripts" "runtime-scripts"

    echo ""
    echo "Embedded payloads ready under $GENERATED_DIR"
}

main "$@"
