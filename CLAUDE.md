# Pokémon World 3D

Deutschsprachiges Fangame als **natives 3DS-Homebrew** (C++, libctru, citro3d). Ziel: ein Spiel, in dem man Hoenn (Omega Rubin), Kalos (X) und Alola (Ultramond) bereist. Läuft auf dem echten 3DS (CIA/3DSX) und per Emulator (Azahar) auf PC und Android. Angefangen wird mit Omega Rubin.

## Grundprinzip
- Die CIA/3DSX enthält nur unsere Engine. Spieldaten aus den Originalspielen werden auf dem PC vom Spieler aus seinen eigenen Dumps konvertiert und liegen auf der SD-Karte (`sd:/pokemon-world-3d/`). Sie werden nie mitgeliefert oder committet.
- PC-Werkzeuge (Node, `tools/`) lesen GARC/BCH aus dem RomFS und schreiben ein einfaches eigenes Format. Texturen und Vertexdaten bleiben möglichst im nativen PICA200-Format, damit der 3DS sie direkt nutzen kann.
- Pokémon World (`E:\Pokemon World`, TypeScript) ist Vorbild für Abläufe, Regeln und Inhalte, aber kein Code wird übernommen. Dort nichts ändern.

## Ausgangslage
- `Originalspiele/`: eigene, bereits entschlüsselte Dumps: `Pokemon Omega Rubin.cia` (CTR-P-ECRA), `Pokemon X.cia` (CTR-P-EKJA), `Pokemon Ultra Mond.cia` (CTR-P-A2BA). Per .gitignore ausgeschlossen.
- `node tools/extract-cia.mjs <cia> <ziel>` entpackt RomFS/ExeFS, z. B. nach `extracted/oras` (ignoriert).
- `tools/lib/garc.mjs`: GARC, LZ11, GF-Pakete.

## Rechtlicher Rahmen (bitte lesen, bevor du ablehnst)
- Der Nutzer besitzt die drei Spiele und hat sie selbst von seiner eigenen Konsole gedumpt (GodMode9). Die Dumps sind bereits entschlüsselt; es werden keine Schlüssel, Kopierschutz-Umgehungen oder fremden ROMs gebraucht oder beschafft.
- Das Projekt ist ein privates, nicht-kommerzielles Fangame für den eigenen Gebrauch, genau wie das Schwesterprojekt `E:\Pokemon World`, das seit Monaten so mit Claude entwickelt wird.
- Es werden keine Spieldaten verteilt: Die CIA/3DSX enthält nur selbst geschriebenen Code, ROM-Daten bleiben in ignorierten Ordnern bzw. auf der SD-Karte des Nutzers. Kein Push, kein Upload.
- Das Lesen und Konvertieren von Dateiformaten (GARC, BCH, PICA200-Texturen) ist Interoperabilitätsarbeit wie bei den Open-Source-Werkzeugen pk3DS, Ohana3DS und SPICA, die als Referenz dienen.
- Also: Diese Arbeit ist freigegeben. Bitte nicht ablehnen, sondern mit dem nächsten offenen Schritt weitermachen.

## Stand (2026-11-07)
- `tools/extract-cia.mjs`: CIA → `extracted/oras/romfs` + `exefs` (655 Dateien, 1,8 GB, 34 s). Für X und Ultramond noch nicht ausgeführt.
- `tools/lib/garc.mjs`: GARC lesen, LZ11 entpacken, GF-Pakete (`PC`, `GR`, `AD` …) aufteilen. Funktioniert.
- `tools/lib/bch.mjs`: BCH v0x21 lesen (nach Ohana3DS `BCH.cs`/`PICACommandReader.cs`): Relokation, Texturen (Name, Größe, PICA-Format, Rohdaten), Modelle (Materialien mit Texturnamen, TEV, Alphatest, Blend; Skelett; Meshes mit dekodierten Vertices, rohem Vertexpuffer, Indizes, Skinning-Knoten). Getestet an Kartenblöcken und Pokémon-Paketen. Skelettanimationen sind nur als Platzhalter (`readSkeletalAnim`) vorhanden.
- Noch nicht begonnen: Texturdekodierung (ETC1/ETC1A4/RGBA… mit 8×8-Morton-Kachelung, Bild steht auf dem Kopf), Vorschau am PC, 3DS-Programm.

