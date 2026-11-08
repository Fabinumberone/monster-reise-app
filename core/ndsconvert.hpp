// Umwandlung zwischen Gen 4/5 (PK4/PK5) und der Gen-7-Form im gemeinsamen Speicher.
//
// - gen45ToPk7: wie Poké Transfer/Poké Transporter (PKHeX PK4→PK5→PK6): EC = PID, Wesen (Gen 4 aus
//   der PID), Fähigkeitsplatz (Gen 4 PID-Bit 0, Gen 5 PID-Bit 16, versteckt = Gen-5-Flag), Formen,
//   IVs/EVs/Wettbewerb/Pokérus, Fundort Poké Transfer. Eigene Pokémon (Trainer des Spielstands)
//   bekommen den Speicher-Trainer.
// - pk7ToGen45: Datensatz für ein Gen-4/5-Spiel; mit Stand vom letzten Besuch (snap, gleiche Gen)
//   bleiben dessen PID und Eigenheiten (Bänder, Fundorte, Begegnungsart …) erhalten. Pokémon mit
//   EC = PID behalten ihre PID; sonst wird eine feste PID gesucht, die Wesen (Gen 4), Geschlecht,
//   Fähigkeitsplatz und Schillern beibehält.
// - mergeGen45: nur übernehmen, was sich im Spiel gegenüber dem dort Geschriebenen geändert hat.
#pragma once
#include <cstdint>
#include <string>

#include "names.hpp"
#include "nds.hpp"
#include "pk6.hpp"
#include "savefile.hpp"
#include "species.hpp"

namespace pw3d {

struct NDSContext {
    const SpeciesData* species = nullptr;
    const Names* names = nullptr;
    Trainer6 trainer;           // Trainer des gemeinsamen Speichers
    NDSGame game = NDSGame::Pt; // Zielspiel (Grenzen, Bälle, Fundorte)
    uint8_t version = 12;       // Herkunft für Pokémon aus späteren Spielen
    uint16_t gameTid = 0, gameSid = 0;
    std::string gameOt;
    int gameGender = -1;        // Geschlecht der Spielfigur (eigene Pokémon bekommen es als OT-Geschlecht)
};

// Trainername, wie ihn ein Gen-4/5-Spiel speichern kann (höchstens 7 Zeichen).
std::string ndsTrainerName(const std::string& utf8);

// Herkunftsspiel für Pokémon aus späteren Spielen: das häufigste der eigenen Pokémon, sonst nach Spiel.
uint8_t ndsOriginVersion(const SaveNDS& s);

const char* whyNotGen45(const PK6& pk7, NDSGame g); // nullptr = sichtbar
bool fitsGen45(const PK6& pk7, NDSGame g);
PK6 gen45ToPk7(const PK45& pk, const NDSContext& ctx);
PK45 pk7ToGen45(const PK6& pk7, const PK45* snap, const NDSContext& ctx);
void mergeGen45(PK6& pk7, const PK45& now, const PK45& before, const NDSContext& ctx);

} // namespace pw3d
