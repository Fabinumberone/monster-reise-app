#include "memecrypto.hpp"

#include <cstring>

#include "crypto.hpp"

namespace pw3d {

namespace {

// Öffentlicher Schlüssel „PokedexAndSaveFile“ (DER) und privater Exponent, aus PKHeX MemeKey.cs.
const uint8_t DER[0x7E] = {
    0x30, 0x7C, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x6B, 0x00,
    0x30, 0x68, 0x02, 0x61, 0x00, 0xB6, 0x1E, 0x19, 0x20, 0x91, 0xF9, 0x0A, 0x8F, 0x76, 0xA6, 0xEA, 0xAA, 0x9A, 0x3C, 0xE5,
    0x8C, 0x86, 0x3F, 0x39, 0xAE, 0x25, 0x3F, 0x03, 0x78, 0x16, 0xF5, 0x97, 0x58, 0x54, 0xE0, 0x7A, 0x9A, 0x45, 0x66, 0x01,
    0xE7, 0xC9, 0x4C, 0x29, 0x75, 0x9F, 0xE1, 0x55, 0xC0, 0x64, 0xED, 0xDF, 0xA1, 0x11, 0x44, 0x3F, 0x81, 0xEF, 0x1A, 0x42,
    0x8C, 0xF6, 0xCD, 0x32, 0xF9, 0xDA, 0xC9, 0xD4, 0x8E, 0x94, 0xCF, 0xB3, 0xF6, 0x90, 0x12, 0x0E, 0x8E, 0x6B, 0x91, 0x11,
    0xAD, 0xDA, 0xF1, 0x1E, 0x7C, 0x96, 0x20, 0x8C, 0x37, 0xC0, 0x14, 0x3F, 0xF2, 0xBF, 0x3D, 0x7E, 0x83, 0x11, 0x41, 0xA9,
    0x73, 0x02, 0x03, 0x01, 0x00, 0x01};

const uint8_t PRIV_D[0x61] = {
    0x00, 0x77, 0x54, 0x55, 0x66, 0x8F, 0xFF, 0x3C, 0xBA, 0x30, 0x26, 0xC2, 0xD0, 0xB2, 0x6B, 0x80, 0x85, 0x89, 0x59, 0x58,
    0x34, 0x11, 0x57, 0xAE, 0xB0, 0x3B, 0x6B, 0x04, 0x95, 0xEE, 0x57, 0x80, 0x3E, 0x21, 0x86, 0xEB, 0x6C, 0xB2, 0xEB, 0x62,
    0xA7, 0x1D, 0xF1, 0x8A, 0x3C, 0x9C, 0x65, 0x79, 0x07, 0x76, 0x70, 0x96, 0x1B, 0x3A, 0x61, 0x02, 0xDA, 0xBE, 0x5A, 0x19,
    0x4A, 0xB5, 0x8C, 0x32, 0x50, 0xAE, 0xD5, 0x97, 0xFC, 0x78, 0x97, 0x8A, 0x32, 0x6D, 0xB1, 0xD7, 0xB2, 0x8D, 0xCC, 0xCB,
    0x2A, 0x3E, 0x01, 0x4E, 0xDB, 0xD3, 0x97, 0xAD, 0x33, 0xB8, 0xF2, 0x8C, 0xD5, 0x25, 0x05, 0x42, 0x51};

constexpr size_t REGION = 0x80, SIG = 0x60, PAY = REGION - SIG, CH = 16;
constexpr size_t NLEN = 0x61;               // Modulus 0x61 Byte (mit führender 0)
const uint8_t* MODULUS = DER + 0x18;        // 0x61 Byte ab 0x18

struct Where {
    size_t region, table, tableLen;
};

bool locate(size_t size, Where& w) {
    if (size == 0x6CC00) { w = {0x6C000 + 0x100, size - 0x200, 0x150}; return true; }
    if (size == 0x6BE00) { w = {0x6BA00 + 0x100, size - 0x200, 0x140}; return true; }
    return false;
}

void xorTo(uint8_t* a, const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) a[i] ^= b[i]; }

void subKey(const uint8_t t[16], uint8_t s[16]) {
    for (int i = 0; i < 16; i += 2) {
        s[i] = uint8_t(2 * t[i] + (t[i + 1] >> 7));
        s[i + 1] = uint8_t(2 * t[i + 1]);
        if (i + 2 < 16) s[i + 1] = uint8_t(s[i + 1] + (t[i + 2] >> 7));
    }
    if (t[0] & 0x80) s[15] ^= 0x87;
}

Aes128 aesFor(const uint8_t* payload) {
    auto h = sha1(DER, sizeof DER, payload, PAY);
    return Aes128(h.data());
}

void aesEncrypt(uint8_t* r) {
    Aes128 aes = aesFor(r);
    uint8_t* sig = r + PAY;
    uint8_t temp[16] = {}, nx[16];
    for (size_t i = 0; i < SIG; i += CH) {
        xorTo(sig + i, temp, CH);
        aes.encryptBlock(sig + i);
        std::memcpy(temp, sig + i, CH);
    }
    xorTo(temp, sig, CH);
    subKey(temp, nx);
    for (size_t i = 0; i < SIG; i += CH) xorTo(sig + i, nx, CH);
    std::memset(temp, 0, CH);
    for (int i = int(SIG - CH); i >= 0; i -= int(CH)) {
        uint8_t keep[16];
        std::memcpy(keep, sig + i, CH);
        aes.encryptBlock(sig + i);
        xorTo(sig + i, temp, CH);
        std::memcpy(temp, keep, CH);
    }
}

void aesDecrypt(uint8_t* r) {
    Aes128 aes = aesFor(r);
    uint8_t* sig = r + PAY;
    uint8_t temp[16] = {}, nx[16];
    for (int i = int(SIG - CH); i >= 0; i -= int(CH)) {
        uint8_t* s = sig + i;
        xorTo(temp, s, CH);
        aes.decryptBlock(temp);
        std::memcpy(s, temp, CH);
    }
    xorTo(temp, sig + SIG - CH, CH);
    subKey(temp, nx);
    for (size_t i = 0; i < SIG; i += CH) xorTo(sig + i, nx, CH);
    uint8_t prev[16] = {};
    for (size_t i = 0; i < SIG; i += CH) {
        uint8_t keep[16];
        std::memcpy(keep, sig + i, CH);
        aes.decryptBlock(sig + i);
        xorTo(sig + i, prev, CH);
        std::memcpy(prev, keep, CH);
    }
}

// RSA auf 0x60 Byte (big-endian), Modulus hat 0x61 Byte.
void rsa(const uint8_t* in, const uint8_t* exp61, uint8_t* out) {
    uint8_t b[NLEN] = {}, r[NLEN];
    std::memcpy(b + 1, in, SIG);
    modPow(b, exp61, MODULUS, r, NLEN);
    std::memcpy(out, r + 1, SIG);
}

bool sha1TailOk(const uint8_t* r) {
    auto h = sha1(r, REGION - 8);
    return std::memcmp(h.data(), r + REGION - 8, 8) == 0;
}

} // namespace

