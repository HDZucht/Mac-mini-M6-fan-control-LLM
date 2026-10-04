#!/bin/bash
# Builds "Fan Guard.app" into ~/Applications and starts it; the app registers itself as a login item. No sudo needed.
#   ./build.sh            build or update
#   ./build.sh --remove   quit and delete the app
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$HOME/Applications/Fan Guard.app"

if [ "$1" = "--remove" ]; then
    pkill -x FanGuardMenu 2>/dev/null || true; rm -rf "$APP"; echo "Menu bar app removed."; exit 0
fi

mkdir -p "$APP/Contents/MacOS"
swiftc -O -o "$APP/Contents/MacOS/FanGuardMenu" "$HERE/FanGuardMenu.swift"
clang -O2 -o "$APP/Contents/MacOS/fanguard" "$HERE/../fanguard.c" -framework IOKit -framework CoreFoundation
cp "$HERE/../fanguard.conf" "$APP/Contents/MacOS/fanguard.conf"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>io.github.hdzucht.mac-fan-guard.menu</string>
  <key>CFBundleName</key><string>Fan Guard</string>
  <key>CFBundleExecutable</key><string>FanGuardMenu</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0.0</string>
  <key>LSUIElement</key><true/>
</dict></plist>
EOF
codesign --force --deep -s - "$APP"
pkill -x FanGuardMenu 2>/dev/null || true
sleep 1
open "$APP"
echo "Fan Guard menu running: $APP"
