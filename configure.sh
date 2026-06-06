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
echo "S4 Snapshot - Configuration Check"
echo "=========================================="
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

MISSING_DEPS=()
MISSING_OPTIONAL=()

# Function to check if command exists
check_command() {
    if command -v "$1" &> /dev/null; then
        echo -e "${GREEN}✓${NC} $1 found"
        return 0
    else
        echo -e "${RED}✗${NC} $1 not found"
        return 1
    fi
}

# Function to check if package is installed (Debian/Ubuntu)
check_package() {
    if dpkg -l "$1" 2>/dev/null | grep -q "^ii"; then
        echo -e "${GREEN}✓${NC} $1 installed"
        return 0
    else
        echo -e "${RED}✗${NC} $1 not installed"
        return 1
    fi
}

echo "Checking required build tools..."
echo "--------------------------------"

# CMake
if ! check_command cmake; then
    MISSING_DEPS+=("cmake")
fi

# C++ compiler
if ! check_command g++; then
    MISSING_DEPS+=("g++")
fi

# Make or Ninja
if check_command ninja; then
    BUILD_TOOL="ninja"
elif check_command make; then
    BUILD_TOOL="make"
else
    echo -e "${RED}✗${NC} Neither make nor ninja found"
    MISSING_DEPS+=("make")
    BUILD_TOOL="make"
fi

echo ""
echo "Checking system tools..."
echo "------------------------"

# Required system tools
if ! check_command mksquashfs; then
    MISSING_DEPS+=("squashfs-tools")
fi

if ! check_command xorriso; then
    MISSING_DEPS+=("xorriso")
fi

if ! check_command lslogins; then
    echo -e "${YELLOW}⚠${NC} lslogins not found (usually in util-linux)"
fi

# Vendored live ecosystem data
if ! [ -d "data/live-files/files" ] || ! [ -d "data/live-files/general-files" ]; then
    echo -e "${RED}✗${NC} vendored live-files data not found"
    MISSING_DEPS+=("data/live-files")
else
    echo -e "${GREEN}✓${NC} vendored live-files data found"
fi

if ! [ -f "data/s4-iso-templates/iso-template.tar.gz" ] || ! [ -f "data/s4-iso-templates/template-initrd.gz" ]; then
    echo -e "${RED}✗${NC} vendored ISO templates not found"
    MISSING_DEPS+=("data/s4-iso-templates")
else
    echo -e "${GREEN}✓${NC} vendored ISO templates found"
fi

echo ""
echo "Checking Qt6 (optional, for GUI)..."
echo "------------------------------------"

# Qt6 packages (optional for GUI)
QT6_AVAILABLE=true
if ! check_package qt6-base-dev; then
    MISSING_OPTIONAL+=("qt6-base-dev")
    QT6_AVAILABLE=false
fi

if ! check_package qt6-tools-dev; then
    MISSING_OPTIONAL+=("qt6-tools-dev")
    QT6_AVAILABLE=false
fi

echo ""
echo "Checking C++ compiler version..."
echo "--------------------------------"

if command -v g++ &> /dev/null; then
    GCC_VERSION=$(g++ --version | head -n1)
    echo "  $GCC_VERSION"
    
    # Check if GCC >= 10 (required for C++20)
    GCC_MAJOR=$(g++ -dumpversion | cut -d. -f1)
    if [ "$GCC_MAJOR" -ge 10 ]; then
        echo -e "  ${GREEN}✓${NC} GCC version supports C++20"
    else
        echo -e "  ${RED}✗${NC} GCC version too old (need >= 10 for C++20)"
        MISSING_DEPS+=("g++-10 or newer")
    fi
fi

echo ""
echo "=========================================="
echo "Configuration Summary"
echo "=========================================="
echo ""

if [ ${#MISSING_DEPS[@]} -eq 0 ]; then
    echo -e "${GREEN}✓ All required dependencies are installed!${NC}"
    echo ""
    echo "Build tool: $BUILD_TOOL"
    echo ""
    
    if [ "$QT6_AVAILABLE" = true ]; then
        echo -e "${GREEN}✓ Qt6 is available - you can build GUI${NC}"
        echo ""
        echo "Available build targets:"
        echo "  ./build_cli_qtfree.sh  - Qt-free CLI (no Qt dependencies)"
        echo "  ./build_gui.sh         - GUI + CLI (requires Qt6)"
        echo "  ./build_tests.sh       - Test suite"
        echo "  ./build_all.sh         - Everything"
    else
        echo -e "${YELLOW}⚠ Qt6 not available - GUI build will not work${NC}"
        echo ""
        echo "Available build targets:"
        echo "  ./build_cli_qtfree.sh  - Qt-free CLI (recommended)"
        echo "  ./build_tests.sh       - Test suite"
        echo ""
        echo "To install Qt6 for GUI support:"
        echo "  sudo apt install qt6-base-dev qt6-tools-dev"
    fi
else
    echo -e "${RED}✗ Missing required dependencies:${NC}"
    for dep in "${MISSING_DEPS[@]}"; do
        echo "  - $dep"
    done
    echo ""
    echo "To install missing dependencies on Debian/Ubuntu:"
    echo "  sudo apt install ${MISSING_DEPS[*]}"
    echo ""
    exit 1
fi

if [ ${#MISSING_OPTIONAL[@]} -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}Optional dependencies not installed:${NC}"
    for dep in "${MISSING_OPTIONAL[@]}"; do
        echo "  - $dep"
    done
    echo ""
    echo "To install optional dependencies:"
    echo "  sudo apt install ${MISSING_OPTIONAL[*]}"
fi

echo ""
echo "=========================================="
echo "System Information"
echo "=========================================="
echo "OS: $(lsb_release -ds 2>/dev/null || cat /etc/os-release | grep PRETTY_NAME | cut -d'"' -f2)"
echo "Kernel: $(uname -r)"
echo "Architecture: $(uname -m)"
echo ""

# Made with Bob
