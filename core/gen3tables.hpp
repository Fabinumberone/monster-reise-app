// Tabellen für Gen 3 (erzeugt von tools/gen-gen3tables.mjs, siehe gen3tables.cpp).
#pragma once
#include <cstdint>

namespace pw3d {

extern const uint16_t kGen3NationalToInternal[387]; // Pokédex-Nummer -> interne Gen-3-Nummer
extern const uint16_t kGen3InternalToNational[];    // interne Nummer -> Pokédex-Nummer (0 = ungültig)
extern const int kGen3InternalCount;
extern const uint16_t kGen3ItemToModern[];          // Gen-3-Item -> heutige Nummer (0 = keine)
extern const int kGen3ItemCount;
extern const char* const kGen3Chars[256];           // Zeichensatz (international/Deutsch), UTF-8; "" = Ende/ungültig

} // namespace pw3d
