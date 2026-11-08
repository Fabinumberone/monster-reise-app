#include "evolve.hpp"

#include "bytes.hpp"

namespace pw3d {

std::vector<TradeEvo> tradeOptions(const PK6& pk, const SpeciesData& sp) {
    std::vector<TradeEvo> out;
    if (pk.isEgg() || pk.heldItem() == ITEM_EVERSTONE) return out;
    for (const TradeEvo& t : sp.tradeEvos()) {
        if (t.species != pk.species()) continue;
        if (t.method == 6 && pk.heldItem() != t.param) continue;
        out.push_back(t);
    }
    return out;
}

void applyTradeEvolution(PK6& pk, const TradeEvo& evo, const SpeciesData& sp, const Names& names) {
    uint8_t* d = pk.d.data();
    wr16(d + 0x08, evo.target); // Art (Form bleibt, z. B. Alola-Georok -> Alola-Geowaz)
    if (evo.method == 6) wr16(d + 0x0A, 0); // Item verbraucht
    // Fähigkeit nach Platz (0x15: 1, 2 oder 4 = versteckt)
    const SpeciesInfo& si = sp.get(evo.target);
    uint8_t num = d[0x15];
    int slot = num == 4 ? 2 : num == 2 ? 1 : 0;
    if (si.ability[size_t(slot)]) d[0x14] = si.ability[size_t(slot)];
    // Name: ohne Spitznamen der Artname in der Sprache des Pokémon (nur Deutsch/Englisch bekannt)
    if (!pk.isNicknamed()) {
        uint8_t lang = pk.language();
        std::string name = lang == 5 ? names.species(evo.target) : lang == 2 ? names.speciesEn(evo.target) : std::string();
        if (!name.empty() && name[0] != '#') {
            for (int i = 0; i < 0x1A; i++) d[0x40 + i] = 0;
            utf8ToUtf16(name, d + 0x40, 13);
        }
    }
    pk.updateChecksum();
}

} // namespace pw3d
