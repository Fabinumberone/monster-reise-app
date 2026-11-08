#include "nds.hpp"

#include <algorithm>
#include <cstring>

#include "bytes.hpp"
#include "crc16.hpp"
#include "gen4tables.hpp"

namespace pw3d {

namespace {

constexpr uint32_t PARTITION = 0x40000;

// Position des Blocks i je Vertauschungswert (wie PKHeX BlockPosition)
const uint8_t kBlockPos[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

void cryptLCRNG(uint8_t* p, size_t len, uint32_t seed) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        wr16(p + i, uint16_t(rd16(p + i) ^ (seed >> 16)));
    }
}

int compareCounters(uint32_t a, uint32_t b) { // 0 erster, 1 zweiter, 2 gleich
    if (a == 0xFFFFFFFF && b != 0xFFFFFFFE) return 1;
    if (b == 0xFFFFFFFF && a != 0xFFFFFFFE) return 0;
    if (a > b) return 0;
    if (a < b) return 1;
    return 2;
}

// Aktive Partition eines Gen-4-Blocks (wie PKHeX SAV4BlockDetection.CompareFooters)
uint32_t activeBlock(const std::vector<uint8_t>& d, uint32_t begin, uint32_t length) {
    uint32_t o1 = begin + length - 0x14, o2 = o1 + PARTITION;
    int r = compareCounters(rd32(&d[o1]), rd32(&d[o2]));
    if (r == 2) r = compareCounters(rd32(&d[o1 + 4]), rd32(&d[o2 + 4])) == 1 ? 1 : 0;
    return r == 1 ? begin + PARTITION : begin;
}

struct G4Layout { NDSGame game; uint32_t gSize, sStart, sSize, footer; };
const G4Layout kG4[] = {
    {NDSGame::DP, 0xC100, 0xC100, 0x121E0, 0x14},
    {NDSGame::Pt, 0xCF2C, 0xCF2C, 0x121E4, 0x14},
    {NDSGame::HGSS, 0xF628, 0xF700, 0x12310, 0x10},
};

bool g4BlockValid(const std::vector<uint8_t>& d, uint32_t ofs, uint32_t size, uint32_t footer) {
    if (ofs + size > d.size()) return false;
    return crc16Ccitt(&d[ofs], size - footer) == rd16(&d[ofs + size - 2]);
}

} // namespace

const char* ndsGameName(NDSGame g) {
    switch (g) {
    case NDSGame::DP: return "Diamant/Perl";
    case NDSGame::Pt: return "Platin";
    case NDSGame::HGSS: return "HeartGold/SoulSilver";
    case NDSGame::BW: return "Schwarz/Weiß";
    case NDSGame::B2W2: return "Schwarz 2/Weiß 2";
    }
    return "?";
}

std::string ndsString(const uint8_t* s, size_t maxChars, int gen) {
    std::string out;
    for (size_t i = 0; i < maxChars; i++) {
        uint16_t c = rd16(s + i * 2);
        if (c == 0xFFFF || c == 0) break;
        if (gen == 4) {
            if (c < kGen4CharCount) out += kGen4Chars[c];
            continue;
        }
        if (c == 0x246D) c = 0x2642; // ♂
        else if (c == 0x246E) c = 0x2640; // ♀
        uint8_t tmp[4];
        wr16(tmp, c);
        wr16(tmp + 2, 0);
        out += utf16ToUtf8(tmp, 1);
    }
    return out;
}

void ndsEncodeString(const std::string& s, uint8_t* out, size_t maxChars, int gen) {
    std::memset(out, 0, maxChars * 2);
    size_t n = 0, i = 0;
    while (i < s.size() && n + 1 < maxChars) {
        size_t len = 1;
        unsigned char c = s[i];
        if (c >= 0xF0) len = 4; else if (c >= 0xE0) len = 3; else if (c >= 0xC0) len = 2;
        std::string ch = s.substr(i, len);
        i += len;
        uint16_t code = 0;
        if (gen == 4) {
            code = 0x1AC; // ?
            for (int k = 1; k < kGen4CharCount; k++)
                if (ch == kGen4Chars[k]) { code = uint16_t(k); break; }
        } else {
            uint8_t tmp[8] = {};
            utf8ToUtf16(ch, tmp, 2);
            code = rd16(tmp);
            if (code == 0x2642) code = 0x246D; // ♂
            else if (code == 0x2640) code = 0x246E; // ♀
        }
        wr16(out + n * 2, code);
        n++;
    }
    wr16(out + n * 2, 0xFFFF);
}

