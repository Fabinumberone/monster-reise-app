// Prüfsummen der 3DS-Pokémon-Spielstände.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pw3d {

// CRC-16/CCITT-FALSE (Polynom 0x1021, Start 0xFFFF, nicht gespiegelt). Gen 6 (XY, ORAS).
uint16_t crc16Ccitt(const uint8_t* data, size_t len);

// Gespiegeltes CRC-16 (Polynom 0xA001, Start 0xFFFF, Ergebnis invertiert). Vermutlich Gen 7.
uint16_t crc16Invert(const uint8_t* data, size_t len);

} // namespace pw3d
