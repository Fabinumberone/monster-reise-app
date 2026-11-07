// Umwandlung von Pokémon-Datensätzen zwischen Gen 6 und Gen 7.
// Beide haben dieselbe Größe und Verschlüsselung; nur einzelne Felder sind umgelegt.
#pragma once
#include "pk6.hpp"

namespace pw3d {

// Gen 6 -> Gen 7 wie die Pokémon-Bank (nach PKHeX PK6.ConvertToPK7):
// Markierungen auf 2 Bit je Symbol, Formargument umgelegt, alte Gen-6-Felder geleert.
// Nicht übernommen: Fähigkeitenabgleich (bräuchte die Personal-Tabellen), AP auffüllen,
// Erinnerungen des Handelspartners.
PK6 toGen7(const PK6& pk6);

} // namespace pw3d