// ---------- PK4/PK5 ----------

uint32_t PK45::pid() const { return rd32(&d[0]); }
uint16_t PK45::checksum() const { return rd16(&d[6]); }
uint16_t PK45::calcChecksum() const {
    uint16_t c = 0;
    for (int i = 8; i < 0x88; i += 2) c = uint16_t(c + rd16(&d[size_t(i)]));
    return c;
}
bool PK45::present() const { return species() != 0 && checksum() == calcChecksum(); }
uint16_t PK45::species() const { return rd16(&d[0x08]); }
uint16_t PK45::heldItem() const { return rd16(&d[0x0A]); }
uint16_t PK45::tid() const { return rd16(&d[0x0C]); }
uint16_t PK45::sid() const { return rd16(&d[0x0E]); }
uint32_t PK45::exp() const { return rd32(&d[0x10]); }
std::array<uint16_t, 4> PK45::moves() const { return {rd16(&d[0x28]), rd16(&d[0x2A]), rd16(&d[0x2C]), rd16(&d[0x2E])}; }
uint32_t PK45::iv32() const { return rd32(&d[0x38]); }
uint8_t PK45::nature() const { return gen == 5 ? d[0x41] : uint8_t(pid() % 25); }
uint8_t PK45::ball() const {
    if (gen == 4 && d[0x86] > d[0x83]) return d[0x86]; // HGSS-Bälle (Turbo-, Freund- …)
    return d[0x83];
}
std::string PK45::nickname() const { return ndsString(&d[0x48], 11, gen); }
std::string PK45::otName() const { return ndsString(&d[0x68], 8, gen); }

PK45 pk45Decrypt(const uint8_t* raw, int gen, bool party) {
    PK45 pk;
    pk.gen = gen;
    pk.party = party;
    size_t len = party ? (gen == 5 ? 220 : 236) : 136;
    uint8_t buf[236] = {};
    std::memcpy(buf, raw, len);
    cryptLCRNG(buf + 8, 0x80, rd16(buf + 6));
    if (party) cryptLCRNG(buf + 0x88, len - 0x88, rd32(buf));
    std::memcpy(pk.d.data(), buf, len);
    const uint8_t* pos = kBlockPos[((rd32(buf) >> 13) & 31) % 24];
    for (int b = 0; b < 4; b++) std::memcpy(&pk.d[size_t(8 + b * 32)], buf + 8 + pos[b] * 32, 32);
    return pk;
}

void pk45Encrypt(const PK45& pk, uint8_t* raw) {
    size_t len = pk.party ? (pk.gen == 5 ? 220 : 236) : 136;
    uint8_t buf[236] = {};
    std::memcpy(buf, pk.d.data(), len);
    const uint8_t* pos = kBlockPos[((pk.pid() >> 13) & 31) % 24];
    for (int b = 0; b < 4; b++) std::memcpy(buf + 8 + pos[b] * 32, &pk.d[size_t(8 + b * 32)], 32);
    cryptLCRNG(buf + 8, 0x80, rd16(buf + 6));
    if (pk.party) cryptLCRNG(buf + 0x88, len - 0x88, rd32(buf));
    std::memcpy(raw, buf, len);
}

// ---------- Spielstand ----------

