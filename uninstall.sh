#!/bin/bash
# Removes the print queue, the print service and all installed files.
set -euo pipefail

lpadmin -x Samsung_ML_1520 2>/dev/null || true
launchctl bootout "gui/$(id -u)/local.ml1520.printer" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/local.ml1520.printer.plist" "$HOME/Library/Logs/ML1520.log"
rm -rf "$HOME/Library/ML1520"
echo "Removed."