bool signSave7(std::vector<uint8_t>& sav) {
    Where w;
    if (!locate(sav.size(), w)) return false;
    uint8_t* r = &sav[w.region];
    std::memset(r, 0, REGION);
    auto h = sha256(&sav[w.table], w.tableLen);
    std::memcpy(r, h.data(), 32);
    auto t = sha1(r, REGION - 8);
    std::memcpy(r + REGION - 8, t.data(), 8);
    aesEncrypt(r);
    r[PAY] &= 0x7F;
    rsa(r + PAY, PRIV_D, r + PAY);
    return true;
}

bool verifySave7(const std::vector<uint8_t>& sav) {
    Where w;
    if (!locate(sav.size(), w)) return false;
    uint8_t e[NLEN] = {};
    e[NLEN - 3] = 0x01; e[NLEN - 2] = 0x00; e[NLEN - 1] = 0x01; // 65537
    uint8_t dec[SIG];
    rsa(&sav[w.region + PAY], e, dec);
    auto want = sha256(&sav[w.table], w.tableLen);
    for (uint8_t flip : {uint8_t(0), uint8_t(0x80)}) {
        uint8_t r[REGION];
        std::memcpy(r, &sav[w.region], REGION);
        std::memcpy(r + PAY, dec, SIG);
        r[PAY] |= flip;
        aesDecrypt(r);
        if (sha1TailOk(r) && std::memcmp(r, want.data(), 32) == 0) return true;
    }
    return false;
}

} // namespace pw3d