bool SaveNDS::load(std::vector<uint8_t> data, std::string& err) {
    if (data.size() < 0x80000) { err = "Kein DS-Spielstand (zu klein)"; return false; }
    data_ = std::move(data);
    // Gen 5: Prüfsummentabelle prüfen
    struct G5 { NDSGame g; uint32_t table, len; };
    for (G5 c : {G5{NDSGame::BW, 0x23F00, 0x8C}, G5{NDSGame::B2W2, 0x25F00, 0x94}}) {
        if (crc16Ccitt(&data_[c.table], c.len) == rd16(&data_[c.table + c.len + 0xE])) { game_ = c.g; return true; } // Prüfsumme bei Tabelle + Länge + 0xE
    }
    // Gen 4: Größe im Fuß (Ende − 0x0C) muss zur Blockgröße passen
    for (const G4Layout& l : kG4) {
        for (uint32_t part : {uint32_t(0), PARTITION}) {
            if (rd32(&data_[part + l.gSize - 0x0C]) != l.gSize) continue;
            game_ = l.game;
            generalSize_ = l.gSize;
            storageSize_ = l.sSize;
            footer_ = l.footer;
            general_ = activeBlock(data_, 0, l.gSize);
            storage_ = activeBlock(data_, l.sStart, l.sSize);
            // Wie das Spiel: ist der neuere Block beschädigt (z. B. halb gespeichert), den anderen nehmen
            for (auto [blk, size] : {std::pair<uint32_t*, uint32_t>{&general_, l.gSize}, {&storage_, l.sSize}}) {
                uint32_t other = *blk >= PARTITION ? *blk - PARTITION : *blk + PARTITION;
                if (!g4BlockValid(data_, *blk, size, l.footer) && g4BlockValid(data_, other, size, l.footer)) *blk = other;
            }
            return true;
        }
    }
    err = "Kein Gen-4/5-Spielstand erkannt";
    return false;
}

bool SaveNDS::checksumsValid() const {
    if (generation() == 4)
        return g4BlockValid(data_, general_, generalSize_, footer_) && g4BlockValid(data_, storage_, storageSize_, footer_);
    uint32_t table = game_ == NDSGame::BW ? 0x23F00 : 0x25F00;
    uint32_t len = game_ == NDSGame::BW ? 0x8C : 0x94;
    return crc16Ccitt(&data_[table], len) == rd16(&data_[table + len + 0xE]);
}

uint32_t SaveNDS::trainerOfs() const {
    switch (game_) {
    case NDSGame::DP: case NDSGame::HGSS: return general_ + 0x64;
    case NDSGame::Pt: return general_ + 0x68;
    default: return 0x19400 + 4; // PlayerData5: Name ab +4
    }
}

std::string SaveNDS::trainerName() const { return ndsString(&data_[trainerOfs()], 8, generation()); }
uint16_t SaveNDS::tid() const { return rd16(&data_[trainerOfs() + 0x10]); }
uint16_t SaveNDS::sid() const { return rd16(&data_[trainerOfs() + 0x12]); }
uint8_t SaveNDS::trainerGender() const { return generation() == 4 ? data_[trainerOfs() + 0x18] : data_[0x19400 + 0x21]; }
uint8_t SaveNDS::language() const { return generation() == 4 ? data_[trainerOfs() + 0x19] : data_[0x19400 + 0x1E]; }
uint32_t SaveNDS::money() const {
    if (generation() == 4) return rd32(&data_[trainerOfs() + 0x14]);
    return rd32(&data_[game_ == NDSGame::BW ? 0x21200 : 0x21100]);
}

uint32_t SaveNDS::partyOfs() const {
    switch (game_) {
    case NDSGame::DP: case NDSGame::HGSS: return general_ + 0x98;
    case NDSGame::Pt: return general_ + 0xA0;
    default: return 0x18E00 + 8;
    }
}

std::vector<PK45> SaveNDS::party() const {
    std::vector<PK45> out;
    uint32_t o = partyOfs();
    int n = std::min<int>(generation() == 4 ? data_[o - 4] : data_[o - 4], 6);
    size_t sz = generation() == 5 ? 220 : 236;
    for (int i = 0; i < n; i++) out.push_back(pk45Decrypt(&data_[o + size_t(i) * sz], generation(), true));
    return out;
}

