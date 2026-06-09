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
echo "S4 Snapshot - Build All Targets"
echo "=========================================="
echo ""
echo "This will build:"
echo "  1. Qt-free CLI (iso-snapshot-cli)"
echo "  2. GUI (s4-snapshot)"
echo "  3. Test suite (unit_tests, build only)"
echo ""

SKIP_CONFIRM=false
RUN_TESTS=false
for arg in "$@"; do
    case "$arg" in
        -y|--yes)
            SKIP_CONFIRM=true
            ;;
        --run-tests)
            RUN_TESTS=true
            ;;
    esac
done

if [ "$SKIP_CONFIRM" = false ]; then
    read -r -p "Continue? (y/N) " -n 1 REPLY
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 0
    fi
fi

echo ""
echo "=========================================="
echo "Building Qt-Free CLI..."
echo "=========================================="
./build_cli_qtfree.sh

echo ""
echo "=========================================="
echo "Building GUI..."
echo "=========================================="
./build_gui.sh

echo ""
echo "=========================================="
echo "Building and Running Tests..."
echo "=========================================="
./build_tests.sh

echo ""
echo "=========================================="
echo "All Builds Complete!"
echo "=========================================="
echo ""
echo "Summary:"
echo "  Qt-free CLI:  build-make/iso-snapshot-cli"
echo "  GUI:          build-gui/s4-snapshot"
echo "  Helper:       build-make/helper, build-gui/helper"
echo "  Tests:        build-tests/unit_tests"
echo ""
echo "Quick start:"
echo "  ./build-make/iso-snapshot-cli --help"
echo "  ./build-gui/s4-snapshot"
echo ""
if [ "$RUN_TESTS" = false ]; then
    echo "To run oracle tests:"
    echo "  ./build_tests.sh"
    echo "  ./build_all.sh --run-tests"
    echo ""
fi
