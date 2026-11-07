# Pokémon World 3D – Reise zwischen den Regionen

Privates Mod- und Homebrew-Projekt, damit man mit **einem durchgehenden Spielstand** zwischen den eigenen Originalspielen reisen kann: Hoenn (Omega Rubin), Kalos (X) und Alola (Ultramond). Gespielt werden immer die Originalspiele selbst. Unser Teil überträgt Team, Boxen, Items, Geld usw. zwischen den Spielständen und startet das jeweilige Spiel. Läuft auf dem echten 3DS (Luma3DS) und, so weit möglich, in Azahar auf PC und Android.

## Abgrenzung (mit dem Nutzer so vereinbart)
- Erlaubt: Werkzeuge für die eigenen Spielstände, Mods der eigenen Spiele (LayeredFS, `code.ips`, 3GX-Plugin) mit eigenen Änderungen, z. B. geänderte Begegnungstabellen.
- Nicht Teil des Projekts: ein Spiel nachbauen (Karten, Modelle, Animationen, Musik in eigener Engine) oder Inhalte eines Spiels in ein anderes portieren (z. B. Kalos-Karten in Omega Rubin). Die frühere Idee einer eigenen 3D-Engine mit ORAS-Daten ist verworfen.
- Keine Spieldaten, Spielstände oder gepatchten ROMs committen, hochladen oder weitergeben.
- Mit übertragenen oder veränderten Spielständen nicht online gehen (GTS, Wundertausch, Online-Kämpfe).

## Ziel
1. **Übertragungskern (C++, `core/`)**: Spielstände von XY/ORAS (Gen 6) und USUM (Gen 7) lesen und schreiben, Prüfsummen, Pokémon ent-/verschlüsseln, Umwandlung PK6 ↔ PK7.
2. **Gemeinsamer Speicher** auf der SD (`sd:/pokemon-world-3d/`): jedes Pokémon in voller Form. Jedes Spiel bekommt die Version, die es versteht. Was ein Spiel nicht kennt (Alola-Arten, Alola-Formen, Gen-7-Attacken/-Fähigkeiten/-Items), wird abgespeckt oder als Platzhalter eingesetzt und beim Zurückreisen anhand der Kennung (EC/PID) wieder zusammengeführt. Keine echten Bad Eggs als Platzhalter (Datenverlust-Risiko).
3. **Trainer-ID übernehmen**: TID/SID/OT-Name des neuen Spielstands auf die des bisherigen setzen, damit alle Pokémon als eigene gelten und ohne Orden gehorchen.
4. **PC-Werkzeug** (`pc/`) zum Testen und als Ausweichlösung für Azahar (überträgt und startet `azahar.exe` mit dem Zielspiel).
5. **Reise-App** (3DS-Homebrew, .3dsx/CIA): Region wählen → Spielstand übertragen → Spiel starten. Vor jeder Übertragung Sicherung.
6. **Später**: Menüpunkt „Reisen“ im Spiel selbst (3GX-Plugin oder Code-Patch). Alle Pokémon der jeweiligen Region fangbar, in Omega Rubin auch die Alpha-Saphir-Exklusiven (Begegnungstabellen per LayeredFS ändern).
7. Nicht übertragbar: Story-Fortschritt, Orden, Ereignis-Flags (spielspezifisch).

## Stand (2026-11-07)
- `core/` und `pc/` angelegt: Gen-6-Spielstand (Blocktabelle + CRC16 prüfen/neu berechnen), PK6 ent-/verschlüsseln, Team/Boxen/Trainer auslesen, Suche nach gültigen PK6 im Spielstand (`scan`) zur Bestätigung der Offsets.
- **Offsets und Blocktabelle stammen aus dem Gedächtnis (Stand PKHeX) und sind noch nicht an echten Spielständen geprüft.** `pw3d-reise info` meldet, ob alle Block-Prüfsummen stimmen; `scan` findet die tatsächlichen Box-/Team-Offsets.
- Noch keine Spielstände vorhanden: Azahar wurde noch nie gestartet (kein `%APPDATA%\Azahar`). CIAs in Azahar installieren, je Spiel bis zum ersten Speichern spielen.
- Host-Compiler: WinLibs (MinGW g++ 16.2) per winget installiert, liegt unter `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_*\mingw64\bin` (in neuen Shells im PATH). Bauen: in `pc/` `mingw32-make` → `build/pw3d-reise.exe`.
- `pw3d-reise selbsttest` läuft durch: CRC-Testvektoren und PK6-Rundlauf (verschlüsseln/entschlüsseln) stimmen. Ob die Blocktabelle (Vertauschungstabelle der Blöcke A–D eingeschlossen) zu echten Daten passt, zeigt erst ein echter Spielstand.

## Nächste Schritte
1. Echte Spielstände von Omega Rubin und X aus Azahar holen (nach `saves/`, ignoriert), `info`/`scan` laufen lassen, Offsets bestätigen bzw. korrigieren.
2. Geld, Bag und Spielzeit bestätigen; Übertragung ORAS ↔ XY inkl. Trainer-ID; Rundlauf testen (übertragen, im Spiel laden, speichern, zurück).
3. Gen 7 (USUM): Blocktabelle, Prüfsummen, Signatur (siehe PKHeX `MemeCrypto`), PK7, Umwandlung, Platzhalter, gemeinsamer Speicher.
4. Reise-App (libctru: Spielstand-Zugriff wie Checkpoint, Titelstart per APT).

## Spielkennungen (Title-IDs, EU)
- Omega Rubin `000400000011C400`, Alpha Saphir `000400000011C500`, X `0004000000055D00`, Y `0004000000055E00`, Ultrasonne `00040000001B5000`, Ultramond `00040000001B5100`.
- Spielstand in Azahar: `%APPDATA%\Azahar\sdmc\Nintendo 3DS\<id0>\<id1>\title\00040000\<untere 8 Stellen>\data\00000001\main` (prüfen, sobald vorhanden).

## Vorhandene Werkzeuge
- `tools/extract-cia.mjs <cia> <ziel>`: RomFS/ExeFS aus den entschlüsselten CIAs entpacken (für LayeredFS-Mods). ORAS liegt in `extracted/oras` (ignoriert).
- `tools/lib/garc.mjs`: GARC, LZ11, GF-Pakete (für Begegnungstabellen nützlich).
- `tools/lib/bch.mjs`: BCH-Leser aus der verworfenen Engine-Idee, für das aktuelle Ziel nicht nötig.
- `Originalspiele/`: eigene entschlüsselte Dumps (ignoriert).
- devkitPro unter `C:\devkitPro` (devkitARM, libctru, citro2d, 3dsxtool, smdhtool; makerom/bannertool fehlen). Azahar: `C:\Program Files\Azahar\azahar.exe`. Node unter `C:\Program Files\nodejs` (in Bash nicht im PATH).

## Regeln
- Texte und Benutzeroberfläche auf Deutsch.
- Keine Hinweis-Popups, die den Spieler zu neuen Zielen führen.
- Spieldaten, Spielstände und konvertierte Daten nie committen.
- Vor jedem Schreiben in einen Spielstand eine Sicherung anlegen.
- Nur lokal committen, nie pushen.
- Commit-Nachrichten enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- In PowerShell nie `Set-Content` für Quelltexte (Umlaute gehen kaputt). Stattdessen `[IO.File]::WriteAllText` mit UTF-8 ohne BOM oder das Edit-Tool.
