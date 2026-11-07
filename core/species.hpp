// Artdaten aus arten.bin (siehe tools/gen-arten.mjs) und Berechnungen, die sie brauchen:
// Erfahrungskurven, Werte in Gen 1/2 und Gen 7.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pw3d {

struct SpeciesInfo {
    uint8_t growth = 0;      // 0 mittel-schnell, 1 unregelmäßig, 2 schwankend, 3 mittel-langsam, 4 schnell, 5 langsam
    uint8_t gender = 0;      // 0 nur ♂ … 254 nur ♀, 255 geschlechtslos
    std::array<uint8_t, 3> ability{};
    uint8_t friendship = 70;
    std::array<uint8_t, 5> base1{}; // Gen 1: KP, Ang, Ver, Init, Spez
    uint8_t type1 = 0, type2 = 0, catchRate = 0; // Gen 1
    std::array<uint8_t, 6> base2{}; // Gen 2: KP, Ang, Ver, Init, SpAng, SpVer
    std::array<uint8_t, 6> base7{}; // Gen 7: KP, Ang, Ver, Init, SpAng, SpVer
};

class SpeciesData {
public:
    bool load(const std::string& path);
    bool loaded() const { return !info_.empty(); }
    const SpeciesInfo& get(uint16_t s) const { return s < info_.size() ? info_[s] : empty_; }

private:
    std::vector<SpeciesInfo> info_;
    SpeciesInfo empty_;
};

uint32_t expForLevel(uint8_t growth, int level);
uint8_t levelForExp(uint8_t growth, uint32_t exp); // 1..100

// Gen-1/2-Werte. dv: KP aus den anderen DVs, statExp wie gespeichert.
uint16_t statGB(uint8_t base, uint8_t dv, uint16_t statExp, int level, bool hp);

// Gen-7-Wert. natureIndex: 0..24; statIndex: 1 Ang, 2 Ver, 3 Init, 4 SpAng, 5 SpVer (0 = KP).
uint16_t stat7(uint8_t base, uint8_t iv, uint8_t ev, int level, int statIndex, uint8_t nature);

} // namespace pw3d
