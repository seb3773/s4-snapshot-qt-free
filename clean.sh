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

echo "=========================================="
echo "S4 Snapshot - Clean Build Directories"
echo "=========================================="
echo ""

# List of build directories to clean
BUILD_DIRS=(
    "build-make"
    "build-gui"
    "build-qt"
    "build-nqt"
    "build-tests"
    "build"
)

echo "Cleaning build directories..."
for dir in "${BUILD_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        echo "  Removing: $dir/"
        rm -rf "$dir"
    fi
done

echo ""
echo "Cleaning CMake cache files..."
find . -maxdepth 1 -name "CMakeCache.txt" -delete 2>/dev/null || true
find . -maxdepth 1 -name "CMakeFiles" -type d -exec rm -rf {} + 2>/dev/null || true

echo ""
echo "Cleaning generated files..."
# Clean Qt generated files
find . -name "*.qm" -type f -delete 2>/dev/null || true
find . -name "moc_*.cpp" -type f -delete 2>/dev/null || true
find . -name "ui_*.h" -type f -delete 2>/dev/null || true
find . -name "*_autogen" -type d -exec rm -rf {} + 2>/dev/null || true

# Clean compiled objects
find . -name "*.o" -type f -delete 2>/dev/null || true
find . -name "*.a" -type f -delete 2>/dev/null || true
find . -name "*.so" -type f -delete 2>/dev/null || true

echo ""
echo "✓ Clean complete!"
echo ""
echo "You can now run one of the build scripts:"
echo "  ./build_cli_qtfree.sh  - Build Qt-free CLI"
echo "  ./build_gui.sh         - Build GUI"
echo "  ./build_tests.sh       - Build and run test suite (--build-only to skip run)"
echo "  ./build_all.sh         - Build CLI, GUI, and tests"

# Made with Bob
