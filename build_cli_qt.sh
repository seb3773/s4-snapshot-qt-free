#!/bin/bash
# **********************************************************************
# * Copyright (C) 2015-2025 MX Authors
# *
# * This file is part of S4 Snapshot.
# *
# * S4 Snapshot is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# **********************************************************************

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-qt"
JOBS=$(nproc)

echo "=========================================="
echo "S4 Snapshot - Build Qt-Based CLI (Legacy)"
echo "=========================================="
echo ""
echo "Target: iso-snapshot-cli-qt (Qt-based, for comparison)"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $JOBS"
echo ""
echo "NOTE: This is the legacy Qt-based CLI, kept for oracle testing."
echo "      For production use, prefer the Qt-free CLI (build_cli_qtfree.sh)"
echo ""

# Check if Qt6 is available
if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    echo "ERROR: Qt6 not found. Please install Qt6 development packages:"
    echo "  sudo apt install qt6-base-dev qt6-tools-dev"
    exit 1
fi

echo "Step 1/2: Configuring..."
cmake -B "$BUILD_DIR" \
    -DBUILD_GUI=OFF \
    -DBUILD_CLI=OFF \
    -DBUILD_CLI_QT=ON \
    -DBUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Step 2/2: Building..."
cmake --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "=========================================="
echo "Build Complete!"
echo "=========================================="
echo ""
echo "Binary location: $BUILD_DIR/iso-snapshot-cli-qt"
echo ""
echo "To test:"
echo "  $BUILD_DIR/iso-snapshot-cli-qt --help"
echo "  $BUILD_DIR/iso-snapshot-cli-qt --version"
echo ""
echo "To compare with Qt-free version:"
echo "  ./build_cli_qtfree.sh"
echo "  diff <($BUILD_DIR/iso-snapshot-cli-qt --help) <(./build-make/iso-snapshot-cli --help)"
echo ""

# Made with Bob