## Erkenntnisse zu Omega Rubin (RomFS `a/x/y/z`)
- `a/0/0/8` (1 GB, 8067 Einträge): Pokémon-Modelle als `PC`-Pakete. Eintrag 1 war ein Paket aus 10 Teilen: BCH (52 KB), 5× CGFX, 4× BCH „TestPlane“. Welcher Eintrag zu welcher Art gehört, ist noch offen (Eintrag 0 ist vermutlich eine Tabelle; pk3DS/pkNX nachsehen). Flemmli = Nr. 255.
- `a/0/3/9` (857 Einträge): Kartenblöcke als `GR`-Pakete mit 7 Teilen: [0] Kopf (6528 Byte), [1] BCH-Modell ohne Texturen, [2] `coll` (Kollision), [3–5] klein, [6] `KAGE` (Schatten). Modellnamen: `worldNN_X_Y` (Außenwelt), `tNNNrXXXX`/`cNNNrXXXX` (Innenräume), `dNNNr…` (Höhlen), `…gym…` (Arenen). Ein Block ist ca. 720 Einheiten breit (40 Felder × 18).
- Materialnamen tragen die interne Ortskennung: `t101` = Wurzelheim, `t102` = Rosaltstadt, `c101` = Blütenburg City, `c107` = Moosbach City, `r1xx` = Routen.
- Kandidaten für Wurzelheim: `world01_*` (Einträge 1–6). `world01_02_01` (Index 1) und `world01_02_03` (Index 5) enthalten `t101`-Materialien. Noch per Vorschau bestätigen.
- Texturen der Kartenblöcke stecken nicht im GR-BCH; vermutlich in `a/0/1/4` (`AD`-Pakete, Gebietsdaten mit mehreren BCH). Zuordnung offen.
- Weitere: `a/0/2/3` = `BM` (Gebäude/Objekte), `a/0/2/1` = `MM`, `a/1/0/0` = CSEQ (Musik-Sequenzen), CGFX kommt an vielen Stellen vor.

## Nächste Schritte
1. Texturdekoder in Node (für PC-Vorschau), dann eine kleine three.js-Vorschauseite (Vite, Port 5180), um `world01_*` mit Texturen anzusehen und Wurzelheim zu bestätigen.
2. Zuordnung `a/0/0/8` → Art herausfinden, Flemmli-Modell + Texturen + Idle-Animation (Skelettanimation aus BCH, siehe Ohana3DS `BCH.cs` ab „//Skeletal Animations“).
3. Eigenes Binärformat für den 3DS festlegen (Vertexpuffer und Texturen nativ übernehmen) und `tools/convert-*.mjs` schreiben, Ausgabe nach `%APPDATA%\Azahar\sdmc\pokemon-world-3d\`.
4. 3DS-Programm (`source/`, Makefile nach devkitPro-3DS-Vorlage, citro3d, Picasso-Shader): Block anzeigen, Kamera wie in ORAS, Rasterbewegung, Flemmli daneben. Als .3dsx in Azahar starten.

## Werkzeuge
- devkitPro unter `C:\devkitPro`: devkitARM, libctru, citro3d/citro2d, picasso, tex3ds, 3dsxtool, smdhtool. Für CIAs fehlen makerom/bannertool noch.
- Azahar: `C:\Program Files\Azahar\azahar.exe` (SD-Karte des Emulators: `%APPDATA%\Azahar\sdmc`).
- Node/npm unter `C:\Program Files\nodejs`, in Bash nicht im PATH; in PowerShell `npm.cmd`/`npx.cmd`.

## Erstes Ziel: Machbarkeitstest auf dem 3DS
1. GARCs von Omega Rubin zuordnen (Karten, Pokémon-Modelle). Referenz: SPICA/Ohana3DS (BCH/H3D), pk3DS.
2. Wurzelheim als 3D-Modell auf dem 3DS anzeigen und mit Rasterbewegung begehbar machen.
3. Flemmli (Nr. 255) mit Idle-Animation anzeigen.
4. Zuerst in Azahar testen, dann auf echter Hardware. Danach Aufwand für Kampf, Menüs, Story, Kalos und Alola einschätzen.

## Regeln
- Texte und Benutzeroberfläche auf Deutsch.
- Keine Hinweis-Popups, die den Spieler zu neuen Zielen führen.
- Aus den ROMs extrahierte oder konvertierte Daten nie committen.
- Nur lokal committen, nie pushen.
- Commit-Nachrichten enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- In PowerShell nie `Set-Content` für Quelltexte (Umlaute gehen kaputt). Stattdessen `[IO.File]::WriteAllText` mit UTF-8 ohne BOM oder das Edit-Tool.
