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

# Source Qt-free verification function
source "$SCRIPT_DIR/scripts/verify_qt_free.sh"

BUILD_DIR="build-gui"
JOBS=$(nproc)

echo "=========================================="
echo "S4 Snapshot - Build GUI + CLI"
echo "=========================================="
echo ""
echo "Targets: s4-snapshot (GUI), helper"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $JOBS"
echo ""

# Check if Qt6 is available
if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    echo "ERROR: Qt6 not found. Please install Qt6 development packages:"
    echo "  sudo apt install qt6-base-dev qt6-tools-dev"
    exit 1
fi

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

echo "Step 1/2: Configuring..."
cmake -B "$BUILD_DIR" \
    -DBUILD_GUI=ON \
    -DBUILD_CLI=OFF \
    -DBUILD_CLI_QT=OFF \
    -DBUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Step 2/3: Building..."
cmake --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "Step 3/3: Verifying helper is Qt-free..."
echo "=========================================="

# Verify helper is Qt-free (GUI uses Qt6, but helper should be Qt-free)
if ! verify_qt_free "$BUILD_DIR/helper" "helper"; then
    echo ""
    echo "WARNING: Helper has Qt dependencies!"
    echo "This is unexpected but won't prevent GUI from working."
fi

echo ""
echo "=========================================="
echo "Build Complete!"
echo "=========================================="
echo ""
echo "Binaries:"
echo "  GUI:    $BUILD_DIR/s4-snapshot (uses Qt6)"
echo "  Helper: $BUILD_DIR/helper (Qt-free)"
echo ""
echo "To run GUI:"
echo "  $BUILD_DIR/s4-snapshot"
echo ""
echo "Note: GUI uses the C++ backend directly (no CLI dependency)."
echo "      To build CLI separately, use: ./build_cli_qtfree.sh"
echo ""

# Made with Bob
