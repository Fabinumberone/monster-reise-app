#!/bin/sh
# Stellt die Dateien für ein GitHub-Release zusammen (build/release/):
#   monster-reise-app.cia     (vorher in app/ „make cia“)
#   monster-reise-daten.html  (Daten-Werkzeug für den Browser, Windows/Mac/Linux)
# Enthält keine Spieldaten.
set -e
cd "$(dirname "$0")/.."
out=build/release
rm -rf "$out" && mkdir -p "$out"
cp app/monster-reise-app.cia "$out/"
node tools/gen-html.mjs "$out/monster-reise-daten.html"
ls -la "$out"