uint32_t SaveNDS::boxOfs(int box, int slot) const {
    uint32_t s = uint32_t(slot) * 136;
    switch (game_) {
    case NDSGame::DP: case NDSGame::Pt: return storage_ + 4 + uint32_t(box) * 0xFF0 + s;
    case NDSGame::HGSS: return storage_ + uint32_t(box) * 0x1000 + s;
    default: return 0x400 + uint32_t(box) * 0x1000 + s;
    }
}

PK45 SaveNDS::boxSlot(int box, int slot) const { return pk45Decrypt(&data_[boxOfs(box, slot)], generation(), false); }

SaveNDS::PouchInfo SaveNDS::pouchInfo(Pouch p) const {
    // Gen 4 relativ zum allgemeinen Block + Bag-Basis, Gen 5 absolut (PKHeX PlayerBag4*/5*)
    static const PouchInfo dp[8] = {{0x000, 165}, {0x294, 50}, {0x35C, 100}, {0x4EC, 12}, {0x51C, 40}, {0x5BC, 64}, {0x6BC, 15}, {0x6F8, 13}};
    static const PouchInfo hg[8] = {{0x000, 165}, {0x294, 50}, {0x35C, 101}, {0x4F0, 12}, {0x520, 40}, {0x5C0, 64}, {0x6C0, 24}, {0x720, 13}};
    static const PouchInfo bw[8] = {{0x000, 310}, {0x4D8, 83}, {0x624, 109}, {0, 0}, {0x7D8, 48}, {0x898, 74}, {0, 0}, {0, 0}};
    int i = int(p);
    switch (game_) {
    case NDSGame::DP: return {general_ + 0x624 + dp[i].ofs, dp[i].count};
    case NDSGame::Pt: return {general_ + 0x630 + dp[i].ofs, dp[i].count};
    case NDSGame::HGSS: return {general_ + 0x644 + hg[i].ofs, hg[i].count};
    default: return {0x18400 + bw[i].ofs, bw[i].count};
    }
}

void SaveNDS::setParty(const std::vector<PK45>& mons) {
    uint32_t o = partyOfs();
    size_t sz = generation() == 5 ? 220 : 236;
    size_t n = std::min<size_t>(mons.size(), 6);
    data_[o - 4] = uint8_t(n);
    PK45 blank;
    blank.gen = generation();
    blank.party = true;
    for (size_t i = 0; i < 6; i++) {
        PK45 pk = i < n ? mons[i] : blank;
        pk.gen = generation();
        pk.party = true;
        pk45Encrypt(pk, &data_[o + i * sz]);
    }
}

void SaveNDS::setBoxSlot(int box, int slot, const PK45* pk) {
    PK45 p;
    if (pk) p = *pk;
    p.gen = generation();
    p.party = false;
    pk45Encrypt(p, &data_[boxOfs(box, slot)]);
}

void SaveNDS::setPouch(Pouch p, const std::vector<NDSItem>& items) {
    PouchInfo pi = pouchInfo(p);
    for (int i = 0; i < pi.count; i++) {
        uint8_t* q = &data_[pi.ofs + size_t(i) * 4];
        NDSItem it = size_t(i) < items.size() ? items[size_t(i)] : NDSItem{};
        wr16(q, it.id);
        wr16(q + 2, it.id ? it.count : 0);
    }
}

void SaveNDS::unlockAllBoxes() {
    if (generation() == 5) data_[0x3DD] = 24; // PKHeX BoxLayout5 (Block 0)
}

void SaveNDS::setMoney(uint32_t v) {
    v = std::min<uint32_t>(v, 999999);
    if (generation() == 4) wr32(&data_[trainerOfs() + 0x14], v);
    else wr32(&data_[game_ == NDSGame::BW ? 0x21200 : 0x21100], v);
}

void SaveNDS::setTrainer(const std::string& name, uint16_t tid, uint16_t sid, uint8_t gender) {
    ndsEncodeString(name, &data_[trainerOfs()], 8, generation());
    wr16(&data_[trainerOfs() + 0x10], tid);
    wr16(&data_[trainerOfs() + 0x12], sid);
    if (generation() == 4) data_[trainerOfs() + 0x18] = gender & 1;
    else data_[0x19400 + 0x21] = gender & 1;
}

