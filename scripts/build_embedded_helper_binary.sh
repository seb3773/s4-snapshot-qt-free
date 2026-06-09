#!/bin/bash
# Convert the freshly built helper executable into a compressed C++ payload header.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <helper-binary> <output-header>" >&2
    exit 2
fi

HELPER_BIN="$1"
OUT_HEADER="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$(dirname "$OUT_HEADER")"
TMP_DIR="$OUT_DIR/helper-binary-build"
TMP_BIN="$TMP_DIR/helper.bin"
TMP_LZ4="$TMP_DIR/helper.bin.lz4"
TMP_SIZES="$TMP_DIR/helper.sizes"
TMP_INC="$TMP_DIR/helper.inc"
TOOLS_DIR="$TMP_DIR/tools"

if [ ! -x "$HELPER_BIN" ]; then
    echo "ERROR: helper binary is not executable: $HELPER_BIN" >&2
    exit 1
fi

mkdir -p "$TMP_DIR"
cp "$HELPER_BIN" "$TMP_BIN"

mkdir -p "$TOOLS_DIR"
gcc -O2 -std=c11 -Wall -Wextra \
    -I"$ROOT_DIR/compressor/lz4" \
    -o "$TOOLS_DIR/asset_pack" \
    "$ROOT_DIR/tools/asset_pack.c" \
    "$ROOT_DIR/compressor/lz4/lz4.c" \
    "$ROOT_DIR/compressor/lz4/lz4hc.c" \
    "$ROOT_DIR/compressor/lz4/lz4_compress_wrapper.c"

"$TOOLS_DIR/asset_pack" "$TMP_BIN" "$TMP_LZ4" "$TMP_SIZES"
xxd -i -n embedded_helper_binary_payload "$TMP_LZ4" >"$TMP_INC"

sed -i 's/^unsigned char embedded_helper_binary_payload\[\]/static const unsigned char embedded_helper_binary_payload[]/' "$TMP_INC"
sed -i '/^unsigned int /d' "$TMP_INC"

read -r UNCOMPRESSED_SIZE COMPRESSED_SIZE <"$TMP_SIZES"

cat >"$OUT_HEADER" <<EOF
#pragma once

#include <cstddef>
#include <cstdint>

static constexpr std::uint32_t embedded_helper_binary_uncompressed_size = ${UNCOMPRESSED_SIZE}u;
static constexpr std::uint32_t embedded_helper_binary_compressed_size = ${COMPRESSED_SIZE}u;
EOF

cat "$TMP_INC" >>"$OUT_HEADER"
rm -rf "$TMP_DIR"
