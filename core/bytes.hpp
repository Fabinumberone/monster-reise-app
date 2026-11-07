// Little-Endian-Zugriffe und Zeichenketten (UTF-16LE der Spiele -> UTF-8).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace pw3d {

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline void wr16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void wr32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }

// Liest höchstens maxChars UTF-16-Zeichen bis zur ersten 0.
std::string utf16ToUtf8(const uint8_t* p, size_t maxChars);

// Schreibt höchstens maxChars-1 Zeichen und eine abschließende 0, Rest wird genullt.
void utf8ToUtf16(const std::string& s, uint8_t* p, size_t maxChars);

} // namespace pw3d
