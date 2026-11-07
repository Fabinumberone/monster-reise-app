#include "species.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace pw3d {

bool SpeciesData::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 16 || std::memcmp(d.data(), "PW3DARTN", 8) != 0) return false;
    uint16_t count = uint16_t(d[12] | (d[13] << 8)), rec = uint16_t(d[14] | (d[15] << 8));
    if (rec < 26 || d.size() < 16 + size_t(count + 1) * rec) return false;
    info_.assign(count + 1, SpeciesInfo{});
    for (uint16_t s = 0; s <= count; s++) {
        const uint8_t* p = &d[16 + size_t(s) * rec];
        SpeciesInfo& i = info_[s];
        i.growth = p[0];
        i.gender = p[1];
        i.ability = {p[2], p[3], p[4]};
        i.friendship = p[5];
        std::memcpy(i.base1.data(), p + 6, 5);
        i.type1 = p[11];
        i.type2 = p[12];
        i.catchRate = p[13];
        std::memcpy(i.base2.data(), p + 14, 6);
        std::memcpy(i.base7.data(), p + 20, 6);
    }
    return true;
}

uint32_t expForLevel(uint8_t growth, int n) {
    if (n <= 1) return 0;
    long long n3 = (long long)n * n * n;
    switch (growth) {
    case 0: return uint32_t(n3);                                  // mittel-schnell
    case 1:                                                       // unregelmäßig
        if (n <= 50) return uint32_t(n3 * (100 - n) / 50);
        if (n <= 68) return uint32_t(n3 * (150 - n) / 100);
        if (n <= 98) return uint32_t(n3 * ((1911 - 10 * n) / 3) / 500);
        return uint32_t(n3 * (160 - n) / 100);
    case 2:                                                       // schwankend
        if (n <= 15) return uint32_t(n3 * ((n + 1) / 3 + 24) / 50);
        if (n <= 36) return uint32_t(n3 * (n + 14) / 50);
        return uint32_t(n3 * (n / 2 + 32) / 50);
    case 3: {                                                     // mittel-langsam
        long long v = 6 * n3 / 5 - 15LL * n * n + 100LL * n - 140;
        return uint32_t(v < 0 ? 0 : v);
    }
    case 4: return uint32_t(4 * n3 / 5);                          // schnell
    case 5: return uint32_t(5 * n3 / 4);                          // langsam
    default: return uint32_t(n3);
    }
}

uint8_t levelForExp(uint8_t growth, uint32_t exp) {
    int l = 1;
    while (l < 100 && expForLevel(growth, l + 1) <= exp) l++;
    return uint8_t(l);
}

uint16_t statGB(uint8_t base, uint8_t dv, uint16_t statExp, int level, bool hp) {
    // floor(((Base + DV) * 2 + floor(ceil(sqrt(StatExp)) / 4)) * L / 100) + 5 (KP: + L + 10)
    int root = int(std::ceil(std::sqrt(double(statExp))));
    if (root > 255) root = 255;
    int v = ((base + dv) * 2 + root / 4) * level / 100;
    return uint16_t(hp ? v + level + 10 : v + 5);
}

uint16_t stat7(uint8_t base, uint8_t iv, uint8_t ev, int level, int statIndex, uint8_t nature) {
    int v = (2 * base + iv + ev / 4) * level / 100;
    if (statIndex == 0) return uint16_t(base == 1 ? 1 : v + level + 10); // Ninjatom: immer 1 KP
    v += 5;
    // Wesen: erhöhter Wert = nature / 5, gesenkter = nature % 5 (Reihenfolge Ang, Ver, Init, SpAng, SpVer)
    int up = nature / 5 + 1, down = nature % 5 + 1;
    if (up != down) {
        if (statIndex == up) v = v * 11 / 10;
        if (statIndex == down) v = v * 9 / 10;
    }
    return uint16_t(v);
}

} // namespace pw3d
