// Umwandlung zwischen Gen 3 (PK3) und der Gen-7-Form im gemeinsamen Speicher.
//
// - gen3ToPk7: wie Pal Park + Poké Transfer + Poké Transporter (PKHeX PK3→PK4→PK5→PK6): EC = PID,
//   Wesen = PID % 25, Geschlecht aus PID, Fähigkeit nach Fähigkeitsplatz (Ultramond-Artdaten),
//   Fundort „Poké Transfer“, IVs/EVs/Wettbewerbswerte/Pokérus übernommen, Spitzname nur, wenn er vom
//   Artnamen abweicht. Eigene Pokémon (Trainer des Spielstands) bekommen den Speicher-Trainer.
// - pk7ToGen3: Datensatz für ein Gen-3-Spiel. Gibt es einen Stand vom letzten Gen-3-Besuch (snap),
//   bleiben dessen PID und Gen-3-Eigenheiten (Fundort, Bänder, Herkunft) erhalten. Sonst wird eine
//   feste PID gesucht, die Wesen, Geschlecht, Schillern und Icognito-Form beibehält.
// - mergeGen3: nach einem Gen-3-Besuch nur übernehmen, was sich gegenüber dem dort Geschriebenen
//   geändert hat.
#pragma once
#include <cstdint>
#include <string>

#include "gba.hpp"
#include "names.hpp"
#include "pk6.hpp"
#include "savefile.hpp"
#include "species.hpp"

namespace pw3d {

struct GBAContext {
    const SpeciesData* species = nullptr;
    const Names* names = nullptr;
    Trainer6 trainer;       // Trainer des gemeinsamen Speichers
    uint8_t version = 2;    // Herkunft für Pokémon aus späteren Spielen (1 Saphir, 2 Rubin, 3 Smaragd, 4 FR, 5 BG)
    uint16_t gameTid = 0, gameSid = 0; // Trainer des Spielstands (dessen Pokémon gelten als eigene)
    std::string gameOt;
    int gameGender = -1;    // Geschlecht der Spielfigur (eigene Pokémon bekommen es als OT-Geschlecht)
};

// Trainername, wie ihn ein Gen-3-Spiel speichern kann (höchstens 7 Zeichen).
std::string gen3TrainerName(const std::string& utf8);
// Name des gemeinsamen Trainers in Spielen mit höchstens 7 Zeichen (Gen 1–5). Ohne Kurzname die
// ersten 7 Zeichen, mit Kurzname (setTrainerShortName) dieser, wenn utf8 der volle Name ist.
std::string shortTrainerName(const std::string& utf8);
void setTrainerShortName(const std::string& fullName, const std::string& shortName);
bool trainerShortNameSet();

uint16_t gen3ItemToModern(uint16_t id);
uint16_t modernItemToGen3(uint16_t id); // 0 = keine Entsprechung

// Kann das Pokémon (Gen-7-Form) in einem Gen-3-Spiel erscheinen?
const char* whyNotGen3(const PK6& pk7); // nullptr = sichtbar
bool fitsGen3(const PK6& pk7);

// Herkunftsspiel für Pokémon aus späteren Spielen: das häufigste der eigenen Pokémon im Spielstand,
// sonst nach Spiel (Rubin, Smaragd, Feuerrot).
uint8_t gen3OriginVersion(const SaveGBA& s);

// Herkunftsspiel aus dem Dateinamen (z. B. „Pokemon Feuerrote Edition.sav“, „Weiss 2“, „Heart Gold“);
// 0, wenn nichts passt. Gilt für Gen 3–5.
uint8_t versionFromFileName(const std::string& fileName);

PK6 gen3ToPk7(const PK3& pk, const GBAContext& ctx);
PK3 pk7ToGen3(const PK6& pk7, const PK3* snap, const GBAContext& ctx);
void mergeGen3(PK6& pk7, const PK3& now, const PK3& before, const GBAContext& ctx);

} // namespace pw3d
