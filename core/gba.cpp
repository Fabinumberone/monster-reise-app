#include "gba.hpp"

#include <algorithm>
#include <cstring>

#include "bytes.hpp"
#include "gen3tables.hpp"

namespace pw3d {

namespace {

constexpr uint32_t SECTOR = 0x1000, USED = 0xF80, MAIN = 14 * SECTOR;
constexpr uint32_t SIGNATURE = 0x08012025;

// Position des Blocks i (Wachstum, Attacken, EVs, Sonstiges) je PID % 24 (wie PKHeX BlockPosition).
const uint8_t kBlockPos[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

uint16_t checksum32(const uint8_t* p, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 4 <= n; i += 4) sum += rd32(p + i);
    return uint16_t((sum >> 16) + (sum & 0xFFFF));
}

bool slotComplete(const std::vector<uint8_t>& d, int slot, uint32_t& counter) {
    uint32_t seen = 0;
    counter = 0;
    for (int i = 0; i < 14; i++) {
        const uint8_t* s = &d[size_t(slot) * MAIN + size_t(i) * SECTOR];
        if (rd32(s + 0xFF8) != SIGNATURE) return false;
        uint16_t id = rd16(s + 0xFF4);
        if (id >= 14) return false;
        seen |= 1u << id;
        if (id == 0) counter = rd32(s + 0xFFC);
    }
    return seen == 0x3FFF;
}

} // namespace

const char* gbaGameName(GBAGame g) {
    switch (g) {
    case GBAGame::RS: return "Rubin/Saphir";
    case GBAGame::E: return "Smaragd";
    case GBAGame::FRLG: return "Feuerrot/Blattgrün";
    }
    return "?";
}

std::string gen3String(const uint8_t* s, size_t max) {
    std::string out;
    for (size_t i = 0; i < max && s[i] != 0xFF; i++) out += kGen3Chars[s[i]];
    return out;
}

void gen3Encode(const std::string& s, uint8_t* out, size_t max) {
    std::memset(out, 0xFF, max);
    size_t n = 0, i = 0;
    while (i < s.size() && n < max) {
        size_t len = 1;
        unsigned char c = s[i];
        if (c >= 0xF0) len = 4; else if (c >= 0xE0) len = 3; else if (c >= 0xC0) len = 2;
        std::string ch = s.substr(i, len);
        i += len;
        uint8_t code = 0xAC; // ?
        if (ch == "“") code = 0xB2; // schließendes deutsches Anführungszeichen
        else for (int k = 0; k < 0xFF; k++) if (ch == kGen3Chars[k]) { code = uint8_t(k); break; }
        out[n++] = code;
    }
}

// ---------- PK3 ----------

uint32_t PK3::pid() const { return rd32(&d[0]); }
uint16_t PK3::tid() const { return rd16(&d[4]); }
uint16_t PK3::sid() const { return rd16(&d[6]); }
uint16_t PK3::checksum() const { return rd16(&d[0x1C]); }
uint16_t PK3::calcChecksum() const {
    uint16_t c = 0;
    for (int i = 0x20; i < 0x50; i += 2) c = uint16_t(c + rd16(&d[size_t(i)]));
    return c;
}
uint16_t PK3::speciesInternal() const { return rd16(&d[0x20]); }
uint16_t PK3::species() const {
    uint16_t r = speciesInternal();
    return r < kGen3InternalCount ? kGen3InternalToNational[r] : 0;
}
uint16_t PK3::heldItem() const { return rd16(&d[0x22]); }
uint32_t PK3::exp() const { return rd32(&d[0x24]); }
std::array<uint16_t, 4> PK3::moves() const {
    return {rd16(&d[0x2C]), rd16(&d[0x2E]), rd16(&d[0x30]), rd16(&d[0x32])};
}
uint32_t PK3::ivWord() const { return rd32(&d[0x48]); }
bool PK3::isEgg() const { return (ivWord() >> 30) & 1; }
uint8_t PK3::ball() const { return uint8_t((rd16(&d[0x46]) >> 11) & 0xF); }
uint8_t PK3::version() const { return uint8_t((rd16(&d[0x46]) >> 7) & 0xF); }
bool PK3::present() const { return speciesInternal() != 0 && checksum() == calcChecksum(); }
std::string PK3::nickname() const { return gen3String(&d[0x08], 10); }
std::string PK3::otName() const { return gen3String(&d[0x14], 7); }

PK3 pk3Decrypt(const uint8_t* raw, bool party) {
    PK3 pk;
    pk.party = party;
    std::memcpy(pk.d.data(), raw, party ? 100 : 80);
    uint32_t key = rd32(raw) ^ rd32(raw + 4);
    uint8_t enc[48];
    for (int i = 0; i < 48; i += 4) wr32(enc + i, rd32(raw + 0x20 + i) ^ key);
    const uint8_t* pos = kBlockPos[rd32(raw) % 24];
    for (int b = 0; b < 4; b++) std::memcpy(&pk.d[size_t(0x20 + b * 12)], enc + pos[b] * 12, 12);
    return pk;
}

void pk3Encrypt(const PK3& pk, uint8_t* raw) {
    std::memcpy(raw, pk.d.data(), pk.party ? 100 : 80);
    uint32_t key = pk.pid() ^ rd32(&pk.d[4]);
    const uint8_t* pos = kBlockPos[pk.pid() % 24];
    uint8_t plain[48];
    for (int b = 0; b < 4; b++) std::memcpy(plain + pos[b] * 12, &pk.d[size_t(0x20 + b * 12)], 12);
    for (int i = 0; i < 48; i += 4) wr32(raw + 0x20 + i, rd32(plain + i) ^ key);
}

// ---------- Spielstand ----------

bool SaveGBA::load(std::vector<uint8_t> data, std::string& err) {
    if (data.size() < 2 * MAIN) { err = "Kein Gen-3-Spielstand (zu klein)"; return false; }
    data_ = std::move(data);
    uint32_t c0, c1;
    bool v0 = slotComplete(data_, 0, c0), v1 = slotComplete(data_, 1, c1);
    if (!v0 && !v1) { err = "Kein Gen-3-Spielstand (Sektoren fehlen)"; return false; }
    active_ = !v0 ? 1 : !v1 ? 0 : ((c1 > c0 && !(c1 == 0xFFFFFFFF && c0 == 0)) || (c0 == 0xFFFFFFFF && c1 == 0)) ? 1 : 0;
    small_.assign(USED, 0);
    large_.assign(4 * USED, 0);
    storage_.assign(9 * USED, 0);
    for (int i = 0; i < 14; i++) {
        const uint8_t* s = &data_[size_t(active_) * MAIN + size_t(i) * SECTOR];
        uint16_t id = rd16(s + 0xFF4);
        uint8_t* dst = id == 0 ? small_.data() : id <= 4 ? &large_[(id - 1) * USED] : &storage_[(id - 5) * USED];
        std::memcpy(dst, s, USED);
    }
    uint32_t code = rd32(&small_[0xAC]);
    game_ = code == 0 ? GBAGame::RS : code == 1 ? GBAGame::FRLG : GBAGame::E;
    return true;
}

bool SaveGBA::checksumsValid() const {
    for (int i = 0; i < 14; i++) {
        const uint8_t* s = &data_[size_t(active_) * MAIN + size_t(i) * SECTOR];
        if (checksum32(s, USED) != rd16(s + 0xFF6)) return false;
    }
    return true;
}

std::string SaveGBA::trainerName() const { return gen3String(&small_[0], 7); }
uint16_t SaveGBA::tid() const { return rd16(&small_[0x0A]); }
uint16_t SaveGBA::sid() const { return rd16(&small_[0x0C]); }
uint32_t SaveGBA::securityKey() const {
    switch (game_) {
    case GBAGame::RS: return 0;
    case GBAGame::E: return rd32(&small_[0xAC]);
    case GBAGame::FRLG: return rd32(&small_[0xF20]);
    }
    return 0;
}
uint32_t SaveGBA::money() const { return rd32(&large_[moneyOfs()]) ^ securityKey(); }

std::vector<PK3> SaveGBA::party() const {
    std::vector<PK3> out;
    int n = std::min<int>(large_[partyCountOfs()], 6);
    for (int i = 0; i < n; i++) out.push_back(pk3Decrypt(&large_[partyCountOfs() + 4 + size_t(i) * 100], true));
    return out;
}

PK3 SaveGBA::boxSlot(int box, int slot) const {
    return pk3Decrypt(&storage_[4 + (size_t(box) * SLOTS + size_t(slot)) * 80], false);
}

void SaveGBA::setParty(const std::vector<PK3>& mons) {
    uint32_t o = partyCountOfs();
    size_t n = std::min<size_t>(mons.size(), 6);
    large_[o] = uint8_t(n);
    std::memset(&large_[o + 4], 0, 6 * 100);
    for (size_t i = 0; i < n; i++) {
        PK3 pk = mons[i];
        pk.party = true;
        pk3Encrypt(pk, &large_[o + 4 + i * 100]);
    }
}

void SaveGBA::setBoxSlot(int box, int slot, const PK3* pk) {
    uint8_t* dst = &storage_[4 + (size_t(box) * SLOTS + size_t(slot)) * 80];
    if (!pk) { std::memset(dst, 0, 80); return; }
    PK3 p = *pk;
    p.party = false;
    pk3Encrypt(p, dst);
}

void SaveGBA::setPouch(Pouch p, const std::vector<GBAItem>& items) {
    PouchInfo pi = pouchInfo(p);
    uint16_t key = p == Pouch::PC ? 0 : uint16_t(securityKey());
    for (int i = 0; i < pi.count; i++) {
        uint8_t* q = &large_[inventoryOfs() + pi.ofs + size_t(i) * 4];
        GBAItem it = size_t(i) < items.size() ? items[size_t(i)] : GBAItem{};
        wr16(q, it.id);
        wr16(q + 2, uint16_t((it.id ? it.count : 0) ^ key));
    }
}

void SaveGBA::setMoney(uint32_t v) { wr32(&large_[moneyOfs()], std::min<uint32_t>(v, 999999) ^ securityKey()); }

void SaveGBA::setTrainer(const std::string& name, uint16_t tid, uint16_t sid, uint8_t gender) {
    gen3Encode(name, &small_[0], 7);
    small_[7] = 0xFF;
    small_[8] = gender & 1;
    wr16(&small_[0x0A], tid);
    wr16(&small_[0x0C], sid);
}

void SaveGBA::registerDex(uint16_t species) {
    if (species == 0 || species > 386) return;
    int bit = species - 1;
    auto set = [&](std::vector<uint8_t>& buf, uint32_t ofs) { buf[ofs + size_t(bit >> 3)] |= uint8_t(1 << (bit & 7)); };
    set(small_, 0x18 + 0x10); // gefangen
    set(small_, 0x18 + 0x44); // gesehen
    set(large_, seen2Ofs());  // Kopien von „gesehen“
    set(large_, seen3Ofs());
}

void SaveGBA::finalize() {
    for (int i = 0; i < 14; i++) {
        uint8_t* s = &data_[size_t(active_) * MAIN + size_t(i) * SECTOR];
        uint16_t id = rd16(s + 0xFF4);
        const uint8_t* src = id == 0 ? small_.data() : id <= 4 ? &large_[(id - 1) * USED] : &storage_[(id - 5) * USED];
        std::memcpy(s, src, USED);
        wr16(s + 0xFF6, checksum32(s, USED));
    }
}

SaveGBA::PouchInfo SaveGBA::pouchInfo(Pouch p) const {
    // Positionen relativ zum Inventar (PKHeX PlayerBag3RS/E/FRLG)
    static const PouchInfo rs[6] = {{0x0C8, 20}, {0x118, 20}, {0x168, 16}, {0x1A8, 64}, {0x2A8, 46}, {0x000, 50}};
    static const PouchInfo e[6] = {{0x0C8, 30}, {0x140, 30}, {0x1B8, 16}, {0x1F8, 64}, {0x2F8, 46}, {0x000, 50}};
    static const PouchInfo fr[6] = {{0x078, 42}, {0x120, 30}, {0x198, 13}, {0x1CC, 58}, {0x2B4, 43}, {0x000, 30}};
    const PouchInfo* t = game_ == GBAGame::RS ? rs : game_ == GBAGame::E ? e : fr;
    return t[int(p)];
}

std::vector<GBAItem> SaveGBA::pouch(Pouch p) const {
    PouchInfo pi = pouchInfo(p);
    uint16_t key = p == Pouch::PC ? 0 : uint16_t(securityKey());
    std::vector<GBAItem> out;
    for (int i = 0; i < pi.count; i++) {
        const uint8_t* q = &large_[inventoryOfs() + pi.ofs + size_t(i) * 4];
        uint16_t id = rd16(q), n = uint16_t(rd16(q + 2) ^ key);
        if (id) out.push_back({id, n});
    }
    return out;
}

} // namespace pw3d
