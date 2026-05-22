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

BUILD_DIR="build-tests"
JOBS=$(nproc)

echo "=========================================="
echo "S4 Snapshot - Build Test Suite"
echo "=========================================="
echo ""
echo "Target: unit_tests (Oracle validation)"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $JOBS"
echo ""

# Check if Qt6 is available (required for oracle tests)
if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    echo "ERROR: Qt6 not found. Oracle tests require Qt6 for comparison:"
    echo "  sudo apt install qt6-base-dev qt6-tools-dev"
    exit 1
fi

# Check if i18n_keyval is available
if [ ! -d "libs/i18n_keyval" ]; then
    echo "ERROR: i18n_keyval library not found at libs/i18n_keyval"
    echo "This is a required dependency included in the project."
    echo "Please ensure you have the complete project source."
    exit 1
fi

echo "Step 1/3: Configuring..."
cmake -B "$BUILD_DIR" \
    -DBUILD_GUI=OFF \
    -DBUILD_CLI=OFF \
    -DBUILD_CLI_QT=OFF \
    -DBUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "Step 2/3: Building..."
cmake --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "Step 3/3: Running tests..."
echo "=========================================="
"$BUILD_DIR/unit_tests"

TEST_RESULT=$?

echo ""
echo "=========================================="
if [ $TEST_RESULT -eq 0 ]; then
    echo "✓ All tests passed!"
    echo "=========================================="
    echo ""
    echo "Test binary: $BUILD_DIR/unit_tests"
    echo ""
    echo "To run specific tests:"
    echo "  $BUILD_DIR/unit_tests --gtest_filter='*WorkSetupEnv*'"
    echo "  $BUILD_DIR/unit_tests --gtest_filter='*CommandLineParser*'"
    echo ""
else
    echo "✗ Some tests failed!"
    echo "=========================================="
    echo ""
    echo "Review the output above for details."
    echo ""
    exit 1
fi

# Made with Bob