void SaveNDS::registerDex(const PK45& pk) {
    uint16_t sp = pk.species();
    if (pk.isEgg() || sp == 0) return;
    auto setBit = [&](uint32_t ofs, int bit) { data_[ofs + uint32_t(bit >> 3)] |= uint8_t(1 << (bit & 7)); };
    auto getBit = [&](uint32_t ofs, int bit) { return (data_[ofs + uint32_t(bit >> 3)] >> (bit & 7)) & 1; };
    uint8_t gender = pk.gender() == 1 ? 1 : 0;
    int b = sp - 1;
    if (generation() == 4) {
        if (sp > 493) return;
        uint32_t dex = general_ + (game_ == NDSGame::DP ? 0x12DC : game_ == NDSGame::Pt ? 0x1328 : 0x12B8);
        const uint32_t R = 0x40;
        bool seen = getBit(dex + 4 + R, b);
        setBit(dex + 4, b);     // gefangen
        if (!seen) {            // zuerst gesehenes Geschlecht in beiden Bereichen
            if (gender) { setBit(dex + 4 + 2 * R, b); setBit(dex + 4 + 3 * R, b); }
        } else if (getBit(dex + 4 + 2 * R, b) == getBit(dex + 4 + 3 * R, b) && gender) {
            setBit(dex + 4 + 3 * R, b); // zweites Geschlecht
        }
        setBit(dex + 4 + R, b); // gesehen
        if (game_ != NDSGame::DP) { // Sprachbits (DP nutzen eine eigene Artenliste, hier ausgelassen)
            int l = pk.language() - 1;
            int lang = l == 3 ? 4 : l == 4 ? 3 : l > 5 ? 0 : l < 0 ? 1 : l;
            uint32_t ofs = dex + 0x108 + (game_ == NDSGame::HGSS ? 0x3C : 0x20) + sp;
            data_[ofs] |= uint8_t(1 << (lang & 7));
        }
        return;
    }
    if (sp > 649) return;
    bool bw = game_ == NDSGame::BW;
    uint32_t dex = bw ? 0x21600 : 0x21400;
    const uint32_t S = 0x54;
    bool shiny = (pk.tid() ^ pk.sid() ^ (pk.pid() >> 16) ^ (pk.pid() & 0xFFFF)) < 8;
    int region = (shiny ? 2 : 0) | gender;
    setBit(dex + 0x8, b);                                     // gefangen
    setBit(dex + 0x5C + uint32_t(region) * S, b);             // gesehen
    bool shown = false;
    for (int i = 0; i < 4; i++) shown |= getBit(dex + 0x1AC + uint32_t(i) * S, b);
    if (!shown) setBit(dex + 0x1AC + uint32_t(region) * S, b); // angezeigt
    if (sp <= 493) {
        int l = pk.language() - 1;
        if (l > 5) l--;
        if (l >= 0 && l <= 5) setBit(dex + (bw ? 0x320 : 0x328), b * 7 + l);
    }
}

int SaveNDS::dexCaughtCount() const {
    uint32_t ofs = generation() == 4 ? general_ + (game_ == NDSGame::DP ? 0x12DC : game_ == NDSGame::Pt ? 0x1328 : 0x12B8) + 4
                                     : (game_ == NDSGame::BW ? 0x21600 : 0x21400) + 0x8;
    int n = 0, max = generation() == 4 ? 493 : 649;
    for (int b = 0; b < max; b++) n += (data_[ofs + uint32_t(b >> 3)] >> (b & 7)) & 1;
    return n;
}

