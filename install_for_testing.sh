#!/bin/bash
# Install locally built binaries for manual testing (backs up existing system copies).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

CLI_BIN="build-make/iso-snapshot-cli"
GUI_BIN="build-gui/s4-snapshot"

INSTALL_CLI=false
INSTALL_GUI=false

if [ -f "$CLI_BIN" ]; then
    INSTALL_CLI=true
fi
if [ -f "$GUI_BIN" ]; then
    INSTALL_GUI=true
fi

if [ "$INSTALL_CLI" = false ] && [ "$INSTALL_GUI" = false ]; then
    echo -e "${RED}Error: no built binaries found.${NC}"
    echo "Build first from the project root:"
    echo "  ./build_cli_qtfree.sh   # for iso-snapshot-cli -> $CLI_BIN"
    echo "  ./build_gui.sh        # for s4-snapshot     -> $GUI_BIN"
    exit 1
fi

echo "=== Temporary installation of built binaries for testing ==="
echo ""

BACKUP_DIR="/tmp/s4-snapshot-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"

echo -e "${YELLOW}1. Backing up existing binaries...${NC}"

if [ "$INSTALL_CLI" = true ] && [ -f /usr/bin/iso-snapshot-cli ]; then
    sudo cp -v /usr/bin/iso-snapshot-cli "$BACKUP_DIR/"
    echo -e "${GREEN}✓ Backed up: /usr/bin/iso-snapshot-cli${NC}"
fi

if [ "$INSTALL_GUI" = true ] && [ -f /usr/bin/s4-snapshot ]; then
    sudo cp -v /usr/bin/s4-snapshot "$BACKUP_DIR/"
    echo -e "${GREEN}✓ Backed up: /usr/bin/s4-snapshot${NC}"
fi

echo ""
echo -e "${YELLOW}2. Installing new binaries...${NC}"

if [ "$INSTALL_CLI" = true ]; then
    sudo cp -v "$CLI_BIN" /usr/bin/iso-snapshot-cli
    sudo chmod 755 /usr/bin/iso-snapshot-cli
    echo -e "${GREEN}✓ Installed: /usr/bin/iso-snapshot-cli${NC}"
fi

if [ "$INSTALL_GUI" = true ]; then
    sudo cp -v "$GUI_BIN" /usr/bin/s4-snapshot
    sudo chmod 755 /usr/bin/s4-snapshot
    echo -e "${GREEN}✓ Installed: /usr/bin/s4-snapshot${NC}"
fi

echo ""
echo -e "${GREEN}=== Installation complete ===${NC}"
echo ""
echo -e "${YELLOW}Binaries backed up to: $BACKUP_DIR${NC}"
echo ""
echo "To restore previous binaries:"
echo "  sudo ./restore_from_backup.sh $BACKUP_DIR"
echo ""
echo "To test:"
if [ "$INSTALL_CLI" = true ]; then
    echo "  iso-snapshot-cli --help"
    echo "  sudo iso-snapshot-cli --file my-system.iso"
fi
if [ "$INSTALL_GUI" = true ]; then
    echo "  s4-snapshot"
fi
echo ""
