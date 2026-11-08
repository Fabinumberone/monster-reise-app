// Umwandlung von Pokémon-Datensätzen zwischen Gen 6 und Gen 7 und Prüfung, ob ein Pokémon
// in einem bestimmten Spiel existieren kann. Beide Generationen haben dieselbe Größe und
// Verschlüsselung; nur einzelne Felder sind umgelegt.
#pragma once
#include "pk6.hpp"

namespace pw3d {

enum class GameKind { XY, ORAS, SM, USUM };

const char* kindName(GameKind k);
int kindGeneration(GameKind k);

// Gen 6 -> Gen 7 wie die Pokémon-Bank (nach PKHeX PK6.ConvertToPK7):
// Markierungen auf 2 Bit je Symbol, Formargument umgelegt, alte Gen-6-Felder geleert.
// Nicht übernommen: Fähigkeitenabgleich (bräuchte die Personal-Tabellen), AP auffüllen,
// Erinnerungen des Handelspartners.
PK6 toGen7(const PK6& pk6);

// Gen 7 -> Gen 6 (Umkehrung von toGen7). Gen-7-Felder (Hypertraining, Resort, Schmutz)
// werden geleert, Markierungen auf 1 Bit reduziert. Setzt voraus, dass fitsIn() stimmt.
PK6 toGen6(const PK6& pk7);

// Kann dieses Pokémon (Gen-7-Form) im Spiel k existieren? Art, Form, Attacken, Item,
// Fähigkeit, Ball und Herkunftsspiel werden gegen die Grenzen des Spiels geprüft.
// Grund, warum das Pokémon in diesem Spiel nicht angezeigt wird (nullptr = sichtbar).
const char* whyNotIn(const PK6& pk7, GameKind k);
bool fitsIn(const PK6& pk7, GameKind k);

// Bytebereiche, die nur in einer Generation Bedeutung haben. Beim Hin- und Herreisen werden
// sie aus dem letzten Stand der jeweiligen Generation wiederhergestellt.
void restoreGen6Only(PK6& pk6, const PK6& lastGen6);
void restoreGen7Only(PK6& pk7, const PK6& lastGen7, uint8_t gen6Markings);

} // namespace pw3d
