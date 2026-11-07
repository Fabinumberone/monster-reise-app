#include "gb.hpp"

#include <cstring>

namespace pw3d {

namespace {

// Internationaler Zeichensatz (PKHeX StringConverter1.TableEN), 0x80–0xFF; davor nur Sonderfälle.
const char* const TABLE_HI[128] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",            // 80
    "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "(", ")", ":", ";", "[", "]",            // 90
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p",            // A0
    "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "à", "è", "é", "ù", "À", "Á",            // B0
    "Ä", "Ö", "Ü", "ä", "ö", "ü", "È", "É", "Ì", "Í", "Ñ", "Ò", "Ó", "Ù", "Ú", "á",            // C0
    "ì", "í", "ñ", "ò", "ó", "ú", "º", "", "", "", "", "", "", "", "←", "'",                   // D0
    "’", "PK", "MN", "-", "", "", "?", "!", ".", "&", "é", "→", "▷", "▶", "▼", "♂",            // E0
    "$", "×", ".", "/", ",", "♀", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",            // F0
};

// Gen 1: Pokédex -> interne Nummer und umgekehrt (PKHeX SpeciesConverter).
const uint8_t NAT_TO_INT[152] = {
    0x00, 0x99, 0x09, 0x9A, 0xB0, 0xB2, 0xB4, 0xB1, 0xB3, 0x1C, 0x7B, 0x7C, 0x7D, 0x70, 0x71, 0x72, 0x24, 0x96, 0x97,
    0xA5, 0xA6, 0x05, 0x23, 0x6C, 0x2D, 0x54, 0x55, 0x60, 0x61, 0x0F, 0xA8, 0x10, 0x03, 0xA7, 0x07, 0x04, 0x8E, 0x52,
    0x53, 0x64, 0x65, 0x6B, 0x82, 0xB9, 0xBA, 0xBB, 0x6D, 0x2E, 0x41, 0x77, 0x3B, 0x76, 0x4D, 0x90, 0x2F, 0x80, 0x39,
    0x75, 0x21, 0x14, 0x47, 0x6E, 0x6F, 0x94, 0x26, 0x95, 0x6A, 0x29, 0x7E, 0xBC, 0xBD, 0xBE, 0x18, 0x9B, 0xA9, 0x27,
    0x31, 0xA3, 0xA4, 0x25, 0x08, 0xAD, 0x36, 0x40, 0x46, 0x74, 0x3A, 0x78, 0x0D, 0x88, 0x17, 0x8B, 0x19, 0x93, 0x0E,
    0x22, 0x30, 0x81, 0x4E, 0x8A, 0x06, 0x8D, 0x0C, 0x0A, 0x11, 0x91, 0x2B, 0x2C, 0x0B, 0x37, 0x8F, 0x12, 0x01, 0x28,
    0x1E, 0x02, 0x5C, 0x5D, 0x9D, 0x9E, 0x1B, 0x98, 0x2A, 0x1A, 0x48, 0x35, 0x33, 0x1D, 0x3C, 0x85, 0x16, 0x13, 0x4C,
    0x66, 0x69, 0x68, 0x67, 0xAA, 0x62, 0x63, 0x5A, 0x5B, 0xAB, 0x84, 0x4A, 0x4B, 0x49, 0x58, 0x59, 0x42, 0x83, 0x15,
};

// Gen-2-Item -> heutige Nummer (PKHeX ItemConverter.Item2to4; 0xFFFF dort = 0 hier).
const uint16_t ITEM2_TO_MODERN[256] = {
    0,   1,   2,   213, 3,   4,   0,   450, 81,  18,  19,  20,  21,  22,  23,  24,  25,  26,  17,  78,  79,  41,
    82,  83,  84,  0,   45,  46,  47,  48,  256, 49,  50,  60,  85,  257, 92,  63,  27,  28,  29,  55,  76,  77,
    56,  0,   30,  31,  32,  57,  0,   58,  59,  61,  444, 0,   0,   216, 445, 446, 0,   447, 51,  38,  39,  40,
    478, 464, 456, 484, 0,   482, 33,  217, 151, 0,   237, 244, 149, 153, 152, 245, 221, 156, 150, 485, 86,  87,
    222, 486, 0,   223, 487, 488, 224, 243, 248, 490, 241, 491, 0,   489, 240, 473, 0,   259, 228, 246, 242, 157,
    88,  89,  229, 247, 504, 0,   0,   239, 258, 230, 0,   34,  35,  36,  37,  238, 231, 475, 481, 0,   0,   90,
    91,  476, 480, 0,   0,   0,   249, 43,  232, 0,   0,   233, 250, 0,   234, 0,   0,   0,   154, 235, 0,   0,
    0,   0,   44,  495, 0,   493, 0,   492, 0,   236, 497, 498, 496, 0,   0,   80,  0,   0,   252, 155, 158, 477,
    0,   500, 483, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   328, 329, 330, 331, 331, 332, 333,
    334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355,
    355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376,
    377, 420, 421, 422, 423, 424, 425, 426, 427, 0,   0,   0,   0,   0,
};

// Gen-1-Item -> heutige Nummer (nach den Item-Konstanten von pokered). Nur reisende Items;
// Basis-Items, Orden, Fossilien-Schlüssel, TMs/VMs = 0 (bleiben im Spiel).
uint16_t gen1Item(uint8_t i) {
    switch (i) {
    case 0x01: return 1;   // Meisterball
    case 0x02: return 2;   // Hyperball
    case 0x03: return 3;   // Superball
    case 0x04: return 4;   // Pokéball
    case 0x08: return 5;   // Safariball
    case 0x0A: return 81;  // Mondstein
    case 0x0B: return 18;  // Gegengift
    case 0x0C: return 19;  // Feuerheiler
    case 0x0D: return 20;  // Eisheiler
    case 0x0E: return 21;  // Aufwecker
    case 0x0F: return 22;  // Para-Heiler
    case 0x10: return 23;  // Top-Genesung
    case 0x11: return 24;  // Top-Trank
    case 0x12: return 25;  // Hypertrank
    case 0x13: return 26;  // Supertrank
    case 0x14: return 17;  // Trank
    case 0x1D: return 78;  // Fluchtseil
    case 0x1E: return 79;  // Schutz
    case 0x1F: return 103; // Altbernstein
    case 0x20: return 82;  // Feuerstein
    case 0x21: return 83;  // Donnerstein
    case 0x22: return 84;  // Wasserstein
    case 0x23: return 45;  // KP-Plus
    case 0x24: return 46;  // Protein
    case 0x25: return 47;  // Eisen
    case 0x26: return 48;  // Carbon
    case 0x27: return 49;  // Kalzium
    case 0x28: return 50;  // Sonderbonbon
    case 0x29: return 102; // Domfossil
    case 0x2A: return 101; // Helixfossil
    case 0x2E: return 60;  // X-Treffer
    case 0x2F: return 85;  // Blattstein
    case 0x31: return 92;  // Nugget
    case 0x33: return 63;  // Poképuppe
    case 0x34: return 27;  // Hyperheiler
    case 0x35: return 28;  // Beleber
    case 0x36: return 29;  // Top-Beleber
    case 0x37: return 55;  // Megablock
    case 0x38: return 76;  // Superschutz
    case 0x39: return 77;  // Top-Schutz
    case 0x3A: return 56;  // Angriffplus
    case 0x3C: return 30;  // Tafelwasser
    case 0x3D: return 31;  // Sprudel
    case 0x3E: return 32;  // Limonade
    case 0x41: return 57;  // X-Angriff
    case 0x42: return 58;  // X-Abwehr
    case 0x43: return 59;  // X-Tempo
    case 0x44: return 61;  // X-Spezial
    case 0x4F: return 51;  // AP-Plus
    case 0x50: return 38;  // Äther
    case 0x51: return 39;  // Top-Äther
    case 0x52: return 40;  // Elixier
    case 0x53: return 41;  // Top-Elixier
    default: return 0;
    }
}

uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t be24(const uint8_t* p) { return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]; }

