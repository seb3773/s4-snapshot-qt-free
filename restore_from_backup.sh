#!/bin/bash
# Restore system binaries from an install_for_testing.sh backup directory.

set -euo pipefail

if [ -z "${1:-}" ]; then
    echo "Usage: $0 <backup_directory>"
    echo ""
    echo "Example: sudo $0 /tmp/s4-snapshot-backup-20260519-040000"
    exit 1
fi

BACKUP_DIR="$1"

if [ ! -d "$BACKUP_DIR" ]; then
    echo "Error: backup directory not found: $BACKUP_DIR"
    exit 1
fi

echo "=== Restoring original binaries ==="
echo ""
echo "Backup source: $BACKUP_DIR"
echo ""

RESTORED=0

if [ -f "$BACKUP_DIR/iso-snapshot-cli" ]; then
    sudo cp -v "$BACKUP_DIR/iso-snapshot-cli" /usr/bin/iso-snapshot-cli
    sudo chmod 755 /usr/bin/iso-snapshot-cli
    echo "✓ Restored: /usr/bin/iso-snapshot-cli"
    RESTORED=1
fi

if [ -f "$BACKUP_DIR/s4-snapshot" ]; then
    sudo cp -v "$BACKUP_DIR/s4-snapshot" /usr/bin/s4-snapshot
    sudo chmod 755 /usr/bin/s4-snapshot
    echo "✓ Restored: /usr/bin/s4-snapshot"
    RESTORED=1
fi

if [ -d "$BACKUP_DIR/iso-snapshot-cli" ]; then
    sudo rm -rf /usr/lib/iso-snapshot-cli
    sudo cp -rv "$BACKUP_DIR/iso-snapshot-cli" /usr/lib/
    echo "✓ Restored: /usr/lib/iso-snapshot-cli/"
    RESTORED=1
fi

if [ "$RESTORED" -eq 0 ]; then
    echo "Nothing to restore in: $BACKUP_DIR"
    exit 1
fi

echo ""
echo "=== Restore complete ==="
echo ""
echo "Backup is still available in: $BACKUP_DIR"
echo "You can delete it with: rm -rf $BACKUP_DIR"
echo ""
