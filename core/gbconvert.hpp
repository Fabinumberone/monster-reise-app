// Umwandlung zwischen Gen 1/2 (Virtual Console) und der Gen-7-Form im gemeinsamen Speicher.
//
// - gbToPk7: neues Pokémon aus GB, angelehnt an Poké Transporter (PKHeX PK1/PK2.ConvertToPK7):
//   Herkunft VC-Version, Fundort Kanto/Johto, Pokéball, Fähigkeit versteckt (Ausnahmen wie Transporter),
//   Wesen = EP % 25. Abweichend von Transporter bleiben EP und Spitzname erhalten, IVs = DV·2+1,
//   EVs aus Stat-EXP; EC/PID werden aus dem GB-Schlüssel abgeleitet (wiederholbar).
// - pk7ToGb: Datensatz für das GB-Spiel. Gibt es einen Stand vom letzten GB-Besuch (snap), werden
//   dessen DVs, Stat-EXP und GB-Eigenheiten verwendet, sonst aus IVs/EVs berechnet.
// - mergeGb: nach einem GB-Besuch nur übernehmen, was sich gegenüber dem dort Geschriebenen geändert
//   hat (Art/Entwicklung, EP, Attacken, Spitzname, Item, Freundschaft, Stat-EXP).
#pragma once
#include <cstdint>

#include "gb.hpp"
#include "names.hpp"
#include "pk6.hpp"
#include "savefile.hpp"
#include "species.hpp"

namespace pw3d {

struct GBContext {
    const SpeciesData* species = nullptr;
    const Names* names = nullptr;
    Trainer6 trainer;      // Trainer des gemeinsamen Speichers
    int gen = 1;           // 1 oder 2
    uint8_t version = 37;  // Herkunft: 35 Rot, 36 Grün, 37 Blau, 38 Gelb, 39 Gold, 40 Silber, 41 Kristall
    uint8_t language = 5;  // 2 Englisch, 5 Deutsch
    uint16_t gameTid = 0;  // ursprünglicher Trainer des GB-Spielstands (dessen Pokémon gelten als eigene)
    std::string gameOt;
};

// Schlüssel zur Wiedererkennung (TID, DVs, OT-Name).
uint32_t gbKey(const GBMon& m);

// Kann das Pokémon (Gen-7-Form) in diesem GB-Spiel erscheinen?
bool fitsGB(const PK6& pk7, int gen);

PK6 gbToPk7(const GBMon& m, const GBContext& ctx);
GBMon pk7ToGb(const PK6& pk7, const GBMon* snap, const GBContext& ctx);
void mergeGb(PK6& pk7, const GBMon& now, const GBMon& before, const GBContext& ctx);

// Teamwerte (Level, KP, Werte) der Gen-7-Form neu berechnen, KP voll.
void refreshParty7(PK6& pk7, const SpeciesData& sp);

} // namespace pw3d
