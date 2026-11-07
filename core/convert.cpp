#include "convert.hpp"

#include <cstring>

#include "bytes.hpp"

namespace pw3d {

PK6 toGen7(const PK6& in) {
    PK6 pk = in;
    uint8_t* d = pk.d.data();

    uint8_t oldMarks = d[0x2A];
    uint8_t formMax = d[0x3C], formRemain = d[0xED], formElapsed = d[0xEE];

    d[0x2A] = 0;                 // Gen 7: Resort-Ereignis (Gen 6: Markierungen)
    wr16(d + 0x16, 0);           // Gen 7: Markierungen (Gen 6: Supertraining-Beutel)
    d[0xDE] = 0;                 // Gen 7: Hypertraining (Gen 6: Begegnungstyp Gen 4)
    wr32(d + 0x3C, 0);           // Formargument neu aufgebaut
    d[0xED] = 0;                 // Gen 7: Schmutz-Art
    d[0xEE] = 0;                 // Gen 7: Schmutz-Ort

    uint16_t marks = 0;          // 2 Bit je Symbol, 1 = blau
    for (int i = 0; i < 6; i++)
        if (oldMarks >> i & 1) marks |= uint16_t(1u << (i * 2));
    wr16(d + 0x16, marks);

    std::memset(d + 0x94, 0, 0x9E - 0x94); // Geodaten
    std::memset(d + 0xAA, 0, 0xB0 - 0xAA); // ungenutzt / Ami-Sättigung
    std::memset(d + 0xE4, 0, 0xE8 - 0xE4); // ungenutzt
    d[0x72] &= 0xFC;                        // Supertraining-Bits
    d[0xEF] = 0;                            // Gen-6-Trainingsbeutel im Team

    d[0x3C] = formRemain;  // Gen 7: Rest, verstrichen, Maximum
    d[0x3D] = formElapsed;
    d[0x3E] = formMax;

    pk.updateChecksum();
    return pk;
}

} // namespace pw3d
