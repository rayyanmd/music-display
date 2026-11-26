#!/bin/sh
# Build release, wrap in MusicDisplay.app, sign with "Music Display Dev", install to ~/Applications, launch.
set -e
cd "$(dirname "$0")/.."
swift build -c release
APP=build/MusicDisplay.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$(swift build -c release --show-bin-path)/MusicDisplay" "$APP/Contents/MacOS/MusicDisplay"
cp Resources/Info.plist "$APP/Contents/Info.plist"
codesign --force --options runtime --timestamp=none --sign "Music Display Dev" \
  --identifier com.musicdisplay.helper --entitlements Resources/MusicDisplay.entitlements "$APP"
codesign --verify --strict "$APP"
mkdir -p "$HOME/Applications"
pkill -x MusicDisplay 2>/dev/null && sleep 1 || true
rm -rf "$HOME/Applications/MusicDisplay.app"
cp -R "$APP" "$HOME/Applications/"
open "$HOME/Applications/MusicDisplay.app"
echo "Installed and launched ~/Applications/MusicDisplay.app"
