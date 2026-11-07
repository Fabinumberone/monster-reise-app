// Taschen (Beutel) der Spiele und Geld.
//
// Reisende Taschen: normale Items, Medizin, Beeren, Z-Kristalle (Gen 7), Roto-Items (USUM).
// Basis-Items und TMs/VMs bleiben je Spiel (Wunsch des Nutzers).
//
// Taschenplatz Gen 6: u16 Item, u16 Anzahl. Gen 7: u32 mit 10 Bit Item, 10 Bit Anzahl,
// 10 Bit Sortierindex („freier Platz“), Bit 30 = NEU. Positionen nach PKHeX PlayerBag*.cs,
// an echten Spielständen bestätigt.
#pragma once
#include <cstdint>
#include <vector>

#include "convert.hpp" // GameKind

namespace pw3d {

class SaveFile;

enum class Pouch { None, Items, Medicine, Berries, ZCrystals, Battle };

const char* pouchName(Pouch p);

struct ItemList {
    GameKind game;
    Pouch pouch;
    const uint16_t* ids; // aufsteigend sortiert
    int count;
};
extern const ItemList ITEM_TABLES[];
extern const int ITEM_TABLE_COUNT;

// In welche reisende Tasche gehört das Item in diesem Spiel? Pouch::None = gibt es dort nicht.
Pouch pouchOf(GameKind k, uint16_t item);

struct ItemStack {
    uint16_t item = 0;
    uint16_t count = 0;
};

// Alle Items der reisenden Taschen (in Taschenreihenfolge).
std::vector<ItemStack> readTravelItems(const SaveFile& sav);

// Schreibt die reisenden Taschen neu. Items, die das Spiel nicht kennt oder für die kein Platz
// ist, werden nicht geschrieben; written[i] ist die tatsächlich geschriebene Anzahl von items[i]
// (0 = unsichtbar, kleiner als count = Rest bleibt im Speicher).
void writeTravelItems(SaveFile& sav, const std::vector<ItemStack>& items, std::vector<uint16_t>& written);

} // namespace pw3d
