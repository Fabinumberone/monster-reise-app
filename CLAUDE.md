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
- **Gen 6 an echten Spielständen bestätigt** (Sicherungen in `saves/orig/`, ignoriert): ORAS 58 Blöcke, XY 55 Blöcke, alle CRC16-CCITT korrekt. Trainer (0x14000), Team (0x14200) und PK6-Entschlüsselung inkl. Vertauschungstabelle stimmen (OR: Flemmli, X: Froxy, je Level 5). Boxen noch leer; leere Plätze sind verschlüsselte Leerdatensätze (Art 0, Spitzname „Ei“) und entschlüsseln an den angenommenen Box-Offsets sauber.
- **Ultramond (USUM, 0x6CC00) erste Untersuchung**: Blocktabelle gleich aufgebaut (BEEF bei Ende−0x1F0, 39 Blöcke, 0x200-Ausrichtung), Prüfsumme CRC16 gespiegelt+invertiert (`crc16Invert`). Team bei 0x1600 (Block 04, gleicher Aufbau wie Gen 6, PK7 mit gleicher Verschlüsselung, Flamiau Nr. 725), Trainer-TID/SID bei 0x1400 (Block 03).
- **USUM im Kern** (`core/save7.*`): MyStatus 0x1400 (TID, SID, Spiel +4, Geschlecht +5, Sprache +0x35, Name +0x38), Geld 0x4404 (u32), Team 0x1600, Boxen 0x5200 (32 × 30 × 0xE8, Block 0x0E). Alle 39 CRCs werden korrekt berechnet (Block 0x24 mit genullter Signatur).
- **Signatur wird geprüft**: Erster Test (nur CRCs neu) → „The saved game data is corrupted“. Sicherung zurückgespielt.
- **Signatur nachgebaut** (`core/memecrypto.*`, `core/crypto.*`: SHA-1, SHA-256, AES-128, modPow ohne Fremdbibliotheken): Verfahren und Schlüssel „PokedexAndSaveFile“ nach PKHeX `MemeCrypto.cs`/`MemeKey.cs` (GPLv3; Projekt bleibt privat). Neu-Signieren des unveränderten UM-Spielstands ergibt byte-genau die Originalsignatur. `info` zeigt Signaturstatus, `geld` signiert nach den CRCs neu.
- **Zweiter Signaturtest erfolgreich**: Geld 5000 → 5123 mit neuer Signatur, UM lädt ohne Fehler, Spiel speicherte danach 5223 ₽ (eigene Signatur gültig). Danach auf Wunsch 99999 ₽ gesetzt.
- **Gemeinsame Schnittstelle** `core/savefile.*` (`SaveFile`, `openSave`): Save6 und Save7 mit Team/Boxen lesen und schreiben, `addToParty`, `adoptTrainer`, `registerDex`, `finalize` (CRCs, bei Gen 7 danach Signatur). PC-Befehle: `info`, `scan`, `pokedex`, `trainer`, `kopiere`, `insteam`, `geld`, `signieren`.
- **Pokédex Gen 7** (USUM-Block 0x2C00, nach PKHeX Zukan7): gefangen +0x88, gesehen +0xF0 (4 × 0x8C), angezeigt +0x320, Sprachbits +0x550 (9 je Art, Deutsch = Index 4). Formen liegen als Bits hinter 807 (noch nicht gesetzt). `pokedex` auf alle Originale (OR, X, UM inkl. Signatur) ändert kein Byte.
- **Umwandlung Gen 6 → 7** (`core/convert.*`, nach PKHeX `PK6.ConvertToPK7`): Markierungen 2-Bit, Formargument umgelegt, alte Felder geleert. Kein Fähigkeitenabgleich (Personal-Tabellen fehlen), AP nicht aufgefüllt, keine HT-Erinnerungen.
- **Übertragung nach UM angewendet** (2026-11-07): UM übernimmt Trainer aus OR (65014/22666, Anzeige-ID 503990; Flamiau umgeschrieben), Flemmli (aus OR) und Froxy (aus X) als Kopien ins Team (Platz 2, 3). Sicherung `saves/backup/um-20261107-142917.main`. Test im Spiel steht aus. Noch offen: Trainerdaten an anderen Stellen (z. B. Festival Plaza) werden nicht angepasst.
- **UM im Spiel bestätigt**: Flemmli/Froxy im Team, Fünfeck-Symbol, „Hat Raum und Zeit überwunden“, Herkunft Hoenn/Kalos. Trainerpass und Flamiau zeigen 503990, Gen-6-Pokémon zeigen die 5-stellige 65014 (normal für Gen-6-Herkunft). Keine Tausch-EP, weil sie als eigene gelten (gewollt).
- **Sonne/Mond im Kern** (nach PKHeX SaveBlockAccessor7SM): MyStatus 0x1200, Team 0x1400, Boxen 0x4E00 (32), Geld 0x4004, Dex 0x2A00 (Sprachbits +0x550), max. Art 802, Signatur 0x6BB00 über 0x140 Byte Tabelle. An Sonne bestätigt: 37 CRCs + Signatur gültig, `pokedex` + Neu-Signieren ändert kein Byte.
- **Übertragung nach Sonne angewendet**: Trainer aus OR übernommen (Bauz umgeschrieben), Flemmli + Froxy ins Team (Platz 2, 3). Sicherung `saves/backup/sonne-20261107-144254.main`. Danach Flamiau aus UM (Teamplatz 3) als Platz 4, im Pokédex eingetragen (vorher nicht vorhanden). Test im Spiel steht aus.
- **Gemeinsamer Speicher** (`core/bank.*`, Entscheidungen des Nutzers 2026-11-07): Alles reist mit (Team + 32 Boxen, wie ein gemeinsamer PC). Was ein Spiel nicht kennt, wird dort **gar nicht angezeigt** (kein Platzhalter) und bleibt nur im Speicher. Einrichten mit Startspiel; Pokémon, die es nur in anderen Spielständen gibt, kommen in freie Plätze, Duplikate (EC+PID) fallen weg, Startspiel gewinnt.
  - Speicher hält je Pokémon die Gen-7-Form plus den letzten Gen-6-Stand; Gen-6-eigene Felder (Ami, Geodaten, Supertraining) und Gen-7-eigene (Hypertraining, Resort, farbige Markierungen) werden beim Hin- und Herreisen wiederhergestellt.
  - `abreisen`: Spielinhalt wird neuer Stand; was beim Ankommen geschrieben wurde und fehlt, gilt als freigelassen; nicht angezeigte Pokémon behalten ihren Platz (Team: alter Index). `ankommen`: Team/Boxen schreiben, leere Plätze mit dem Leerdatensatz des Spiels, Pokédex, Trainer.
  - Sichtbarkeit (`fitsIn`): Art, Attacken, Grundattacken, Item, Fähigkeit, Ball je Spiel; Gen 6 nur Herkunft ≤ Gen 6 (Version ≤ 27, konservativ: Gen-7-gefangene Pokémon alter Arten sind in Gen 6 unsichtbar), keine Alola-Formen; SM ohne USUM-Formen. Box 32 nur in Gen 7.
  - Befehle: `speicher-neu <speicher> <start> [weitere]`, `speicher-info`, `reise <speicher> <von> <nach>`.
  - Getestet an Kopien aller vier Spielstände: 8 Reisen (u. a. UM→OR→SO→X→UM, OR→OR) → alle Pokémon bytegleich und an alten Plätzen, alle CRCs/Signaturen gültig, OR→OR bytegleich. Freilassen/Verschieben/Gen-6-Felder noch nicht im Spiel getestet.
