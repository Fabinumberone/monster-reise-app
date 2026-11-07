// Die unterstützten Spiele und der Zugriff auf ihre Spielstände.
//
// 3DS-Hauptspiele (Gen 6/7): Spielstand „main“. Virtual-Console-Spiele (Gen 1/2): „sav.dat“.
//
// Zugriff auf den Spielstand:
//  1. Azahar: Die Emulator-SD enthält die Spielstände als normale Dateien unter
//     sdmc:/Nintendo 3DS/<0…0>/<0…0>/title/00040000/<id>/data/00000001/<datei>.
//  2. Echter 3DS: Spielstand-Archiv des Systems (ARCHIVE_USER_SAVEDATA, wie Checkpoint), getrennt
//     für die installierte Fassung (SD) und das Modul. Nach dem Schreiben wird festgeschrieben (Commit).
#pragma once
#include <3ds.h>

#include <string>
#include <vector>

#include "convert.hpp"

struct Game {
    const char* name;     // „Omega Rubin“
    const char* region;   // „Hoenn“
    u32 lowId;            // untere 32 Bit der Title-ID
    int gen;              // 1, 2, 6 oder 7
    pw3d::GameKind kind;  // nur Gen 6/7
    const char* saveFile; // "main" oder "sav.dat"
    u8 language = 5;      // Gen 1/2: 2 Englisch, 5 Deutsch
    u8 gbVersion = 0;     // Gen 1/2: Herkunft für Gen 7 (37 Blau, 38 Gelb, 40 Silber, 41 Kristall …);
                          // Gen 3: Herkunft für Pokémon aus späteren Spielen (1 Saphir … 5 Blattgrün)
    const char* kindName = nullptr; // Gen 3–5: Kennung im Speicher („GBA-RS“, „NDS-Pt“ …)
    u8 sub = 0;                     // Gen 3: GBAGame, Gen 4/5: NDSGame
};

// Spielstand-Dateien auf der SD (Gen 3–5): saveFile = voller Pfad. Werden nicht von der App
// gestartet (mGBA bzw. TWiLight Menu++ selbst öffnen).
inline bool isFileGame(const Game& g) { return g.gen >= 3 && g.gen <= 5; }

// Spielstände von mGBA (sdmc:/mGBA/*.sav, Gen 3) und TWiLight Menu++ (sdmc:/roms/nds favoriten/saves,
// sdmc:/roms/nds/saves, Gen 4/5). lowId ist ein Hash des Pfads (Bit 31 gesetzt).
const std::vector<Game>& fileGames();
// Dateiname des Spielstands (für Sicherungen).
std::string saveName(const Game& g);

const std::vector<Game>& allGames();
const Game* gameByLowId(u32 lowId);
// Kennung für den gemeinsamen Speicher („ORAS“, „XY“, „SM“, „USUM“, „RBY“, „GSC“)
const char* bankKind(const Game& g);

// Ein Spielstand: Spiel + Medium (installiert oder Modul). Modul und installierte Fassung haben
// getrennte Spielstände.
struct SaveSlot {
    const Game* game = nullptr;
    FS_MediaType media = MEDIATYPE_SD;
    bool cart() const { return media == MEDIATYPE_GAME_CARD; }
    std::string label() const { return std::string(game->name) + (cart() ? " (Modul)" : ""); }
};

// Alle Spielstände, die sich lesen lassen (installiert/eingelegt und mindestens einmal gespeichert).
std::vector<SaveSlot> findSaves();

bool readSave(const SaveSlot& s, std::vector<u8>& out, std::string& err);
bool writeSave(const SaveSlot& s, const std::vector<u8>& data, std::string& err);

// Startet das Spiel: mit Home-Menü beim Beenden der App (Chainloader), ohne Home-Menü
// (Azahar) sofort per Anwendungssprung. false, wenn der Sprung abgelehnt wurde.
// Gen 3 startet mGBA, Gen 4/5 den Forwarder des Spiels (falls installiert), sonst TWiLight Menu++.
bool launchGame(const SaveSlot& s);
// Programm, in dem man Gen-3–5-Spiele selbst auswählt („mGBA“, „TWiLight Menu++“), sonst nullptr.
const char* launcherName(const SaveSlot& s);
