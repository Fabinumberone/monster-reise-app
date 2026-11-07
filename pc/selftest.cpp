// Selbsttest ohne Spielstand: Prüfsummen-Testvektoren und PK6-Rundlauf.
#include <cstdio>
#include <cstring>
#include <random>

#include <string>

#include "../core/crc16.hpp"
#include "../core/crypto.hpp"
#include "../core/pk6.hpp"

using namespace pw3d;

int runSelftest() {
    int fails = 0;
    const char* v = "123456789";
    uint16_t a = crc16Ccitt((const uint8_t*)v, 9), b = crc16Invert((const uint8_t*)v, 9);
    std::printf("CRC-16/CCITT-FALSE: %04X (erwartet 29B1) %s\n", a, a == 0x29B1 ? "ok" : "FALSCH");
    std::printf("CRC-16/USB:         %04X (erwartet B4C8) %s\n", b, b == 0xB4C8 ? "ok" : "FALSCH");
    fails += (a != 0x29B1) + (b != 0xB4C8);

    auto hex = [](const uint8_t* p, size_t n) {
        std::string s;
        char b[3];
        for (size_t i = 0; i < n; i++) { std::snprintf(b, sizeof b, "%02x", p[i]); s += b; }
        return s;
    };
    auto check = [&](const char* name, const std::string& got, const char* want) {
        bool ok = got == want;
        std::printf("%-20s %s\n", name, ok ? "ok" : ("FALSCH: " + got).c_str());
        fails += !ok;
    };
    const uint8_t* abc = (const uint8_t*)"abc";
    check("SHA-1(abc)", hex(sha1(abc, 3).data(), 20), "a9993e364706816aba3e25717850c26c9cd0d89d");
    check("SHA-256(abc)", hex(sha256(abc, 3).data(), 32), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    uint8_t key[16], blk[16];
    for (int i = 0; i < 16; i++) { key[i] = uint8_t(i); blk[i] = uint8_t(i * 0x11); }
    Aes128 aes(key);
    aes.encryptBlock(blk);
    check("AES-128 (FIPS-197)", hex(blk, 16), "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes.decryptBlock(blk);
    check("AES-128 zurück", hex(blk, 16), "00112233445566778899aabbccddeeff");
    uint8_t b5[2] = {0, 5}, e3[2] = {0, 117}, m[2] = {0x00, 0x13}, r[2]; // 5^117 mod 19 = 1
    modPow(b5, e3, m, r, 2);
    check("modPow", hex(r, 2), "0001");

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