struct Layout {
    uint32_t trainerName, tid, money, party, currentBoxIndex, currentBox;
    uint32_t bag, balls, keys, pc; // Gen 1: bag = Beutel, balls/keys = 0
    uint32_t sumStart, sumEnd, sum1, sum2;
    int storedSize, partySize;
};

const Layout L_RBY{0x2598, 0x2605, 0x25F3, 0x2F2C, 0x284C, 0x30C0, 0x25C9, 0, 0, 0x27E6, 0x2598, 0x3522, 0x3523, 0, 33, 44};
const Layout L_C{0x200B, 0x2009, 0x23DC, 0x2865, 0x2700, 0x2D10, 0x2420, 0x2465, 0x244A, 0x247F, 0x2009, 0x2B82, 0x2D0D, 0x1F0D, 32, 48};
const Layout L_GS{0x200B, 0x2009, 0x23DB, 0x288A, 0x2724, 0x2D6C, 0x241F, 0x2464, 0x2449, 0x247E, 0x2009, 0x2D68, 0x2D69, 0x7E6D, 32, 48};

const Layout& layoutOf(GBGame g) { return g == GBGame::RBY ? L_RBY : g == GBGame::Crystal ? L_C : L_GS; }

bool gen1SumOk(const std::vector<uint8_t>& d) {
    uint8_t s = 0;
    for (uint32_t i = 0x2598; i < 0x3523; i++) s = uint8_t(s + d[i]);
    return uint8_t(~s) == d[0x3523];
}

