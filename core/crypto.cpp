#include "crypto.hpp"

#include <cstring>
#include <vector>

namespace pw3d {

// ---------- SHA-1 ----------
namespace {

inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t buf[64];
    size_t used = 0;
    uint64_t total = 0;

    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void update(const uint8_t* p, size_t n) {
        total += n;
        while (n) {
            size_t take = 64 - used < n ? 64 - used : n;
            std::memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    std::array<uint8_t, 20> finish() {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80, zero = 0;
        update(&pad, 1);
        while (used != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - 8 * i));
        update(len, 8);
        std::array<uint8_t, 20> out;
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 4; j++) out[i * 4 + j] = uint8_t(h[i] >> (24 - 8 * j));
        return out;
    }
};

const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

} // namespace

std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len) {
    Sha1 s;
    s.update(data, len);
    return s.finish();
}

std::array<uint8_t, 20> sha1(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
    Sha1 s;
    s.update(a, alen);
    s.update(b, blen);
    return s.finish();
}

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> m(data, data + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    uint64_t bits = uint64_t(len) * 8;
    for (int i = 0; i < 8; i++) m.push_back(uint8_t(bits >> (56 - 8 * i)));
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = uint32_t(m[o + i * 4]) << 24 | uint32_t(m[o + i * 4 + 1]) << 16 | uint32_t(m[o + i * 4 + 2]) << 8 | m[o + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = uint8_t(h[i] >> (24 - 8 * j));
    return out;
}

// ---------- AES-128 ----------
namespace {

uint8_t SBOX[256], INV_SBOX[256];
bool sboxReady = false;

uint8_t xtime(uint8_t x) { return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; }
    return r;
}

void initSbox() {
    if (sboxReady) return;
    // S-Box aus multiplikativem Inversen + affiner Abbildung berechnen.
    for (int i = 0; i < 256; i++) {
        uint8_t inv = 0;
        if (i) for (int j = 1; j < 256; j++) if (gmul(uint8_t(i), uint8_t(j)) == 1) { inv = uint8_t(j); break; }
        uint8_t s = inv, x = inv;
        for (int k = 0; k < 4; k++) { x = uint8_t((x << 1) | (x >> 7)); s ^= x; }
        s ^= 0x63;
        SBOX[i] = s;
        INV_SBOX[s] = uint8_t(i);
    }
    sboxReady = true;
}

} // namespace

Aes128::Aes128(const uint8_t key[16]) {
    initSbox();
    std::memcpy(rk_, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4] = {rk_[i - 4], rk_[i - 3], rk_[i - 2], rk_[i - 1]};
        if (i % 16 == 0) {
            uint8_t u = t[0];
            t[0] = uint8_t(SBOX[t[1]] ^ rcon); t[1] = SBOX[t[2]]; t[2] = SBOX[t[3]]; t[3] = SBOX[u];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; j++) rk_[i + j] = uint8_t(rk_[i - 16 + j] ^ t[j]);
    }
}

