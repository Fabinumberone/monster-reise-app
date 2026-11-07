// Pokémon-Datensatz der 6. Generation (XY, ORAS).
// Box: 0xE8 Byte, Team: 0x104 Byte (zusätzlich Status, Level, aktuelle Werte).
// Gespeichert verschlüsselt: Blöcke A–D (je 56 Byte ab 0x08) vertauscht und mit LCRNG verXORt.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pw3d {

constexpr size_t PK6_BOX_SIZE = 0xE8;
constexpr size_t PK6_PARTY_SIZE = 0x104;
constexpr uint16_t GEN6_MAX_SPECIES = 721;

struct PK6 {
    std::array<uint8_t, PK6_PARTY_SIZE> d{}; // immer entschlüsselt, Blöcke in Reihenfolge A–D

    // Entschlüsselt len Byte (0xE8 oder 0x104).
    static PK6 decrypt(const uint8_t* enc, size_t len);
    // Verschlüsselt nach out (len Byte). Prüfsumme wird vorher neu berechnet.
    void encrypt(uint8_t* out, size_t len);

    uint16_t computeChecksum() const;
    bool checksumValid() const { return computeChecksum() == storedChecksum(); }
    void updateChecksum();

    // Plausibel belegter Platz: Prüfsumme stimmt und Art im gültigen Bereich.
    bool looksValid() const;

    uint32_t ec() const;
    uint16_t storedChecksum() const;
    uint16_t species() const;
    uint16_t heldItem() const;
    uint16_t tid() const;
    uint16_t sid() const;
    uint32_t exp() const;
    uint8_t ability() const;
    uint32_t pid() const;
    uint8_t form() const;
    std::array<uint16_t, 4> moves() const;
    uint32_t iv32() const;
    bool isEgg() const { return (iv32() >> 30) & 1; }
    bool isNicknamed() const { return (iv32() >> 31) & 1; }
    std::string nickname() const;
    std::string otName() const;
    uint8_t partyLevel() const { return d[0xEC]; } // nur im Team gültig

    void setTid(uint16_t v);
    void setSid(uint16_t v);
    void setOtName(const std::string& s);
};

// Datensatz ohne Inhalt (nur Nullen)?
bool isEmptySlot(const uint8_t* enc, size_t len);

} // namespace pw3d
