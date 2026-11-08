// Spielstände von DS-Modulen (Gen 4/5) direkt lesen und schreiben, über den SPI-Bus des Modulschachts
// (Dienst pxi:dev). Der SPI-Teil folgt Checkpoint (spi.cpp, GPLv3, Bernardo Giordano/FlagBrew),
// das wiederum auf TWLSaveTool (TuxSH) und savegame-manager (Pokedoc) beruht.
//
// Unterstützt werden nur die Pokémon-Spiele mit 512-KB-Flash (DP, Pt, HGSS, BW, B2W2; HGSS/BW/B2W2
// mit Infrarot). Andere Module werden nicht angefasst.
#pragma once
#include <3ds.h>

#include <string>
#include <vector>

namespace ndscard {

struct Info {
    bool present = false;  // DS-Pokémon-Modul mit passendem Speicherchip
    char gameCode[5] = {}; // z. B. „IPKD“
    std::string error;     // warum nicht (für das Protokoll)
};

// Prüft den Modulschacht (einmal beim Start).
Info probe();

// Ganzen Spielstand (512 KB) lesen.
bool read(std::vector<u8>& out, std::string& err);

// Schreibt nur die Seiten, die sich vom Modul unterscheiden, und liest danach zur Kontrolle zurück.
bool write(const std::vector<u8>& data, std::string& err);

// DS-Modul starten (Neustart in den DS-Modus). false, wenn das System es ablehnt.
bool launch();

} // namespace ndscard