void Aes128::encryptBlock(uint8_t s[16]) const {
    for (int i = 0; i < 16; i++) s[i] ^= rk_[i];
    for (int r = 1; r <= 10; r++) {
        for (int i = 0; i < 16; i++) s[i] = SBOX[s[i]];
        uint8_t t[16];
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) t[c * 4 + rr] = s[((c + rr) % 4) * 4 + rr];
        if (r != 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t* col = t + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = uint8_t(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
                col[1] = uint8_t(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
                col[2] = uint8_t(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
                col[3] = uint8_t(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
            }
        }
        for (int i = 0; i < 16; i++) s[i] = uint8_t(t[i] ^ rk_[r * 16 + i]);
    }
}

void Aes128::decryptBlock(uint8_t s[16]) const {
    for (int i = 0; i < 16; i++) s[i] ^= rk_[160 + i];
    for (int r = 9; r >= 0; r--) {
        uint8_t t[16];
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) t[((c + rr) % 4) * 4 + rr] = s[c * 4 + rr];
        for (int i = 0; i < 16; i++) t[i] = uint8_t(INV_SBOX[t[i]] ^ rk_[r * 16 + i]);
        if (r != 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t* col = t + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = uint8_t(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
                col[1] = uint8_t(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
                col[2] = uint8_t(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
                col[3] = uint8_t(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
            }
        }
        std::memcpy(s, t, 16);
    }
}

// ---------- Modulare Exponentiation (einfach, ohne Montgomery) ----------
namespace {

constexpr int LIMBS = 34; // 34 × 32 Bit = 1088 Bit, reicht für 2·N bei N ≤ 1024 Bit

struct Big {
    uint32_t w[LIMBS] = {}; // w[0] niederwertig
};

Big fromBytes(const uint8_t* p, size_t n) {
    Big b;
    for (size_t i = 0; i < n; i++) {
        size_t bit = (n - 1 - i) * 8;
        b.w[bit / 32] |= uint32_t(p[i]) << (bit % 32);
    }
    return b;
}

void toBytes(const Big& b, uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t bit = (n - 1 - i) * 8;
        p[i] = uint8_t(b.w[bit / 32] >> (bit % 32));
    }
}

int cmp(const Big& a, const Big& b) {
    for (int i = LIMBS - 1; i >= 0; i--)
        if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1;
    return 0;
}

void sub(Big& a, const Big& b) {
    uint64_t borrow = 0;
    for (int i = 0; i < LIMBS; i++) {
        uint64_t d = uint64_t(a.w[i]) - b.w[i] - borrow;
        a.w[i] = uint32_t(d);
        borrow = (d >> 63) & 1;
    }
}

void add(Big& a, const Big& b) {
    uint64_t carry = 0;
    for (int i = 0; i < LIMBS; i++) {
        uint64_t s = uint64_t(a.w[i]) + b.w[i] + carry;
        a.w[i] = uint32_t(s);
        carry = s >> 32;
    }
}

void shl1(Big& a) {
    for (int i = LIMBS - 1; i > 0; i--) a.w[i] = (a.w[i] << 1) | (a.w[i - 1] >> 31);
    a.w[0] <<= 1;
}

int bitLen(const Big& a) {
    for (int i = LIMBS - 1; i >= 0; i--)
        if (a.w[i])
            for (int b = 31; b >= 0; b--)
                if (a.w[i] >> b & 1) return i * 32 + b + 1;
    return 0;
}

bool bit(const Big& a, int i) { return (a.w[i / 32] >> (i % 32)) & 1; }

// a mod m durch schrittweises Abziehen der verschobenen Zahl (nur einmalig für die Basis).
Big modReduce(Big a, const Big& m) {
    Big r;
    for (int i = bitLen(a) - 1; i >= 0; i--) {
        shl1(r);
        if (bit(a, i)) r.w[0] |= 1;
        if (cmp(r, m) >= 0) sub(r, m);
    }
    return r;
}

// (a·b) mod m mit Verdoppeln und Addieren; a, b < m.
Big mulMod(const Big& a, const Big& b, const Big& m) {
    Big r;
    for (int i = bitLen(b) - 1; i >= 0; i--) {
        shl1(r);
        if (cmp(r, m) >= 0) sub(r, m);
        if (bit(b, i)) {
            add(r, a);
            if (cmp(r, m) >= 0) sub(r, m);
        }
    }
    return r;
}

} // namespace

void modPow(const uint8_t* base, const uint8_t* exp, const uint8_t* mod, uint8_t* out, size_t n) {
    Big m = fromBytes(mod, n), e = fromBytes(exp, n);
    Big b = modReduce(fromBytes(base, n), m);
    Big r;
    r.w[0] = 1;
    for (int i = bitLen(e) - 1; i >= 0; i--) {
        r = mulMod(r, r, m);
        if (bit(e, i)) r = mulMod(r, b, m);
    }
    toBytes(r, out, n);
}

} // namespace pw3d