void SaveNDS::finalize() {
    if (generation() == 4) {
        // Beide Hälften bekommen denselben Inhalt (jede mit eigenem Zähler und eigener Prüfsumme): HeartGold/
        // SoulSilver schreibt beim Speichern nur geänderte Boxen in die andere Hälfte und verlässt sich darauf,
        // dass der Rest dort schon stimmt. Sonst ist der PC-Block danach dort beschädigt (2026-11-08, Modul).
        for (auto [ofs, size] : {std::pair<uint32_t, uint32_t>{general_, generalSize_}, {storage_, storageSize_}}) {
            uint32_t other = ofs >= PARTITION ? ofs - PARTITION : ofs + PARTITION;
            std::memcpy(&data_[other], &data_[ofs], size - 0x14); // ohne Fuß (Zähler ab Ende − 0x14)
            wr16(&data_[ofs + size - 2], crc16Ccitt(&data_[ofs], size - footer_));
            wr16(&data_[other + size - 2], crc16Ccitt(&data_[other], size - footer_));
        }
        return;
    }
    // Gen 5: geänderte Blöcke (Boxen, Inventar, Team, Trainer, Geld) + Prüfsummentabelle
    bool bw = game_ == NDSGame::BW;
    uint32_t table = bw ? 0x23F00 : 0x25F00, tlen = bw ? 0x8C : 0x94;
    struct B { uint32_t ofs, len; int idx; };
    std::vector<B> blocks;
    blocks.push_back({0x0, 0x3E0, 0}); // Boxnamen und freigeschaltete Boxen
    for (int i = 0; i < 24; i++) blocks.push_back({0x400 + uint32_t(i) * 0x1000, 0xFF0, i + 1});
    blocks.push_back({0x18400, bw ? 0x9C0u : 0x9ECu, 25});
    blocks.push_back({0x18E00, 0x534, 26});
    blocks.push_back({0x19400, bw ? 0x68u : 0xB0u, 27});
    blocks.push_back({bw ? 0x21200u : 0x21100u, bw ? 0xECu : 0xF0u, 52});
    blocks.push_back({bw ? 0x21600u : 0x21400u, bw ? 0x4D4u : 0x4DCu, bw ? 55 : 54}); // Pokédex
    for (const B& b : blocks) {
        uint16_t c = crc16Ccitt(&data_[b.ofs], b.len);
        wr16(&data_[b.ofs + b.len + 2], c);
        wr16(&data_[table + uint32_t(b.idx) * 2], c);
    }
    wr16(&data_[table + tlen + 0xE], crc16Ccitt(&data_[table], tlen));
}

bool SaveNDS::hasPouch(Pouch p) const { return pouchInfo(p).count > 0; }

std::vector<NDSItem> SaveNDS::pouch(Pouch p) const {
    PouchInfo pi = pouchInfo(p);
    std::vector<NDSItem> out;
    for (int i = 0; i < pi.count; i++) {
        const uint8_t* q = &data_[pi.ofs + size_t(i) * 4];
        if (rd16(q)) out.push_back({rd16(q), rd16(q + 2)});
    }
    return out;
}

} // namespace pw3d

namespace pw3d {
static int bits(unsigned v) { int n = 0; for (; v; v &= v - 1) n++; return n; }

// Gen 4: Trainerdaten (allgemein + 0x64, Platin + 0x68): Orden +0x1A (HGSS Kanto +0x1F), Spielzeit +0x22
// (u16 Stunden, +2 Minuten). Gen 5: Spielzeit 0x19400 + 0x24, Orden BW 0x21200 + 4 / S2W2 0x21100 + 4.
int SaveNDS::playMinutes() const {
    uint32_t o = generation() == 4 ? trainerOfs() + 0x22 : 0x19400 + 0x24;
    return (data_[o] | (data_[o + 1] << 8)) * 60 + data_[o + 2];
}
int SaveNDS::badgeCount() const {
    if (generation() == 5) return bits(data_[(game_ == NDSGame::BW ? 0x21200 : 0x21100) + 4]);
    int n = bits(data_[trainerOfs() + 0x1A]);
    if (game_ == NDSGame::HGSS) n += bits(data_[trainerOfs() + 0x1F]);
    return n;
}
int SaveNDS::badgeMax() const { return game_ == NDSGame::HGSS ? 16 : 8; }
} // namespace pw3d
