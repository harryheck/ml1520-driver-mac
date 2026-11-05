#!/bin/bash
# Installs the ML-1520 printer application for the current user. Does not need sudo.
# To undo: ./uninstall.sh
set -euo pipefail
cd "$(dirname "$0")"

APP_DIR="$HOME/Library/ML1520"
LABEL=local.ml1520.printer
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/ML1520.log"
PORT=18631
QUEUE=Samsung_ML_1520
URI="ipp://localhost:$PORT/ipp/print"
DOMAIN="gui/$(id -u)"

[ "$(uname -m)" = arm64 ] || { echo "This build only runs on Apple Silicon Macs." >&2; exit 1; }
[ -x /usr/bin/ippeveprinter ] || { echo "This macOS version has no /usr/bin/ippeveprinter." >&2; exit 1; }
[ -x build/ml1520-print ] || ./build.sh

launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true

mkdir -p "$APP_DIR/spool" "$HOME/Library/LaunchAgents"
install -m 755 build/ml1520-print "$APP_DIR/ml1520-print"
install -m 644 driver/ml1520.conf "$APP_DIR/ml1520.conf"
# Files from a browser download carry a quarantine flag, and install keeps it. Without this
# step macOS would refuse to run the program in the background.
xattr -d com.apple.quarantine "$APP_DIR/ml1520-print" 2>/dev/null || true

cat > "$PLIST" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/ippeveprinter</string>
        <string>-n</string><string>localhost</string>
        <string>-r</string><string>off</string>
        <string>-p</string><string>$PORT</string>
        <string>-a</string><string>$APP_DIR/ml1520.conf</string>
        <string>-c</string><string>$APP_DIR/ml1520-print</string>
        <string>-d</string><string>$APP_DIR/spool</string>
        <string>-D</string><string>file:///dev/null</string>
        <string>--no-web-forms</string>
        <string>Samsung ML-1520</string>
    </array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>ThrottleInterval</key><integer>10</integer>
    <key>StandardOutPath</key><string>$LOG</string>
    <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
PLIST_EOF
plutil -lint -s "$PLIST"
launchctl bootstrap "$DOMAIN" "$PLIST"

for _ in $(seq 1 40); do
    curl -s -o /dev/null -m 1 "http://127.0.0.1:$PORT/" && break
    perl -e 'select(undef,undef,undef,0.25)'
done
curl -s -o /dev/null -m 1 "http://127.0.0.1:$PORT/" || {
    echo "The print service did not start. See $LOG" >&2; exit 1; }

lpadmin -p "$QUEUE" -D "Samsung ML-1520" -E -v "$URI" -m everywhere
lpadmin -p "$QUEUE" -o printer-is-shared=false

lpstat -v "$QUEUE"
echo "Done. Choose \"Samsung ML-1520\" in the print dialog."
