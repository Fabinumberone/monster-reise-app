// Selbsttest ohne Spielstand: Prüfsummen-Testvektoren und PK6-Rundlauf.
#include <cstdio>
#include <cstring>
#include <random>

#include "../core/crc16.hpp"
#include "../core/pk6.hpp"

using namespace pw3d;

int runSelftest() {
    int fails = 0;
    const char* v = "123456789";
    uint16_t a = crc16Ccitt((const uint8_t*)v, 9), b = crc16Invert((const uint8_t*)v, 9);
    std::printf("CRC-16/CCITT-FALSE: %04X (erwartet 29B1) %s\n", a, a == 0x29B1 ? "ok" : "FALSCH");
    std::printf("CRC-16/USB:         %04X (erwartet B4C8) %s\n", b, b == 0xB4C8 ? "ok" : "FALSCH");
    fails += (a != 0x29B1) + (b != 0xB4C8);

    std::mt19937 rng(1234);
    int bad = 0;
    for (int n = 0; n < 2000; n++) {
        PK6 pk;
        for (auto& x : pk.d) x = uint8_t(rng());
        size_t len = (n & 1) ? PK6_PARTY_SIZE : PK6_BOX_SIZE;
        uint8_t enc[PK6_PARTY_SIZE];
        pk.encrypt(enc, len); // setzt auch die Prüfsumme
        PK6 back = PK6::decrypt(enc, len);
        if (std::memcmp(back.d.data(), pk.d.data(), len) || !back.checksumValid()) bad++;
    }
    std::printf("PK6-Rundlauf (2000 Zufallsdatensätze): %s\n", bad ? "FALSCH" : "ok");
    fails += bad != 0;
    return fails ? 1 : 0;
}
