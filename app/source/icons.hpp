// PC-Symbole der Pokémon aus icons.bin (erzeugt von tools/daten.mjs aus dem eigenen
// Ultramond-Dump, liegt nur auf der SD).
//
// icons.bin Version 2: "PW3DICON", u32 2, u16 Arten, u16 Symbole, u32 Offset[Arten+1],
//   u32 Länge Tabellenbereich, Tabellenbereich (je Art: u8 Formen, u8 0, Formen × (u16 m, u16 w)),
//   danach je Symbol 4096 Byte: 64×32 RGBA5551 im PICA-Kachelformat, aufrecht, Symbol bei x 12–52.
// Die Zuordnung Art/Form/Geschlecht → Symbol stammt aus der Tabelle im Programmcode des Spiels.
#pragma once
#include <citro2d.h>

#include <string>
#include <vector>

class IconSet {
public:
    bool load(const std::string& path);
    bool loaded() const { return !data_.empty(); }
    // Symbol für Art, Form und Geschlecht (1 = weiblich); lädt die Textur beim ersten Gebrauch.
    const C2D_Image* get(u16 species, u8 form, u8 gender);

private:
    std::vector<u8> data_;   // Bilddaten
    std::vector<u8> table_;  // Tabellenbereich
    std::vector<u32> offset_;
    u16 iconCount_ = 0;
    std::vector<C3D_Tex*> tex_;
    std::vector<C2D_Image> img_;
    // Ausschnitt mit oben > unten (sonst deutet citro2d die Textur als um 90° gedreht).
    // Die erste Speicherzeile liegt bei v = 1, daher legt tools/lib/monsterdaten.mjs die obere Bildzeile zuerst ab.
    Tex3DS_SubTexture sub_{40, 30, 12.0f / 64, 1.0f, 52.0f / 64, 1.0f - 30.0f / 32};
};