bool gen2SumOk(const std::vector<uint8_t>& d, const Layout& L) {
    uint16_t s = 0;
    for (uint32_t i = L.sumStart; i <= L.sumEnd; i++) s = uint16_t(s + d[i]);
    return d[L.sum1] == (s & 0xFF) && d[L.sum1 + 1] == (s >> 8);
}

} // namespace

std::string gbToUtf8(const uint8_t* p, size_t maxLen) {
    std::string s;
    for (size_t i = 0; i < maxLen; i++) {
        uint8_t c = p[i];
        if (c == 0x50 || c == 0x00) break;
        if (c == 0x7F) s += ' ';
        else if (c >= 0x80) s += TABLE_HI[c - 0x80];
        else if (c == 0x5D) s += "*"; // Tausch-OT
    }
    return s;
}

void utf8ToGb(const std::string& s, uint8_t* p, size_t maxLen) {
    std::memset(p, 0x50, maxLen);
    size_t n = 0;
    for (size_t i = 0; i < s.size() && n + 1 < maxLen;) {
        // längstes passendes Zeichen der Tabelle suchen
        int best = -1;
        size_t bestLen = 0;
        for (int k = 0; k < 128; k++) {
            size_t l = std::strlen(TABLE_HI[k]);
            if (l && l > bestLen && s.compare(i, l, TABLE_HI[k]) == 0) { best = k; bestLen = l; }
        }
        if (s[i] == ' ') { p[n++] = 0x7F; i++; continue; }
        if (best < 0) { i++; continue; }
        p[n++] = uint8_t(0x80 + best);
        i += bestLen;
    }
}

uint16_t gen1InternalToNational(uint8_t internal) {
    for (uint16_t s = 1; s <= 151; s++)
        if (NAT_TO_INT[s] == internal) return s;
    return 0;
}

uint8_t gen1NationalToInternal(uint16_t species) { return species <= 151 ? NAT_TO_INT[species] : 0; }

uint16_t gen1ItemToModern(uint8_t item) { return gen1Item(item); }
uint8_t modernItemToGen1(uint16_t item) {
    if (!item) return 0;
    for (int i = 1; i < 256; i++)
        if (gen1Item(uint8_t(i)) == item) return uint8_t(i);
    return 0;
}
uint16_t gen2ItemToModern(uint8_t item) { return ITEM2_TO_MODERN[item]; }
uint8_t modernItemToGen2(uint16_t item) {
    if (!item) return 0;
    for (int i = 1; i < 256; i++)
        if (ITEM2_TO_MODERN[i] == item) return uint8_t(i);
    return 0;
}

const char* gbGameName(GBGame g) {
    switch (g) {
    case GBGame::RBY: return "Rot/Blau/Gelb";
    case GBGame::GS: return "Gold/Silber";
    case GBGame::Crystal: return "Kristall";
    default: return "unbekannt";
    }
}

bool GBMon::shiny() const {
    return dv(1) == 10 && dv(2) == 10 && dv(3) == 10 && (dv(0) & 2);
}

bool SaveGB::load(std::vector<uint8_t> data, std::string& err) {
    data_ = std::move(data);
    if (data_.size() != 0x8000 && data_.size() != 0x8010) { err = "Kein Gen-1/2-Spielstand (Größe)"; return false; }
    if (gen1SumOk(data_)) game_ = GBGame::RBY;
    else if (gen2SumOk(data_, L_C)) game_ = GBGame::Crystal;
    else if (gen2SumOk(data_, L_GS)) game_ = GBGame::GS;
    else { err = "Prüfsumme passt zu keinem Gen-1/2-Spiel"; return false; }
    return true;
}

bool SaveGB::checksumValid() const {
    if (game_ == GBGame::RBY) return gen1SumOk(data_);
    const Layout& L = layoutOf(game_);
    return gen2SumOk(data_, L) && data_[L.sum2] == data_[L.sum1] && data_[L.sum2 + 1] == data_[L.sum1 + 1];
}

