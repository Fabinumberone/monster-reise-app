#include "items.hpp"

#include <algorithm>

#include "bytes.hpp"
#include "savefile.hpp"

namespace pw3d {

namespace {

struct PouchLayout {
    Pouch pouch;
    uint32_t offset; // absolut im Spielstand
    int slots;
    uint16_t maxCount;
};

// Nur die reisenden Taschen (nach PKHeX PlayerBag6XY/6AO/7SM/7USUM).
std::vector<PouchLayout> layoutOf(GameKind k) {
    switch (k) {
    case GameKind::XY:
        return {{Pouch::Items, 0x400 + 0x000, 400, 999}, {Pouch::Medicine, 0x400 + 0x968, 64, 999},
                {Pouch::Berries, 0x400 + 0xA68, 72, 999}};
    case GameKind::ORAS:
        return {{Pouch::Items, 0x400 + 0x000, 400, 999}, {Pouch::Medicine, 0x400 + 0x970, 64, 999},
                {Pouch::Berries, 0x400 + 0xA70, 72, 999}};
    case GameKind::SM:
        return {{Pouch::Items, 0x000, 430, 999}, {Pouch::Medicine, 0xB48, 64, 999}, {Pouch::Berries, 0xC48, 72, 999},
                {Pouch::ZCrystals, 0xD68, 30, 1}};
    case GameKind::USUM:
        return {{Pouch::Items, 0x000, 427, 999}, {Pouch::Medicine, 0xB74, 60, 999}, {Pouch::Berries, 0xC64, 67, 999},
                {Pouch::ZCrystals, 0xD70, 35, 1}, {Pouch::Battle, 0xDFC, 11, 999}};
    }
    return {};
}

struct Slot {
    uint16_t item, count;
    uint16_t freeSpace; // nur Gen 7
    bool isNew;         // nur Gen 7
};

Slot readSlot(const std::vector<uint8_t>& d, uint32_t at, int gen) {
    if (gen == 6) return {rd16(&d[at]), rd16(&d[at + 2]), 0, false};
    uint32_t v = rd32(&d[at]);
    return {uint16_t(v & 0x3FF), uint16_t((v >> 10) & 0x3FF), uint16_t((v >> 20) & 0x3FF), bool(v & 0x40000000)};
}

void writeSlot(std::vector<uint8_t>& d, uint32_t at, int gen, const Slot& s) {
    if (gen == 6) {
        wr16(&d[at], s.item);
        wr16(&d[at + 2], s.count);
        return;
    }
    uint32_t v = (s.item & 0x3FFu) | (uint32_t(s.count & 0x3FF) << 10) | (uint32_t(s.freeSpace & 0x3FF) << 20) |
                 (s.isNew ? 0x40000000u : 0);
    wr32(&d[at], v);
}

} // namespace

const char* pouchName(Pouch p) {
    switch (p) {
    case Pouch::Items: return "Items";
    case Pouch::Medicine: return "Medizin";
    case Pouch::Berries: return "Beeren";
    case Pouch::ZCrystals: return "Z-Kristalle";
    case Pouch::Battle: return "Roto";
    default: return "-";
    }
}

Pouch pouchOf(GameKind k, uint16_t item) {
    for (int i = 0; i < ITEM_TABLE_COUNT; i++) {
        const ItemList& l = ITEM_TABLES[i];
        if (l.game == k && std::binary_search(l.ids, l.ids + l.count, item)) return l.pouch;
    }
    return Pouch::None;
}

std::vector<ItemStack> readTravelItems(const SaveFile& sav) {
    std::vector<ItemStack> out;
    const auto& d = sav.raw();
    int gen = sav.generation();
    for (const auto& p : layoutOf(sav.kind()))
        for (int i = 0; i < p.slots; i++) {
            Slot s = readSlot(d, p.offset + uint32_t(i) * 4, gen);
            if (s.item && s.count) out.push_back({s.item, s.count});
        }
    return out;
}

void writeTravelItems(SaveFile& sav, const std::vector<ItemStack>& items, std::vector<uint16_t>& written) {
    auto& d = sav.raw();
    int gen = sav.generation();
    GameKind k = sav.kind();
    written.assign(items.size(), 0);
    for (const auto& p : layoutOf(k)) {
        // Bisherige Belegung merken (Gen 7: Sortierindex und NEU-Markierung erhalten).
        std::vector<Slot> before;
        for (int i = 0; i < p.slots; i++) before.push_back(readSlot(d, p.offset + uint32_t(i) * 4, gen));
        int n = 0;
        for (size_t j = 0; j < items.size(); j++) {
            const ItemStack& it = items[j];
            if (it.count == 0 || pouchOf(k, it.item) != p.pouch || n >= p.slots) continue;
            Slot s{it.item, uint16_t(std::min<int>(it.count, p.maxCount)), 0, true};
            for (const Slot& b : before)
                if (b.item == it.item && b.count) { s.freeSpace = b.freeSpace; s.isNew = b.isNew; break; }
            writeSlot(d, p.offset + uint32_t(n++) * 4, gen, s);
            written[j] = s.count; // mehr als erlaubt: Rest bleibt im Speicher
        }
        for (int i = n; i < p.slots; i++) writeSlot(d, p.offset + uint32_t(i) * 4, gen, Slot{0, 0, 0, false});
    }
}

} // namespace pw3d
