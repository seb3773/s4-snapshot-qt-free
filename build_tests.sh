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

BUILD_DIR="build-tests"
JOBS=$(nproc)
BUILD_ONLY=false

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Build and optionally run the unit_tests oracle suite.

Options:
  --build-only   Configure and build unit_tests without running them
  -h, --help     Show this help

Examples:
  $0
  $0 --build-only
  $0 --build-only && build-tests/unit_tests --test-case test_work_setupenv_plan_qt_oracle_vs_cpp_planner_basic
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --build-only)
            BUILD_ONLY=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo ""
            usage
            exit 1
            ;;
    esac
done

echo "=========================================="
echo "S4 Snapshot - Build Test Suite"
echo "=========================================="
echo ""
echo "Target: unit_tests (Oracle validation)"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $JOBS"
if [ "$BUILD_ONLY" = true ]; then
    echo "Mode: build only (tests will not run)"
fi
echo ""

# Check if Qt6 is available (required for oracle tests)
if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    echo "ERROR: Qt6 not found. Oracle tests require Qt6 for comparison:"
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

if [ "$BUILD_ONLY" = true ]; then
    TOTAL_STEPS=2
else
    TOTAL_STEPS=3
fi

echo "Step 1/$TOTAL_STEPS: Configuring..."
cmake -B "$BUILD_DIR" \
    -DBUILD_GUI=OFF \
    -DBUILD_CLI=OFF \
    -DBUILD_CLI_QT=OFF \
    -DBUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "Step 2/$TOTAL_STEPS: Building..."
cmake --build "$BUILD_DIR" -j"$JOBS" --target unit_tests

echo ""
echo "=========================================="
echo "✓ Build complete!"
echo "=========================================="
echo ""
echo "Test binary: $BUILD_DIR/unit_tests"
echo ""

if [ "$BUILD_ONLY" = true ]; then
    echo "To run the full suite:"
    echo "  $BUILD_DIR/unit_tests"
    echo ""
    echo "To run a single test case:"
    echo "  $BUILD_DIR/unit_tests --test-case test_work_setupenv_plan_qt_oracle_vs_cpp_planner_basic"
    echo ""
    exit 0
fi

echo "Step 3/$TOTAL_STEPS: Running tests..."
echo "=========================================="
"$BUILD_DIR/unit_tests"

TEST_RESULT=$?

echo ""
echo "=========================================="
if [ $TEST_RESULT -eq 0 ]; then
    echo "✓ All tests passed!"
    echo "=========================================="
    echo ""
    echo "To run a single test case:"
    echo "  $BUILD_DIR/unit_tests --test-case test_work_setupenv_plan_qt_oracle_vs_cpp_planner_basic"
    echo ""
else
    echo "✗ Some tests failed!"
    echo "=========================================="
    echo ""
    echo "Review the output above for details."
    echo ""
    exit 1
fi
