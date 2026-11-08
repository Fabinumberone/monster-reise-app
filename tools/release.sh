#!/bin/sh
# Stellt die Dateien für ein GitHub-Release zusammen (build/release/):
#   monster-reise-app.cia     (vorher in app/ „make cia“)
#   monster-reise-daten.zip   (Daten-Werkzeug: daten.mjs + lib, Startdatei)
# Enthält keine Spieldaten.
set -e
cd "$(dirname "$0")/.."
out=build/release
rm -rf "$out" && mkdir -p "$out/monster-reise-daten/lib"
cp app/monster-reise-app.cia "$out/"
cp tools/daten.mjs "tools/Daten erzeugen.bat" "$out/monster-reise-daten/"
for f in container monsterdaten garc gftext pica ncch; do cp "tools/lib/$f.mjs" "$out/monster-reise-daten/lib/"; done
cp LICENSE "$out/monster-reise-daten/"
(cd "$out" && powershell.exe -NoProfile -Command "Compress-Archive -Path monster-reise-daten -DestinationPath monster-reise-daten.zip -Force")
rm -rf "$out/monster-reise-daten"
ls -la "$out"
