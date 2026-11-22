#!/bin/bash
# Installs W-Shaper (AU + VST3) in your user folder
cd "$(dirname "$0")" || exit 1

COMP_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$COMP_DIR" "$VST_DIR"

rm -rf "$COMP_DIR/W-Shaper.component" "$VST_DIR/W-Shaper.vst3"
cp -R *.component "$COMP_DIR/"
cp -R *.vst3 "$VST_DIR/"

xattr -dr com.apple.quarantine "$COMP_DIR/W-Shaper.component" 2>/dev/null
xattr -dr com.apple.quarantine "$VST_DIR/W-Shaper.vst3" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null

echo ""
echo "Done. Restart Logic: you will find it under Audio Units > CONTROLUNA > W-Shaper."
