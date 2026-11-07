#include "save7.hpp"

#include <vector>

#include "bytes.hpp"
#include "crc16.hpp"

namespace pw3d {

namespace {

constexpr uint32_t BEEF = 0x42454546;
constexpr uint16_t SIG_BLOCK = 0x24;
constexpr uint32_t SIG_OFS = 0x100, SIG_LEN = 0x80;

// USUM an Ultramond bestätigt. SM noch unbekannt (Sonne-Spielstand fehlt).
constexpr Layout7 LAYOUT_USUM{0x6CC00, 0x1400, 0x1600, 0x5200, 32, 0x4404};

} // namespace

const char* gameName(Game7 g) {
    switch (g) {
    case Game7::SM: return "Sonne/Mond";
    case Game7::USUM: return "Ultrasonne/Ultramond";
    default: return "unbekannt";
    }
}

bool Save7::load(std::vector<uint8_t> data, std::string& err) {
    data_ = std::move(data);
    if (data_.size() == LAYOUT_USUM.size) { game_ = Game7::USUM; layout_ = LAYOUT_USUM; }
    else if (data_.size() == 0x6BE00) { err = "Sonne/Mond wird noch nicht unterstützt"; return false; }
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
    if (b.id != SIG_BLOCK) return crc16Invert(&data_[b.offset], b.length);
    std::vector<uint8_t> tmp(data_.begin() + b.offset, data_.begin() + b.offset + b.length);
    for (uint32_t i = SIG_OFS; i < SIG_OFS + SIG_LEN && i < tmp.size(); i++) tmp[i] = 0;
    return crc16Invert(tmp.data(), tmp.size());
}

bool Save7::checksumsValid() const {
    for (auto& b : blocks_)
        if (!b.ok()) return false;
    return true;
}

void Save7::updateChecksums() {
    for (size_t i = 0; i < blocks_.size(); i++) {
        auto& b = blocks_[i];
        b.computedCrc = blockCrc(b);
        b.storedCrc = b.computedCrc;
        wr16(&data_[tableAt_ + i * 8 + 6], b.storedCrc);
    }
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

uint32_t Save7::money() const { return rd32(&data_[layout_.money]); }
void Save7::setMoney(uint32_t v) { wr32(&data_[layout_.money], v); }

int Save7::partyCount() const { return data_[layout_.party + 6 * PK6_PARTY_SIZE]; }
PK6 Save7::partySlot(int i) const { return PK6::decrypt(&data_[layout_.party + i * PK6_PARTY_SIZE], PK6_PARTY_SIZE); }

uint32_t Save7::boxOffset(int box, int slot) const {
    return layout_.box + uint32_t(box * BOX_SLOTS + slot) * PK6_BOX_SIZE;
}
bool Save7::boxSlotEmpty(int box, int slot) const {
    const uint8_t* p = &data_[boxOffset(box, slot)];
    return isEmptySlot(p, PK6_BOX_SIZE) || PK6::decrypt(p, PK6_BOX_SIZE).species() == 0;
}
PK6 Save7::boxSlot(int box, int slot) const { return PK6::decrypt(&data_[boxOffset(box, slot)], PK6_BOX_SIZE); }

} // namespace pw3d
