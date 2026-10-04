#!/bin/bash
# Builds fanguard and installs it as a LaunchDaemon (root).
#   sudo ./install.sh            install or update
#   sudo ./install.sh --remove   uninstall, fans back on automatic
# The binary is copied to /usr/local/mac-fan-guard so the service never runs from a synced or protected folder.
set -e
[ "$(id -u)" = 0 ] || { echo "Please run with sudo."; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST=/usr/local/mac-fan-guard
LABEL=io.github.hdzucht.mac-fan-guard
PLIST=/Library/LaunchDaemons/$LABEL.plist
MODEDIR=/Users/Shared/mac-fan-guard

if [ "$1" = "--remove" ]; then
    launchctl bootout system "$PLIST" 2>/dev/null || true
    [ -x "$DEST/fanguard" ] && "$DEST/fanguard" auto || true
    rm -f "$PLIST"; rm -rf "$DEST"
    echo "Removed. Fans are back on automatic. ($MODEDIR is left in place.)"; exit 0
fi

mkdir -p "$DEST" "$MODEDIR"
clang -O2 -o "$DEST/fanguard" "$HERE/fanguard.c" -framework IOKit -framework CoreFoundation
chown root:wheel "$DEST/fanguard"; chmod 755 "$DEST/fanguard"
[ -f "$DEST/fanguard.conf" ] || install -m 644 -o root -g wheel "$HERE/fanguard.conf" "$DEST/fanguard.conf"
[ -f "$MODEDIR/mode" ] || echo curve > "$MODEDIR/mode"
chmod 777 "$MODEDIR"; chmod 666 "$MODEDIR/mode"

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$DEST/fanguard</string><string>run</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/var/log/mac-fan-guard.log</string>
  <key>StandardErrorPath</key><string>/var/log/mac-fan-guard.log</string>
</dict></plist>
EOF
chown root:wheel "$PLIST"; chmod 644 "$PLIST"
launchctl bootout system "$PLIST" 2>/dev/null || true
launchctl bootstrap system "$PLIST"
sleep 3
"$DEST/fanguard" status
echo "Settings: $DEST/fanguard.conf (kept on update) · Log: tail -f /var/log/mac-fan-guard.log"