std::string SaveGB::trainerName() const { return gbToUtf8(&data_[layoutOf(game_).trainerName], 11); }
uint16_t SaveGB::tid() const { return be16(&data_[layoutOf(game_).tid]); }

uint32_t SaveGB::money() const {
    const uint8_t* p = &data_[layoutOf(game_).money];
    if (game_ != GBGame::RBY) return be24(p);
    uint32_t v = 0; // BCD
    for (int i = 0; i < 3; i++) v = v * 100 + (p[i] >> 4) * 10 + (p[i] & 15);
    return v;
}

int SaveGB::currentBox() const { return data_[layoutOf(game_).currentBoxIndex] & 0x7F; }

uint32_t SaveGB::boxOffset(int b) const {
    const Layout& L = layoutOf(game_);
    int listSize = (L.storedSize + 1 + 22) * 20 + 2;
    if (game_ == GBGame::RBY) return b < 6 ? 0x4000 + b * listSize : 0x6000 + (b - 6) * listSize;
    return b < 7 ? 0x4000 + b * (listSize + 2) : 0x6000 + (b - 7) * (listSize + 2);
}

std::vector<GBMon> SaveGB::readList(uint32_t ofs, int capacity, bool party) const {
    const Layout& L = layoutOf(game_);
    int size = party ? L.partySize : L.storedSize;
    int count = data_[ofs];
    std::vector<GBMon> out;
    if (count > capacity) return out; // uninitialisiert (z. B. 0xFF)
    uint32_t mons = ofs + 1 + capacity + 1;
    uint32_t ots = mons + capacity * size;
    uint32_t nicks = ots + capacity * 11;
    for (int i = 0; i < count; i++) {
        const uint8_t* m = &data_[mons + i * size];
        GBMon g;
        g.gen = generation();
        g.raw.assign(m, m + size);
        g.ot = gbToUtf8(&data_[ots + i * 11], 11);
        g.nickname = gbToUtf8(&data_[nicks + i * 11], 11);
        if (game_ == GBGame::RBY) {
            g.species = gen1InternalToNational(m[0]);
            g.hp = be16(m + 1);
            g.level = party ? m[33] : m[3];
            g.heldItem = m[7];
            for (int k = 0; k < 4; k++) { g.moves[k] = m[8 + k]; g.pp[k] = m[29 + k]; }
            g.tid = be16(m + 12);
            g.exp = be24(m + 14);
            for (int k = 0; k < 5; k++) g.statExp[k] = be16(m + 17 + k * 2);
            g.dvs = be16(m + 27);
            if (party) g.maxHp = be16(m + 34);
        } else {
            g.species = m[0];
            g.heldItem = m[1];
            for (int k = 0; k < 4; k++) { g.moves[k] = m[2 + k]; g.pp[k] = m[23 + k]; }
            g.tid = be16(m + 6);
            g.exp = be24(m + 8);
            for (int k = 0; k < 5; k++) g.statExp[k] = be16(m + 11 + k * 2);
            g.dvs = be16(m + 21);
            g.friendship = m[27];
            g.level = m[31];
            if (party) { g.hp = be16(m + 34); g.maxHp = be16(m + 36); }
        }
        out.push_back(g);
    }
    return out;
}

std::vector<GBMon> SaveGB::party() const { return readList(layoutOf(game_).party, 6, true); }

std::vector<GBMon> SaveGB::box(int b) const {
    if (b == currentBox()) return readList(layoutOf(game_).currentBox, 20, false);
    return readList(boxOffset(b), 20, false);
}

std::vector<GBItem> SaveGB::readItems(uint32_t ofs, int capacity) const {
    std::vector<GBItem> out;
    int count = data_[ofs];
    if (count > capacity) return out;
    for (int i = 0; i < count; i++) out.push_back({data_[ofs + 1 + i * 2], data_[ofs + 2 + i * 2]});
    return out;
}

std::vector<GBItem> SaveGB::bag() const { return readItems(layoutOf(game_).bag, 20); }
std::vector<GBItem> SaveGB::balls() const { return game_ == GBGame::RBY ? std::vector<GBItem>{} : readItems(layoutOf(game_).balls, 12); }
std::vector<GBItem> SaveGB::pcItems() const { return readItems(layoutOf(game_).pc, 50); }

std::vector<uint8_t> SaveGB::keyItems() const {
    std::vector<uint8_t> out;
    if (game_ == GBGame::RBY) return out;
    uint32_t o = layoutOf(game_).keys;
    int count = data_[o];
    for (int i = 0; i < count && i < 25; i++) out.push_back(data_[o + 1 + i]);
    return out;
}

} // namespace pw3d
