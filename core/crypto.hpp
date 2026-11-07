// Kleine, abhängigkeitsfreie Kryptografie-Bausteine (auch für den 3DS).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace pw3d {

std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len);
// Zwei Teile hintereinander gehasht (für den AES-Schlüssel aus DER + Daten).
std::array<uint8_t, 20> sha1(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen);
std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len);

class Aes128 {
public:
    explicit Aes128(const uint8_t key[16]);
    void encryptBlock(uint8_t b[16]) const;
    void decryptBlock(uint8_t b[16]) const;

private:
    uint8_t rk_[176];
};

// out = base^exp mod mod, alle Zahlen big-endian mit n Byte (n ≤ 128).
void modPow(const uint8_t* base, const uint8_t* exp, const uint8_t* mod, uint8_t* out, size_t n);

} // namespace pw3d
