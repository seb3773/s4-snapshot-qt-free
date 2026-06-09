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

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Source Qt-free verification function
source "$SCRIPT_DIR/scripts/verify_qt_free.sh"

BUILD_DIR="build-make"
JOBS=$(nproc)

echo "=========================================="
echo "S4 Snapshot - Build Qt-Free CLI"
echo "=========================================="
echo ""
echo "Target: iso-snapshot-cli (Qt-free)"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $JOBS"
echo ""

# Check if i18n_keyval is available and populated
if [ ! -f "libs/i18n_keyval/CMakeLists.txt" ]; then
    echo "i18n_keyval library not found or incomplete at libs/i18n_keyval."
    if [ -d ".git" ]; then
        echo "Attempting to initialize and update git submodules..."
        git submodule update --init --recursive
    fi

    if [ ! -f "libs/i18n_keyval/CMakeLists.txt" ]; then
        echo "ERROR: i18n_keyval library not found or missing CMakeLists.txt."
        echo "This is a required dependency. Please run:"
        echo "  git submodule update --init --recursive"
        exit 1
    fi
fi

echo "Step 1/4: Configuring..."
cmake -B "$BUILD_DIR" \
    -DBUILD_GUI=OFF \
    -DBUILD_CLI=ON \
    -DBUILD_CLI_QT=OFF \
    -DBUILD_TESTS=OFF \
    -DQT_FREE_GUARDS=ON \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Step 2/4: Building..."
cmake --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "Step 3/4: Verifying Qt-free status..."
echo "=========================================="

# Verify CLI is Qt-free
if ! verify_qt_free "$BUILD_DIR/iso-snapshot-cli" "iso-snapshot-cli"; then
    echo ""
    echo "ERROR: Qt-free verification failed!"
    echo "Build completed but binary has Qt dependencies."
    exit 1
fi

echo ""
echo "Step 4/4: Verifying helper is Qt-free..."
echo "=========================================="

# Verify helper is Qt-free
if ! verify_qt_free "$BUILD_DIR/helper" "helper"; then
    echo ""
    echo "ERROR: Qt-free verification failed!"
    echo "Build completed but helper has Qt dependencies."
    exit 1
fi

echo ""
echo "=========================================="
echo "Build Complete!"
echo "=========================================="
echo ""
echo "Binary location: $BUILD_DIR/iso-snapshot-cli"
echo "Helper build artifact embedded into binary: $BUILD_DIR/helper"
echo ""
echo "To test:"
echo "  $BUILD_DIR/iso-snapshot-cli --help"
echo "  $BUILD_DIR/iso-snapshot-cli --version"
echo ""
echo "To create an ISO (privileged steps use helper elevation):"
echo "  $BUILD_DIR/iso-snapshot-cli --file my-system.iso"
echo ""

# Made with Bob
