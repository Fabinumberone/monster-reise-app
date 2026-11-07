#include "save6.hpp"

#include "bytes.hpp"
#include "crc16.hpp"

namespace pw3d {

namespace {

constexpr uint32_t BEEF = 0x42454546;

// An echten Spielständen bestätigt (Box-Offset ORAS noch offen).
constexpr Layout6 LAYOUT_XY{0x65600, 0x14000, 0x14200, 0x22600, 31, 0x15000, 0x3C8};
constexpr Layout6 LAYOUT_ORAS{0x76000, 0x14000, 0x14200, 0x33000, 31, 0x15000, 0x400};

// Pokédex-Bitfelder (Bit species-1), relativ zum Dex-Block.
constexpr uint32_t DEX_CAUGHT = 0x008;
constexpr uint32_t DEX_SEEN = 0x068;      // 4 × 0x60: männlich, weiblich, männlich schillernd, weiblich schillernd
constexpr uint32_t DEX_DISPLAYED = 0x1E8; // gleiche 4 Abschnitte: welche Variante im Pokédex gezeigt wird
constexpr uint32_t DEX_SECTION = 0x60;
constexpr uint32_t DEX_FOREIGN_XY = 0x64C; // XY: Arten bis 649 aus Spielen vor XY

// Sprache im Datensatz -> Index der Sprachbits.
int langIndex(uint8_t lang) {
    switch (lang) {
    case 1: return 0; case 2: return 1; case 3: return 2; case 4: return 3;
    case 5: return 4; case 7: return 5; case 8: return 6;
    default: return -1;
    }
}

} // namespace

const char* gameName(Game6 g) {
    switch (g) {
    case Game6::XY: return "X/Y";
    case Game6::ORAS: return "Omega Rubin/Alpha Saphir";
    default: return "unbekannt";
    }
}

bool Save6::load(std::vector<uint8_t> data, std::string& err) {
    data_ = std::move(data);
    if (data_.size() == LAYOUT_XY.size) { game_ = Game6::XY; layout_ = LAYOUT_XY; }
    else if (data_.size() == LAYOUT_ORAS.size) { game_ = Game6::ORAS; layout_ = LAYOUT_ORAS; }
    else {
        err = "Unbekannte Größe " + std::to_string(data_.size()) + " Byte (erwartet 0x65600 für XY oder 0x76000 für ORAS)";
        return false;
    }
    return readBlockTable(err);
}

bool Save6::readBlockTable(std::string& err) {
    blocks_.clear();
    const uint32_t footer = uint32_t(data_.size()) - 0x200;
    // Kennung im Fußbereich suchen, statt eine feste Position anzunehmen.
    uint32_t magicAt = 0;
    for (uint32_t p = footer; p + 4 <= data_.size(); p += 4) {
        if (rd32(&data_[p]) == BEEF) { magicAt = p; break; }
    }
    if (!magicAt) { err = "Blocktabelle nicht gefunden (Kennung BEEF fehlt)"; return false; }

    uint32_t offset = 0;
    for (uint32_t p = magicAt + 4; p + 8 <= data_.size(); p += 8) {
        Block6 b;
        b.length = rd32(&data_[p]);
        b.id = rd16(&data_[p + 4]);
        b.storedCrc = rd16(&data_[p + 6]);
        if (b.length == 0 || offset + b.length > data_.size()) break;
        b.offset = offset;
        b.computedCrc = crc16Ccitt(&data_[b.offset], b.length);
        blocks_.push_back(b);
        offset += b.length;
        if (offset & 0x1FF) offset += 0x200 - (offset & 0x1FF);
        if (b.offset >= footer) break; // letzter Eintrag beschreibt die Tabelle selbst
    }
    if (blocks_.empty()) { err = "Blocktabelle ist leer"; return false; }
    return true;
}

bool Save6::checksumsValid() const {
    for (auto& b : blocks_)
        if (!b.ok()) return false;
    return true;
}

void Save6::updateChecksums() {
    // Die Tabelle liegt im letzten Block und enthält die Prüfsummen der anderen,
    // daher zuerst alle anderen Einträge schreiben, dann den eigenen.
    uint32_t magicAt = 0;
    for (uint32_t p = uint32_t(data_.size()) - 0x200; p + 4 <= data_.size(); p += 4)
        if (rd32(&data_[p]) == BEEF) { magicAt = p; break; }
    for (size_t i = 0; i < blocks_.size(); i++) {
        auto& b = blocks_[i];
        b.computedCrc = crc16Ccitt(&data_[b.offset], b.length);
        b.storedCrc = b.computedCrc;
        wr16(&data_[magicAt + 4 + i * 8 + 6], b.storedCrc);
    }
}

Trainer6 Save6::trainer() const {
    const uint8_t* p = &data_[layout_.trainer];
    Trainer6 t;
    t.tid = rd16(p + 0x00);
    t.sid = rd16(p + 0x02);
    t.gender = p[0x05];
    t.name = utf16ToUtf8(p + 0x48, 13);
    return t;
}

void Save6::setTrainer(const Trainer6& t) {
    uint8_t* p = &data_[layout_.trainer];
    wr16(p + 0x00, t.tid);
    wr16(p + 0x02, t.sid);
    p[0x05] = t.gender;
    utf8ToUtf16(t.name, p + 0x48, 13);
}

int Save6::partyCount() const { return data_[layout_.party + 6 * PK6_PARTY_SIZE]; }

PK6 Save6::partySlot(int i) const { return PK6::decrypt(&data_[layout_.party + i * PK6_PARTY_SIZE], PK6_PARTY_SIZE); }

void Save6::setPartySlot(int i, PK6 pk) { pk.encrypt(&data_[layout_.party + i * PK6_PARTY_SIZE], PK6_PARTY_SIZE); }

uint32_t Save6::boxOffset(int box, int slot) const {
    return layout_.box + uint32_t(box * BOX_SLOTS + slot) * PK6_BOX_SIZE;
}

// Leere Plätze sind im Spiel verschlüsselte Leerdatensätze (Art 0), nicht nur Nullen.
bool Save6::boxSlotEmpty(int box, int slot) const {
    const uint8_t* p = &data_[boxOffset(box, slot)];
    return isEmptySlot(p, PK6_BOX_SIZE) || PK6::decrypt(p, PK6_BOX_SIZE).species() == 0;
}

PK6 Save6::boxSlot(int box, int slot) const { return PK6::decrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

void Save6::setBoxSlot(int box, int slot, PK6 pk) { pk.encrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

namespace {
void setBit(uint8_t* base, uint32_t bit) { base[bit >> 3] |= uint8_t(1u << (bit & 7)); }
bool getBit(const uint8_t* base, uint32_t bit) { return (base[bit >> 3] >> (bit & 7)) & 1; }
} // namespace

bool Save6::dexCaught(uint16_t species) const {
    const uint8_t* dex = &data_[layout_.dex];
    return getBit(dex + DEX_CAUGHT, species - 1u) || (game_ == Game6::XY && getBit(dex + DEX_FOREIGN_XY, species - 1u));
}

void Save6::registerDex(const PK6& pk) {
    uint16_t sp = pk.species();
    if (sp == 0 || sp > GEN6_MAX_SPECIES || pk.isEgg()) return;
    uint8_t* dex = &data_[layout_.dex];
    uint32_t bit = sp - 1u;

    // XY führt Arten bis 649 aus älteren Spielen (vor XY) als „fremd“.
    if (game_ == Game6::XY && pk.version() < 24 && sp <= 649) setBit(dex + DEX_FOREIGN_XY, bit);
    else setBit(dex + DEX_CAUGHT, bit);

    uint32_t section = (pk.gender() == 1 ? 1u : 0u) + (pk.isShiny() ? 2u : 0u);
    setBit(dex + DEX_SEEN + section * DEX_SECTION, bit);

    bool anyDisplayed = false;
    for (uint32_t s = 0; s < 4; s++) anyDisplayed |= getBit(dex + DEX_DISPLAYED + s * DEX_SECTION, bit);
    if (!anyDisplayed) setBit(dex + DEX_DISPLAYED + section * DEX_SECTION, bit);

    int li = langIndex(pk.language());
    if (li >= 0) setBit(dex + layout_.dexLang, bit * 7 + uint32_t(li));
}

int Save6::adoptTrainer(const Trainer6& t) {
    Trainer6 old = trainer();
    auto isOld = [&](const PK6& pk) { return pk.tid() == old.tid && pk.sid() == old.sid && pk.otName() == old.name; };
    auto reown = [&](PK6& pk) { pk.setTid(t.tid); pk.setSid(t.sid); pk.setOtName(t.name); };
    int n = 0;
    for (int i = 0; i < partyCount() && i < 6; i++) {
        PK6 pk = partySlot(i);
        if (pk.looksValid() && isOld(pk)) { reown(pk); setPartySlot(i, pk); n++; }
    }
    for (int b = 0; b < boxCount(); b++)
        for (int s = 0; s < BOX_SLOTS; s++) {
            if (boxSlotEmpty(b, s)) continue;
            PK6 pk = boxSlot(b, s);
            if (pk.looksValid() && isOld(pk)) { reown(pk); setBoxSlot(b, s, pk); n++; }
        }
    Trainer6 nt = old;
    nt.tid = t.tid;
    nt.sid = t.sid;
    nt.name = t.name;
    nt.gender = t.gender;
    setTrainer(nt);
    return n;
}

} // namespace pw3d
