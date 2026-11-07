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
        for (uint32_t part : {0u, PARTITION}) {
            if (rd32(&data_[part + l.gSize - 0x0C]) != l.gSize) continue;
            game_ = l.game;
            generalSize_ = l.gSize;
            storageSize_ = l.sSize;
            footer_ = l.footer;
            general_ = activeBlock(data_, 0, l.gSize);
            storage_ = activeBlock(data_, l.sStart, l.sSize);
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
