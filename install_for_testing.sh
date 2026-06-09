#!/bin/bash
# Temporary installation script for Phase 4 testing
# Backs up existing binaries and installs new ones

set -e

echo "=== Temporary installation of ported binaries for testing ==="
echo ""

# Colors for output
RED='\033[0:31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check we're in the right directory
if [ ! -f "build-make/iso-snapshot-cli" ]; then
    echo -e "${RED}Error: Run from project root${NC}"
    exit 1
fi

# Check that binaries are compiled
if [ ! -f "build-make/iso-snapshot-cli" ]; then
    echo -e "${RED}Error: Binaries not found in build-make/${NC}"
    echo "Run first: cd build-make && make"
    exit 1
fi

echo -e "${YELLOW}1. Backing up existing binaries...${NC}"

# Create backup directory with timestamp
BACKUP_DIR="/tmp/s4-snapshot-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"

# Backup existing binaries
if [ -f "/usr/bin/iso-snapshot-cli" ]; then
    sudo cp -v /usr/bin/iso-snapshot-cli "$BACKUP_DIR/"
    echo -e "${GREEN}✓ Backed up: /usr/bin/iso-snapshot-cli${NC}"
fi

echo ""
echo -e "${YELLOW}2. Installing new binaries...${NC}"

# Install iso-snapshot-cli
sudo cp -v build-make/iso-snapshot-cli /usr/bin/iso-snapshot-cli
sudo chmod 755 /usr/bin/iso-snapshot-cli
echo -e "${GREEN}✓ Installed: /usr/bin/iso-snapshot-cli${NC}"

echo ""
echo -e "${GREEN}=== Installation complete ===${NC}"
echo ""
echo -e "${YELLOW}Binaries backed up to: $BACKUP_DIR${NC}"
echo ""
echo "To restore old binaries:"
echo "  sudo ./restore_from_backup.sh $BACKUP_DIR"
echo ""
echo "To test:"
echo "  sudo iso-snapshot-cli --cli -d /home/snapshot -x Downloads -o"
echo ""

# Made with Bob
