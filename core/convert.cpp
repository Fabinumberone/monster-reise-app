#include "convert.hpp"

#include <cstring>

#include "bytes.hpp"

namespace pw3d {

namespace {

struct Limits {
    uint16_t species, move, item;
    uint8_t ability, ball, maxVersion;
};

// Höchste gültige Werte je Spiel (Arten, Attacken, Items, Fähigkeiten, Bälle, Herkunftsspiel).
// Gen 6 nimmt konservativ nur Pokémon aus Gen-6-Spielen oder älter (Version ≤ 27) an.
Limits limitsOf(GameKind k) {
    switch (k) {
    case GameKind::XY: return {721, 621, 717, 191, 25, 27};
    case GameKind::ORAS: return {721, 621, 775, 191, 25, 27};
    case GameKind::SM: return {802, 720, 920, 233, 26, 33};
    case GameKind::USUM: return {807, 728, 959, 233, 26, 33};
    }
    return {0, 0, 0, 0, 0, 0};
}

// Arten mit Alola-Form (Form 1), die es in Gen 6 nicht gibt.
bool hasAlolanForm(uint16_t s) {
    switch (s) {
    case 19: case 20: case 26: case 27: case 28: case 37: case 38: case 50: case 51: case 52: case 53:
    case 74: case 75: case 76: case 88: case 89: case 103: case 105:
        return true;
    default:
        return false;
    }
}

uint8_t markings6From7(uint16_t m7) {
    uint8_t m = 0;
    for (int i = 0; i < 6; i++)
        if ((m7 >> (i * 2)) & 3) m |= uint8_t(1u << i);
    return m;
}

} // namespace

const char* kindName(GameKind k) {
    switch (k) {
    case GameKind::XY: return "XY";
    case GameKind::ORAS: return "ORAS";
    case GameKind::SM: return "SM";
    case GameKind::USUM: return "USUM";
    }
    return "?";
}

int kindGeneration(GameKind k) { return (k == GameKind::XY || k == GameKind::ORAS) ? 6 : 7; }

PK6 toGen7(const PK6& in) {
    PK6 pk = in;
    uint8_t* d = pk.d.data();

    uint8_t oldMarks = d[0x2A];
    uint8_t formMax = d[0x3C], formRemain = d[0xED], formElapsed = d[0xEE];

    d[0x2A] = 0;                 // Gen 7: Resort-Ereignis (Gen 6: Markierungen)
    d[0xDE] = 0;                 // Gen 7: Hypertraining (Gen 6: Begegnungstyp Gen 4)
    wr32(d + 0x3C, 0);           // Formargument neu aufgebaut
    d[0xED] = 0;                 // Gen 7: Schmutz-Art
    d[0xEE] = 0;                 // Gen 7: Schmutz-Ort

    uint16_t marks = 0;          // Gen 7: 2 Bit je Symbol, 1 = blau (Gen 6: Supertraining-Beutel)
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

PK6 toGen6(const PK6& in) {
    PK6 pk = in;
    uint8_t* d = pk.d.data();
    uint16_t marks7 = rd16(d + 0x16);
    uint8_t formRemain = d[0x3C], formElapsed = d[0x3D], formMax = d[0x3E];

    wr16(d + 0x16, 0);           // Gen 6: Supertraining-Beutel
    d[0x2A] = markings6From7(marks7);
    d[0xDE] = 0;                 // Gen 6: Begegnungstyp
    wr32(d + 0x3C, 0);
    d[0x3C] = formMax;           // Gen 6: nur Maximum hier
    d[0xED] = formRemain;        // Gen 6: Rest/verstrichen im Teambereich
    d[0xEE] = formElapsed;
    d[0xEF] = 0;

    pk.updateChecksum();
    return pk;
}

bool fitsIn(const PK6& pk, GameKind k) {
    Limits L = limitsOf(k);
    uint16_t sp = pk.species();
    if (sp == 0 || sp > L.species) return false;
    if (pk.heldItem() > L.item) return false;
    if (pk.ability() > L.ability) return false;
    if (pk.d[0xDC] > L.ball) return false;
    if (pk.version() > L.maxVersion) return false;
    for (int i = 0; i < 4; i++) {
        if (rd16(pk.d.data() + 0x5A + i * 2) > L.move) return false; // Attacken
        if (rd16(pk.d.data() + 0x6A + i * 2) > L.move) return false; // Grundattacken
    }
    uint8_t f = pk.form();
    if (kindGeneration(k) == 6 && f > 0 && hasAlolanForm(sp)) return false;
    if (k == GameKind::SM) { // Formen, die erst USUM hat
        if (sp == 800 && f > 0) return false; // Necrozma Abendmähne/Morgenschwingen/Ultra
        if (sp == 744 && f > 0) return false; // Wuffels mit Tempomacher
        if (sp == 745 && f == 2) return false; // Wolwerock Zwielichtform
    }
    return true;
}

void restoreGen6Only(PK6& pk6, const PK6& last) {
    uint8_t* d = pk6.d.data();
    const uint8_t* o = last.d.data();
    std::memcpy(d + 0x16, o + 0x16, 2);             // Supertraining-Beutel/Treffer
    d[0x72] = uint8_t((d[0x72] & 0xFC) | (o[0x72] & 3));
    std::memcpy(d + 0x94, o + 0x94, 0x9E - 0x94);   // Geodaten
    std::memcpy(d + 0xAA, o + 0xAA, 0xB0 - 0xAA);   // Ami-Sättigung/Freude
    d[0xDE] = o[0xDE];                              // Begegnungstyp
    std::memcpy(d + 0xE4, o + 0xE4, 4);
    d[0xEF] = o[0xEF];
    pk6.updateChecksum();
}

void restoreGen7Only(PK6& pk7, const PK6& last, uint8_t gen6Markings) {
    uint8_t* d = pk7.d.data();
    const uint8_t* o = last.d.data();
    d[0x2A] = o[0x2A]; // Resort-Ereignis
    d[0xDE] = o[0xDE]; // Hypertraining
    // Farbige Markierungen behalten, solange in Gen 6 nichts an ihnen geändert wurde.
    uint16_t lastMarks = rd16(o + 0x16);
    if (markings6From7(lastMarks) == gen6Markings) wr16(d + 0x16, lastMarks);
    pk7.updateChecksum();
}

} // namespace pw3d
