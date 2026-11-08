# Monster Reise App

Ein Spielstand für alle Regionen: Die Monster Reise App hält Pokémon, Items und Geld aller eigenen
Spielstände in einem gemeinsamen Speicher auf der SD-Karte und schreibt sie in jedes Spiel zurück.
Man fängt in Feuerrot, trainiert in Platin und kämpft in Ultramond mit demselben Team. Gespielt
werden immer die Originalspiele selbst.

> Privates Hobbyprojekt, nicht mit Nintendo, Game Freak, Creatures oder The Pokémon Company
> verbunden. Es enthält keine Daten aus den Spielen. Es funktioniert nur mit eigenen Spielen und
> eigenen Spielständen.

## Unterstützte Spiele

| Generation | Spiele | Spielstand |
|---|---|---|
| 1, 2 | Rot/Blau/Gelb, Gold/Silber/Kristall (Virtual Console) | installiert |
| 3 | Rubin/Saphir/Smaragd, Feuerrot/Blattgrün | `.sav` von mGBA (`sd:/mGBA/`) |
| 4, 5 | Diamant/Perl/Platin, HeartGold/SoulSilver, Schwarz/Weiß (2) | `.sav` von TWiLight Menu++ |
| 6 | X/Y, Omega Rubin/Alpha Saphir | installiert oder Modul |
| 7 | Sonne/Mond, Ultrasonne/Ultramond | installiert oder Modul |

Getestet mit den deutschen Fassungen auf einem New 2DS XL (Luma3DS) und in Azahar.

## ⚠️ Bitte zuerst lesen

- **Testversion.** Die App schreibt in deine Spielstände. Vor jedem Schreiben legt sie eine
  Sicherung an (`sd:/pokemon-world-3d/sicherungen/`, höchstens 10 je Spielstand). Sichere trotzdem
  vorher alles selbst, z. B. mit Checkpoint oder JKSM.
- **Nicht online gehen** mit übertragenen Spielständen (GTS, Wundertausch, Online-Kämpfe usw.).
- Alle Pokémon werden auf einen gemeinsamen Trainer umgeschrieben (Name und ID). Schillernde
  Pokémon bleiben schillernd.

## Installation

1. Voraussetzung: 3DS mit Luma3DS und FBI.
2. Die `monster-reise-app.cia` aus dem neuesten
   [Release](../../releases/latest) installieren. Am einfachsten geht das in FBI mit
   „Remote Install → Scan QR Code“ und dem QR-Code des Download-Links. Updates werden genauso
   installiert; Speicher und Spielstände bleiben dabei erhalten.
3. **Daten erzeugen** (einmalig, siehe unten) und den Ordner `pokemon-world-3d` ins
   Hauptverzeichnis der SD-Karte kopieren.

## Daten erzeugen

Namen, Symbole und Artdaten stammen aus den Spielen und werden deshalb nicht mitgeliefert. Die Seite
`monster-reise-daten.html` (im Release) erzeugt sie aus deinen eigenen Spieldateien. Einfach
herunterladen und im Browser öffnen (Windows, Mac oder Linux). Es wird nichts installiert und nichts
hochgeladen, alles passiert lokal im Browser.

Benötigt werden:

- **Ultrasonne oder Ultramond** (Pflicht) als **entschlüsselte** CIA oder `.3ds`
- optional für Gen 1/2: eine Virtual-Console-Fassung von Rot/Blau/Gelb und von
  Gold/Silber/Kristall, ebenfalls als entschlüsselte CIA

Eigene Spiele sicherst du z. B. mit GodMode9: den Titel auswählen und „Build CIA (standard)“
wählen. Meldet die Seite, die Datei sei verschlüsselt, die CIA in GodMode9 zusätzlich mit
„Decrypt file“ entschlüsseln. Wer Azahar benutzt, kann stattdessen einfach dessen SD-Ordner
(`sdmc`) wählen, dort liegen installierte Spiele bereits entschlüsselt.

Die Dateien auf die Seite ziehen oder auswählen, die ZIP-Datei herunterladen, entpacken und den
Ordner `pokemon-world-3d` ins Hauptverzeichnis der SD-Karte kopieren. **Diese Dateien nicht
weitergeben oder hochladen.**

(Am PC geht dasselbe auch per Node.js: `node tools/daten.mjs <Spieldateien …>`.)

## Ablage auf der SD-Karte

```
sd:/pokemon-world-3d/
  speicher.bin      gemeinsamer Speicher (alle Pokémon, Items, Geld)
  arten.bin         \
  namen.txt          > vom Werkzeug erzeugt
  icons.bin         /
  sicherungen/      Sicherungen der Spielstände
  protokoll.txt     Ablauf des letzten Starts (hilft bei Fehlern)
sd:/mGBA/*.sav                       Gen 3
sd:/roms/nds favoriten/saves/*.sav   Gen 4/5 (auch sd:/roms/nds/saves)
```

## Bauen

- 3DS-App: devkitPro (devkitARM, libctru, citro2d), dazu makerom und bannertool für die CIA:
  in `app/` `make` bzw. `make cia`. Der Pfad darf keine Leerzeichen enthalten.
- PC-Werkzeug (Test und Fehlersuche): in `pc/` `make` mit MinGW g++.

## Lizenz und Dank

GPLv3, siehe [LICENSE](LICENSE). Aufbau der Spielstände, Prüfsummen, Item- und Zeichentabellen und
die Signatur von Sonne/Mond orientieren sich an [PKHeX](https://github.com/kwsch/PKHeX) (GPLv3).
Danke auch an die Entwickler von libctru, citro2d, Luma3DS, FBI, mGBA und TWiLight Menu++.
