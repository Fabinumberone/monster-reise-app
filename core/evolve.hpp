// Tauschentwicklungen in der App (Wunsch des Nutzers statt „Tausch mit sich selbst“).
// Daten: Abschnitt „TRDE“ in arten.bin (aus der Entwicklungstabelle von Ultrasonne/Ultramond, a/0/1/4).
// Methoden wie im Spiel: 5 Tausch, 6 Tausch mit getragenem Item (wird verbraucht),
// 7 Tausch gegen eine bestimmte Art (Laukaps <-> Schnuthelm, nur gemeinsam).
#pragma once
#include <vector>

#include "names.hpp"
#include "pk6.hpp"
#include "species.hpp"

namespace pw3d {

constexpr uint16_t ITEM_EVERSTONE = 229;

// Mögliche Tauschentwicklungen dieses Pokémon (leer: keine, Ei oder trägt einen Ewigstein).
// Methode 6 nur, wenn das passende Item getragen wird.
std::vector<TradeEvo> tradeOptions(const PK6& pk, const SpeciesData& sp);

// Entwickelt das Pokémon (Gen-7-Datensatz): Art, Fähigkeit nach Platz, Name (wenn kein Spitzname,
// Deutsch/Englisch), Item verbraucht bei Methode 6, Prüfsumme. Teamwerte berechnet der Aufrufer neu.
void applyTradeEvolution(PK6& pk, const TradeEvo& evo, const SpeciesData& sp, const Names& names);

} // namespace pw3d
