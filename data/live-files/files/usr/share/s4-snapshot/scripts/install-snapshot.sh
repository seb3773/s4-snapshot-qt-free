#!/bin/sh
# Launch the Calamares installer configured for S4 Snapshot live ISOs.

set -e

CFG="/usr/share/s4-snapshot/scripts/configure_debian_calamares.sh"
if [ -x "$CFG" ]; then
    dash "$CFG"
fi

if command -v calamares >/dev/null 2>&1; then
    exec calamares -d "$@"
fi

echo "Calamares installer is not installed on this live system." >&2
exit 1
