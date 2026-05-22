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

echo "=========================================="
echo "S4 Snapshot - Build All Targets"
echo "=========================================="
echo ""
echo "This will build:"
echo "  1. Qt-free CLI (iso-snapshot-cli)"
echo "  2. GUI (s4-snapshot)"
echo "  3. Qt-based CLI (iso-snapshot-cli-qt, for comparison)"
echo "  4. Test suite (unit_tests)"
echo ""

# Check for -y or --yes argument to skip confirmation
SKIP_CONFIRM=false
if [ "$1" = "-y" ] || [ "$1" = "--yes" ]; then
    SKIP_CONFIRM=true
fi

if [ "$SKIP_CONFIRM" = false ]; then
    read -p "Continue? (y/N) " -n 1 -r
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
echo "Building GUI + CLI..."
echo "=========================================="
./build_gui.sh

echo ""
echo "=========================================="
echo "Building Qt-Based CLI (Legacy)..."
echo "=========================================="
./build_cli_qt.sh

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
echo "  Qt-free CLI:     build-make/iso-snapshot-cli"
echo "  GUI:             build-gui/s4-snapshot"
echo "  Qt-based CLI:    build-qt/iso-snapshot-cli-qt"
echo "  Helper:          build-make/helper, build-gui/helper"
echo "  Tests:           build-tests/unit_tests"
echo ""
echo "Quick start:"
echo "  ./build-make/iso-snapshot-cli --help"
echo "  sudo ./build-gui/s4-snapshot"
echo ""

# Made with Bob
