// Abgleich beim Start: alle gefundenen Spielstände mit dem gemeinsamen Speicher zusammenführen.
//
// - Aktueller Spielstand (in dem man sich laut Speicher befindet): Stand übernehmen wie bei einer
//   Abreise, man bleibt aber dort (Bank::depart mit stay).
// - Alle anderen: neue Pokémon und bei neuen Spielständen einmalig Items und Geld (Bank::integrate).
// - Neuer, leerer Speicher: Trainer = Trainer des Spielstands mit den meisten Pokémon (Gen 6/7 zuerst),
//   kein aktuelles Spiel. Bei gleichen Pokémon in mehreren Spielständen gewinnt der mit mehr EP.
// In die Spielstände wird nichts geschrieben.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "bank.hpp"
#include "names.hpp"
#include "species.hpp"

namespace pw3d {

struct SyncInput {
    std::string label;          // „Omega Rubin (Modul)“
    uint32_t lowId = 0;
    uint8_t media = 1;          // 1 SD/installiert, 2 Modul
    int gen = 6;                // 1, 2, 3, 6, 7
    uint8_t gbVersion = 37;     // Gen 1/2/3: Herkunft (Gen 3: für Pokémon aus späteren Spielen)
    uint8_t language = 5;       // Gen 1/2: Sprache
    bool current = false;       // hier befindet man sich gerade
    bool known = false;         // als bekannt eintragen (Items/Geld schon im Speicher; für ältere Speicher)
    std::vector<uint8_t> data;  // Spielstand
};

struct SyncReport {
    std::vector<std::string> lines; // kurze Meldungen je Spielstand (nur wenn etwas passiert ist)
    int added = 0, updated = 0, noRoom = 0;
    bool createdTrainer = false;
    bool currentSynced = false; // aktueller Spielstand übernommen (dann reicht beim Reisen Bank::leave)
};

void syncAll(Bank& bank, const std::vector<SyncInput>& saves, const SpeciesData& species, const Names& names,
             SyncReport& report);

} // namespace pw3d
