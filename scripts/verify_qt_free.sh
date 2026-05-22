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

# Function to verify a binary is Qt-free
# Usage: verify_qt_free <binary_path> <binary_name>
# Returns: 0 if Qt-free, 1 if Qt found

verify_qt_free() {
    local binary_path="$1"
    local binary_name="$2"
    
    if [ ! -f "$binary_path" ]; then
        echo "ERROR: Binary not found: $binary_path"
        return 1
    fi
    
    echo ""
    echo "Verifying Qt-free status of $binary_name..."
    echo "----------------------------------------"
    
    # Check for Qt libraries in dependencies
    local qt_libs=$(ldd "$binary_path" 2>/dev/null | grep -i qt)
    
    if [ -n "$qt_libs" ]; then
        echo "✗ FAILED: Qt libraries detected!"
        echo ""
        echo "Qt dependencies found:"
        echo "$qt_libs"
        echo ""
        echo "This binary should be Qt-free but has Qt dependencies."
        echo "This indicates a build configuration error."
        return 1
    else
        echo "✓ PASSED: No Qt libraries detected"
        
        # Show actual dependencies for verification
        echo ""
        echo "Dependencies (non-Qt):"
        ldd "$binary_path" 2>/dev/null | grep -v "linux-vdso\|ld-linux" | head -10
        
        # Check binary size
        local size=$(ls -lh "$binary_path" | awk '{print $5}')
        echo ""
        echo "Binary size: $size"
        echo "✓ $binary_name is Qt-free"
        return 0
    fi
}

# If script is executed directly (not sourced), run verification on argument
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    if [ $# -lt 1 ]; then
        echo "Usage: $0 <binary_path> [binary_name]"
        echo "Example: $0 ./build-make/iso-snapshot-cli"
        exit 1
    fi
    
    binary_path="$1"
    binary_name="${2:-$(basename "$binary_path")}"
    
    verify_qt_free "$binary_path" "$binary_name"
    exit $?
fi

# Made with Bob
