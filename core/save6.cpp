#include "save6.hpp"

#include "bytes.hpp"
#include "crc16.hpp"

namespace pw3d {

namespace {

constexpr uint32_t BEEF = 0x42454546;

// Offsets nach PKHeX (SAV6XY / SAV6AO). Noch nicht an echten Spielständen geprüft.
constexpr Layout6 LAYOUT_XY{0x65600, 0x14000, 0x14200, 0x22600, 31};
constexpr Layout6 LAYOUT_ORAS{0x76000, 0x14000, 0x14200, 0x33000, 31};

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

bool Save6::boxSlotEmpty(int box, int slot) const { return isEmptySlot(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

PK6 Save6::boxSlot(int box, int slot) const { return PK6::decrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

void Save6::setBoxSlot(int box, int slot, PK6 pk) { pk.encrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

} // namespace pw3d
