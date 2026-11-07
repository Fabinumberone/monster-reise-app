// Die acht 3DS-Hauptspiele und der Zugriff auf ihre Spielstände.
//
// Zugriff auf den Spielstand „main“:
//  1. Azahar: Die Emulator-SD enthält die Spielstände als normale Dateien unter
//     sdmc:/Nintendo 3DS/<0…0>/<0…0>/title/00040000/<id>/data/00000001/main.
//  2. Echter 3DS: Spielstand-Archiv des Systems (ARCHIVE_USER_SAVEDATA, wie Checkpoint),
//     zuerst SD-Installation, dann Modul. Nach dem Schreiben wird festgeschrieben (Commit).
#pragma once
#include <3ds.h>

#include <string>
#include <vector>

#include "convert.hpp"

struct Game {
    const char* name;   // „Omega Rubin“
    const char* region; // „Hoenn“
    u32 lowId;          // untere 32 Bit der Title-ID
    pw3d::GameKind kind;
};

const std::vector<Game>& allGames();
const Game* gameByLowId(u32 lowId);

// Hat dieses Spiel einen lesbaren Spielstand? (installiert und mindestens einmal gespeichert)
bool hasSave(const Game& g);

bool readSave(const Game& g, std::vector<u8>& out, std::string& err);
bool writeSave(const Game& g, const std::vector<u8>& data, std::string& err);

// Startet das Spiel, sobald die App beendet wird.
void launchOnExit(const Game& g);
