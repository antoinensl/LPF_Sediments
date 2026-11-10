#!/bin/bash
# Sediments - installation pour macOS
cd "$(dirname "$0")" || exit 1

echo "Sediments - installation"
echo

VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3" "$AU"

rm -rf "$VST3/Sediments.vst3" "$AU/Sediments.component"
cp -R Sediments.vst3 "$VST3/" && echo "VST3 : $VST3/Sediments.vst3"
cp -R Sediments.component "$AU/" && echo "AU   : $AU/Sediments.component"

# Downloaded files are quarantined by macOS; the plugin is not signed by Apple.
xattr -dr com.apple.quarantine "$VST3/Sediments.vst3" "$AU/Sediments.component" 2>/dev/null
if [ -d Sediments.app ]; then
    rm -rf "/Applications/Sediments.app"
    cp -R Sediments.app /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine /Applications/Sediments.app 2>/dev/null \
        && echo "App  : /Applications/Sediments.app"
fi

# Ask macOS to rescan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null

echo
echo "TERMINE ! Ouvre (ou rouvre) Reaper : Sediments apparait dans la liste des FX."
echo "Si ce n'est pas le cas : Options > Preferences > Plug-ins > VST > Re-scan."
echo
read -r -p "Appuie sur Entree pour fermer cette fenetre."
