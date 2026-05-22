#!/bin/bash
# Script de restauration des binaires originaux

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <backup_directory>"
    echo ""
    echo "Exemple: $0 /tmp/s4-snapshot-backup-20260519-040000"
    exit 1
fi

BACKUP_DIR="$1"

if [ ! -d "$BACKUP_DIR" ]; then
    echo "Erreur: Répertoire de backup non trouvé: $BACKUP_DIR"
    exit 1
fi

echo "=== Restauration des binaires originaux ==="
echo ""
echo "Backup source: $BACKUP_DIR"
echo ""

# Restaurer iso-snapshot-cli
if [ -f "$BACKUP_DIR/iso-snapshot-cli" ]; then
    sudo cp -v "$BACKUP_DIR/iso-snapshot-cli" /usr/bin/iso-snapshot-cli
    echo "✓ Restauré: /usr/bin/iso-snapshot-cli"
fi

# Restaurer le répertoire lib
if [ -d "$BACKUP_DIR/iso-snapshot-cli" ]; then
    sudo rm -rf /usr/lib/iso-snapshot-cli
    sudo cp -rv "$BACKUP_DIR/iso-snapshot-cli" /usr/lib/
    echo "✓ Restauré: /usr/lib/iso-snapshot-cli/"
fi

echo ""
echo "=== Restauration terminée ===${NC}"
echo ""
echo "Le backup est toujours disponible dans: $BACKUP_DIR"
echo "Vous pouvez le supprimer avec: sudo rm -rf $BACKUP_DIR"
echo ""

# Made with Bob