- **Speicher echt eingerichtet** (2026-11-07 14:58): `%APPDATA%\Azahar\sdmc\pokemon-world-3d\speicher.bin`, Startspiel Ultramond, weitere X, OR, Sonne. 6 Pokémon: Froxy, Flemmli, Flamiau, Taubsi, Zigzachs, Bauz. Danach UM→UM, damit UM alle 6 im Team hat. Aktuelles Spiel: USUM. Sicherungen vorher: `saves/backup/*-20261107-145848-vor-speicher.main`. OR, X und Sonne enthalten noch alte Stände/Testkopien, bis man dorthin reist.
- **Reisen bis zur App**: `tools\reise.ps1 -Von UM -Nach OR` (UTF-8 mit BOM, sonst kaputte Umlaute in PowerShell 5.1). Prüft laufendes Spiel am Fenstertitel, sichert beide Spielstände und den Speicher nach `sdmc\pokemon-world-3d\sicherungen\`, lehnt falsches Abreisespiel ab.
- **Noch nicht übertragen**: Items/Beutel, Geld, Spielzeit. Wunsch für die Reise-App: Team, PC-Boxen und Items ansehen können.
- Lehre: Teamreihenfolge ändert sich, wenn der Nutzer spielt. `insteam` nimmt optional die erwartete Art und bricht sonst ab; vor Übertragungen immer das Quellteam anzeigen. Laufendes Spiel am Fenstertitel erkennen („Azahar … | Spielname“), Azahar ohne Spiel ist unbedenklich. PKHeX ist beim Nutzer installiert (Gegenkontrolle).
- **Signatur in USUM**: Block 0x24 bei 0x6C000 (0x200 Byte) enthält ab +0x100 eine 0x80-Byte-Signatur (MemeCrypto). Die CRC dieses Blocks wird mit genullter Signatur berechnet. Offen: ob das Spiel die Signatur beim Laden prüft (Test: harmlos ändern, nur CRCs neu, in Azahar laden). Falls ja, siehe PKHeX `MemeCrypto`.
- **Erster Schreibtest erfolgreich** (2026-11-07): Flemmli aus OR per `pw3d-reise kopiere` in X Box 1/Platz 1 geschrieben, X lädt in Azahar, Flemmli ist da. Box-Offset XY (0x22600) damit bestätigt. Pokédex wird dabei nicht aktualisiert: Übertragung muss die Dex-Flags (gesehen/gefangen, Sprache) setzen; Arten außerhalb des Kalos-Dex erscheinen in XY ohnehin erst im Nationaldex.
- **Pokédex Gen 6** (relativ zum Dex-Block 0x15000): gefangen +0x8, gesehen +0x68 (4 × 0x60: m, w, m schillernd, w schillernd), angezeigt +0x1E8 (gleiche Abschnitte), Sprachbits 7 je Art ab +0x3C8 (XY) bzw. +0x400 (ORAS), Deutsch = Index 4. XY „fremd“-Bits +0x64C (Arten ≤ 649 aus Spielen vor XY, nach PKHeX, ungetestet). ORAS hat ab +0x686 je Art einen u16-Zähler (DexNav), wird nicht gesetzt. Formen werden noch nicht eingetragen. `pw3d-reise pokedex` auf die Originale ändert kein Byte, d. h. die Logik deckt sich mit dem Spiel.
- **Trainer-ID vereinheitlicht**: Gemeinsamer Trainer ist der aus Omega Rubin („Fabi“ 65014/22666). X übernimmt ihn per `pw3d-reise trainer <OR> <X>`, eigene Pokémon von X wurden auf diese ID umgeschrieben. Pension und andere Ablagen werden dabei noch nicht berücksichtigt. Im Spiel bestätigt: X zeigt ID 65014. Flemmli im Pokédex erst mit Nationaldex sichtbar (erwartet). Sicherungen in `saves/backup/`.
- Installiert in Azahar: Omega Rubin, Alpha Saphir, X, Sonne (`00164800` bestätigt), Ultramond. Spielstände bisher: OR, X, UM.
- Host-Compiler: WinLibs (MinGW g++ 16.2) per winget installiert, liegt unter `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_*\mingw64\bin` (in neuen Shells im PATH). Bauen: in `pc/` `mingw32-make` → `build/pw3d-reise.exe`.
- `pw3d-reise selbsttest` läuft durch: CRC-Testvektoren und PK6-Rundlauf (verschlüsseln/entschlüsseln) stimmen. Ob die Blocktabelle (Vertauschungstabelle der Blöcke A–D eingeschlossen) zu echten Daten passt, zeigt erst ein echter Spielstand.

## Nächste Schritte
1. Echte Spielstände von Omega Rubin und X aus Azahar holen (nach `saves/`, ignoriert), `info`/`scan` laufen lassen, Offsets bestätigen bzw. korrigieren.
2. Geld, Bag und Spielzeit bestätigen; Übertragung ORAS ↔ XY inkl. Trainer-ID; Rundlauf testen (übertragen, im Spiel laden, speichern, zurück).
3. Gen 7 (USUM): Blocktabelle, Prüfsummen, Signatur (siehe PKHeX `MemeCrypto`), PK7, Umwandlung, Platzhalter, gemeinsamer Speicher.
4. Reise-App (libctru: Spielstand-Zugriff wie Checkpoint, Titelstart per APT).

## Spielkennungen (Title-IDs, EU)
- Unterstützt werden sollen alle acht 3DS-Hauptspiele: X, Y, Omega Rubin, Alpha Saphir (Gen 6, zwei Formate: XY und ORAS) sowie Sonne, Mond, Ultrasonne, Ultramond (Gen 7, zwei Formate: SM und USUM). Die Reise-App merkt sich je Region, welche Edition gespielt wird.
- Omega Rubin `000400000011C400`, Alpha Saphir `000400000011C500`, X `0004000000055D00`, Y `0004000000055E00`, Sonne `0004000000164800`, Mond `0004000000175E00`, Ultrasonne `00040000001B5000`, Ultramond `00040000001B5100` (Sonne/Mond noch prüfen).
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
