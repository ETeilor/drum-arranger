#!/bin/bash
# Builds the macOS installer from the Release artefacts:
#   Drum Arranger.component -> /Library/Audio/Plug-Ins/Components
#   Drum Arranger.vst3      -> /Library/Audio/Plug-Ins/VST3
#   Drum Arranger.app       -> /Applications
# Result: dist/DrumArranger-<ver>.pkg and dist/DrumArranger-<ver>.dmg (the dmg holds the pkg and a readme).
set -euo pipefail
VER="${1:-0.1.0}"
ART="build/DrumArranger_artefacts/Release"
OUT="dist"
WORK="$(mktemp -d)"
HERE="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$OUT" && mkdir -p "$OUT"

stage() {   # stage <bundle> <subdir>
    mkdir -p "$WORK/$2"
    cp -R "$1" "$WORK/$2/"
    # Apple Silicon refuses unsigned code: ad-hoc signature (no Apple certificate needed)
    codesign --force --deep --sign - "$WORK/$2/$(basename "$1")"
    codesign --verify --deep --strict "$WORK/$2/$(basename "$1")"
}
LIVE="build/DrumArrangerLive_artefacts/Release"
stage "$ART/AU/Drum Arranger.component" au
stage "$LIVE/AU/Drum Arranger Live.component" au
stage "$ART/VST3/Drum Arranger.vst3" vst3
stage "$LIVE/VST3/Drum Arranger Live.vst3" vst3
stage "$ART/Standalone/Drum Arranger.app" app

# the same postinstall in every component package: it only touches paths that exist
mkdir -p "$WORK/scripts"
cp "$HERE/postinstall" "$WORK/scripts/postinstall"
chmod +x "$WORK/scripts/postinstall"

for part in au:Components vst3:VST3 app:; do
    name="${part%%:*}"
    dir="${part#*:}"
    loc="/Library/Audio/Plug-Ins/$dir"
    [ "$name" = app ] && loc="/Applications"
    pkgbuild --root "$WORK/$name" --install-location "$loc" --scripts "$WORK/scripts" \
             --identifier "com.teilormusic.drumarranger.$name" --version "$VER" "$WORK/$name.pkg"
done

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Drum Arranger $VER</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/><line choice="vst3"/><line choice="app"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro)"><pkg-ref id="com.teilormusic.drumarranger.au"/></choice>
    <choice id="vst3" title="VST3"><pkg-ref id="com.teilormusic.drumarranger.vst3"/></choice>
    <choice id="app" title="Standalone app"><pkg-ref id="com.teilormusic.drumarranger.app"/></choice>
    <pkg-ref id="com.teilormusic.drumarranger.au">au.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.drumarranger.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.drumarranger.app">app.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/DrumArranger-$VER.pkg"

mkdir -p "$WORK/dmg"
cp "$OUT/DrumArranger-$VER.pkg" "$WORK/dmg/Install Drum Arranger.pkg"
cat > "$WORK/dmg/Read me.txt" <<'EOF'
Drum Arranger: установка

1. Дважды щёлкните «Install Drum Arranger.pkg».
   Если macOS пишет, что разработчик не установлен: System Settings → Privacy & Security →
   внизу «Open Anyway», затем снова откройте установщик.
2. Установщик положит плагин в /Library/Audio/Plug-Ins (AU и VST3) и приложение в Applications.
3. Перезапустите Logic Pro. Плагин: дорожка программного инструмента →
   AU Instruments → Teilormusic → Drum Arranger.
   Живой режим: на дорожке с барабанной библиотекой слот MIDI FX →
   Audio Units → Teilormusic → Drum Arranger Live.
   Если Logic его не показал: Logic Pro → Settings → Plug-in Manager, выберите Drum Arranger,
   нажмите «Reset & Rescan Selection».
EOF
hdiutil create -volname "Drum Arranger $VER" -srcfolder "$WORK/dmg" -ov -format UDZO "$OUT/DrumArranger-$VER.dmg"
ls -la "$OUT"
