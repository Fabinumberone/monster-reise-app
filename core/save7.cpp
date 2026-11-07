#include "save7.hpp"

#include "bytes.hpp"
#include "crc16.hpp"
#include "memecrypto.hpp"

namespace pw3d {

namespace {

constexpr uint32_t BEEF = 0x42454546;
constexpr uint16_t SIGNATURE_BLOCK_ID = 0x24;
constexpr uint32_t SIGNATURE_OFS = 0x100, SIGNATURE_LEN = 0x80;

// Blockpositionen nach PKHeX SaveBlockAccessor7SM/USUM. USUM an Ultramond bestätigt.
constexpr Layout7 LAYOUT_USUM{0x6CC00, 0x1400, 0x1600, 0x5200, 32, 0x4404, 0x2C00, 807};
constexpr Layout7 LAYOUT_SM{0x6BE00, 0x1200, 0x1400, 0x4E00, 32, 0x4004, 0x2A00, 802};

// Pokédex (relativ zum Dex-Block, nach PKHeX Zukan7, an Ultramond bestätigt).
constexpr uint32_t DEX_CAUGHT = 0x88;
constexpr uint32_t DEX_SEEN = 0xF0;    // 4 × 0x8C: m, w, m schillernd, w schillernd
constexpr uint32_t DEX_SECTION = 0x8C;
constexpr uint32_t DEX_DISPLAYED = DEX_SEEN + 4 * DEX_SECTION; // 0x320
constexpr uint32_t DEX_LANG = 0x550;   // 9 Bit je Art
constexpr uint32_t DEX_LANG_BYTES = 0x398;

int langIndex7(uint8_t lang) {
    if (lang == 0 || lang == 6 || lang > 10) return -1;
    return lang >= 7 ? lang - 2 : lang - 1;
}

void setBit(uint8_t* base, uint32_t bit) { base[bit >> 3] |= uint8_t(1u << (bit & 7)); }
bool getBit(const uint8_t* base, uint32_t bit) { return (base[bit >> 3] >> (bit & 7)) & 1; }

} // namespace

const char* Save7::gameTitle() const {
    switch (game_) {
    case Game7::SM: return "Sonne/Mond";
    case Game7::USUM: return "Ultrasonne/Ultramond";
    default: return "unbekannt";
    }
}

bool Save7::load(std::vector<uint8_t> data, std::string& err) {
    data_ = std::move(data);
    if (data_.size() == LAYOUT_USUM.size) { game_ = Game7::USUM; layout_ = LAYOUT_USUM; }
    else if (data_.size() == LAYOUT_SM.size) { game_ = Game7::SM; layout_ = LAYOUT_SM; }
    else { err = "Unbekannte Größe " + std::to_string(data_.size()) + " Byte"; return false; }

    uint32_t magicAt = 0;
    for (uint32_t p = uint32_t(data_.size()) - 0x200; p + 4 <= data_.size(); p += 4)
        if (rd32(&data_[p]) == BEEF) { magicAt = p; break; }
    if (!magicAt) { err = "Blocktabelle nicht gefunden"; return false; }
    tableAt_ = magicAt + 4;

    blocks_.clear();
    uint32_t offset = 0;
    for (uint32_t p = tableAt_; p + 8 <= data_.size(); p += 8) {
        Block6 b;
        b.length = rd32(&data_[p]);
        b.id = rd16(&data_[p + 4]);
        b.storedCrc = rd16(&data_[p + 6]);
        if (b.length == 0 || offset + b.length > magicAt) break;
        b.offset = offset;
        b.computedCrc = blockCrc(b);
        blocks_.push_back(b);
        offset += b.length;
        if (offset & 0x1FF) offset += 0x200 - (offset & 0x1FF);
    }
    if (blocks_.empty()) { err = "Blocktabelle ist leer"; return false; }
    return true;
}

uint16_t Save7::blockCrc(const Block6& b) const {
    if (b.id != SIGNATURE_BLOCK_ID) return crc16Invert(&data_[b.offset], b.length);
    std::vector<uint8_t> tmp(data_.begin() + b.offset, data_.begin() + b.offset + b.length);
    for (uint32_t i = SIGNATURE_OFS; i < SIGNATURE_OFS + SIGNATURE_LEN && i < tmp.size(); i++) tmp[i] = 0;
    return crc16Invert(tmp.data(), tmp.size());
}

bool Save7::checksumsValid() const {
    for (auto& b : blocks_)
        if (!b.ok()) return false;
    return true;
}

bool Save7::signatureValid() const { return verifySave7(data_); }
bool Save7::valid() const { return checksumsValid() && signatureValid(); }

void Save7::finalize() {
    for (size_t i = 0; i < blocks_.size(); i++) {
        auto& b = blocks_[i];
        b.computedCrc = blockCrc(b);
        b.storedCrc = b.computedCrc;
        wr16(&data_[tableAt_ + i * 8 + 6], b.storedCrc);
    }
    signSave7(data_); // deckt die Blocktabelle ab, daher nach den Prüfsummen
}

Trainer6 Save7::trainer() const {
    const uint8_t* p = &data_[layout_.trainer];
    Trainer6 t;
    t.tid = rd16(p);
    t.sid = rd16(p + 2);
    t.gender = p[5];
    t.name = utf16ToUtf8(p + 0x38, 13);
    return t;
}

void Save7::setTrainer(const Trainer6& t) {
    uint8_t* p = &data_[layout_.trainer];
    wr16(p, t.tid);
    wr16(p + 2, t.sid);
    p[5] = t.gender;
    utf8ToUtf16(t.name, p + 0x38, 13);
}

uint32_t Save7::money() const { return rd32(&data_[layout_.money]); }
void Save7::setMoney(uint32_t v) { wr32(&data_[layout_.money], v); }

int Save7::partyCount() const { return data_[layout_.party + 6 * PK6_PARTY_SIZE]; }
void Save7::setPartyCount(int n) { data_[layout_.party + 6 * PK6_PARTY_SIZE] = uint8_t(n); }
PK6 Save7::partySlot(int i) const { return PK6::decrypt(&data_[layout_.party + i * PK6_PARTY_SIZE], PK6_PARTY_SIZE); }
void Save7::setPartySlot(int i, PK6 pk) { pk.encrypt(&data_[layout_.party + i * PK6_PARTY_SIZE], PK6_PARTY_SIZE); }

uint32_t Save7::boxOffset(int box, int slot) const {
    return layout_.box + uint32_t(box * BOX_SLOTS + slot) * PK6_BOX_SIZE;
}
PK6 Save7::boxSlot(int box, int slot) const { return PK6::decrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }
void Save7::setBoxSlot(int box, int slot, PK6 pk) { pk.encrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

void Save7::registerDex(const PK6& pk) {
    uint16_t sp = pk.species();
    if (sp == 0 || sp > maxSpecies() || pk.isEgg()) return;
    uint8_t* dex = &data_[layout_.dex];
    uint32_t bit = sp - 1u;
    setBit(dex + DEX_CAUGHT, bit);

    uint32_t section = (pk.gender() == 1 ? 1u : 0u) + (pk.isShiny() ? 2u : 0u);
    setBit(dex + DEX_SEEN + section * DEX_SECTION, bit);
    bool anyDisplayed = false;
    for (uint32_t s = 0; s < 4; s++) anyDisplayed |= getBit(dex + DEX_DISPLAYED + s * DEX_SECTION, bit);
    if (!anyDisplayed) setBit(dex + DEX_DISPLAYED + section * DEX_SECTION, bit);

    int li = langIndex7(pk.language());
    uint32_t lbit = bit * 9 + uint32_t(li);
    if (li >= 0 && lbit < DEX_LANG_BYTES * 8) setBit(dex + DEX_LANG, lbit);
}

} // namespace pw3d
