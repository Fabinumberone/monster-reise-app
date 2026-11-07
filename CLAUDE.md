# Pokémon World 3D

Deutschsprachiges Fangame auf Basis der 3DS-Pokémon-Spiele, angefangen mit Pokémon Omega Rubin.

## Ausgangslage
- `Originalspiele/` enthält die eigenen Dumps des Nutzers: `Pokemon Omega Rubin.cia` (1,9 GB), `Pokemon X.cia`, `Pokemon Ultra Mond.cia`. Der Ordner ist per .gitignore ausgeschlossen.
  - Ob die CIAs entschlüsselt sind, ist noch nicht geprüft. Falls nicht, muss der Nutzer sie selbst entschlüsseln (z. B. GodMode9 auf der eigenen Konsole). Keine Schlüssel aus dem Netz besorgen.
- Entpackte und konvertierte ROM-Daten kommen in ignorierte Ordner (z. B. `extracted/`, `public/assets/`) und werden nie committet.
- Vorbild ist `E:\Pokemon World`: TypeScript, Phaser 4, Vite, Electron, Capacitor-Android, three.js für 3D-Karten.
  - Importer: `tools/import-nds.ts`, `tools/lib/mapbake.ts`, `tools/lib/export3d.ts`.
  - 3D-Darstellung: `src/world/map3d.ts`.
  - Engine-Teile dürfen hierher kopiert werden: Menüs, Kampfsystem, Speichern, Build-Pipeline.
  - Am Repo `E:\Pokemon World` nichts ändern.

## Erstes Ziel: Machbarkeitstest
1. RomFS/ExeFS aus der CIA entpacken (z. B. ctrtool/3dstool oder eigenes Node-Tooling).
2. GARC-Archive von Omega Rubin identifizieren (Referenz: pk3DS, pkNX, SPICA/Ohana3DS) und eine Stadt (z. B. Wurzelheim) als 3D-Modell mit Texturen nach glTF exportieren, mit Kollisionsraster.
3. Die Stadt in three.js laufbar machen, mit Rasterbewegung wie in ORAS.
4. Ein Pokémon-Modell (z. B. Flemmli, Nr. 255) samt Idle-Animation exportieren und in einer Kampfszene zeigen.
5. Danach den Aufwand für den Rest einschätzen: Daten, Texte, Musik (BCSTM), Skripte, Figuren, Mega-Entwicklung, Feen-Typ.

## Regeln
- Texte und Benutzeroberfläche auf Deutsch.
- Keine Hinweis-Popups, die den Spieler zu neuen Zielen führen.
- Nur lokal committen, nie pushen.
- Commit-Nachrichten enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- In PowerShell nie `Set-Content` für Quelltexte (Umlaute gehen kaputt). Stattdessen `[IO.File]::WriteAllText` mit UTF-8 ohne BOM oder das Edit-Tool.
- Node/npm liegen unter `C:\Program Files\nodejs`, sind in Bash aber nicht im PATH. In PowerShell `npm.cmd`/`npx.cmd` benutzen.
- Die Ports 5173/5174 sind vom alten Projekt belegt. Der Dev-Server bekommt einen anderen Port.
