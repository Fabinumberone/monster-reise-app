#include "pk6.hpp"

#include <cstring>

#include "bytes.hpp"

namespace pw3d {

namespace {

constexpr size_t BLOCK = 56;

// Für Schlüssel sv: Block b (A=0 … D=3) liegt verschlüsselt an Position POS[sv][b].
constexpr uint8_t POS[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

void cryptRange(uint8_t* p, size_t len, uint32_t seed) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        seed = seed * 0x41C64E6Du + 0x00006073u;
        wr16(p + i, uint16_t(rd16(p + i) ^ (seed >> 16)));
    }
}

uint32_t shuffleKey(uint32_t ec) { return ((ec >> 13) & 31) % 24; }

} // namespace

PK6 PK6::decrypt(const uint8_t* enc, size_t len) {
    PK6 pk;
    std::memcpy(pk.d.data(), enc, len);
    uint32_t key = rd32(pk.d.data());
    cryptRange(pk.d.data() + 8, PK6_BOX_SIZE - 8, key);
    if (len > PK6_BOX_SIZE) cryptRange(pk.d.data() + PK6_BOX_SIZE, len - PK6_BOX_SIZE, key);

    uint8_t tmp[BLOCK * 4];
    std::memcpy(tmp, pk.d.data() + 8, sizeof tmp);
    uint32_t sv = shuffleKey(key);
    for (int b = 0; b < 4; b++) std::memcpy(pk.d.data() + 8 + b * BLOCK, tmp + POS[sv][b] * BLOCK, BLOCK);
    return pk;
}

void PK6::encrypt(uint8_t* out, size_t len) {
    updateChecksum();
    std::memcpy(out, d.data(), len);
    uint32_t key = ec();
    uint32_t sv = shuffleKey(key);
    for (int b = 0; b < 4; b++) std::memcpy(out + 8 + POS[sv][b] * BLOCK, d.data() + 8 + b * BLOCK, BLOCK);
    cryptRange(out + 8, PK6_BOX_SIZE - 8, key);
    if (len > PK6_BOX_SIZE) cryptRange(out + PK6_BOX_SIZE, len - PK6_BOX_SIZE, key);
}

uint16_t PK6::computeChecksum() const {
    uint16_t sum = 0;
    for (size_t i = 8; i < PK6_BOX_SIZE; i += 2) sum = uint16_t(sum + rd16(d.data() + i));
    return sum;
}

void PK6::updateChecksum() { wr16(d.data() + 6, computeChecksum()); }

bool PK6::looksValid() const {
    uint16_t s = species();
    return s != 0 && s <= GEN6_MAX_SPECIES && checksumValid();
}

uint32_t PK6::ec() const { return rd32(d.data() + 0x00); }
uint16_t PK6::storedChecksum() const { return rd16(d.data() + 0x06); }
uint16_t PK6::species() const { return rd16(d.data() + 0x08); }
uint16_t PK6::heldItem() const { return rd16(d.data() + 0x0A); }
uint16_t PK6::tid() const { return rd16(d.data() + 0x0C); }
uint16_t PK6::sid() const { return rd16(d.data() + 0x0E); }
uint32_t PK6::exp() const { return rd32(d.data() + 0x10); }
uint8_t PK6::ability() const { return d[0x14]; }
uint32_t PK6::pid() const { return rd32(d.data() + 0x18); }
uint8_t PK6::form() const { return d[0x1D] >> 3; }
bool PK6::isShiny() const {
    uint32_t p = pid();
    return (tid() ^ sid() ^ (p >> 16) ^ (p & 0xFFFF)) < 16;
}
std::array<uint16_t, 4> PK6::moves() const {
    return {rd16(d.data() + 0x5A), rd16(d.data() + 0x5C), rd16(d.data() + 0x5E), rd16(d.data() + 0x60)};
}
uint32_t PK6::iv32() const { return rd32(d.data() + 0x74); }
std::string PK6::nickname() const { return utf16ToUtf8(d.data() + 0x40, 13); }
std::string PK6::otName() const { return utf16ToUtf8(d.data() + 0xB0, 13); }

void PK6::setTid(uint16_t v) { wr16(d.data() + 0x0C, v); }
void PK6::setSid(uint16_t v) { wr16(d.data() + 0x0E, v); }
void PK6::setOtName(const std::string& s) { utf8ToUtf16(s, d.data() + 0xB0, 13); }

bool isEmptySlot(const uint8_t* enc, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (enc[i]) return false;
    return true;
}

} // namespace pw3d
