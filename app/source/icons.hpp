// PC-Symbole der Pokémon aus icons.bin (erzeugt von tools/gen-icons.mjs aus dem eigenen
// Ultramond-Dump, liegt nur auf der SD).
//
// icons.bin: "PW3DICON", u32 Version 1, u16 Arten (807), u16 Symbole,
//            u16 Start[Arten+1], u8 Anzahl[Arten+1], dann je Symbol 4096 Byte:
//            64×32 RGBA5551 im PICA-Kachelformat, aufrecht, Symbol bei x 12–52, y 0–30.
#pragma once
#include <citro2d.h>

#include <string>
#include <vector>

class IconSet {
public:
    bool load(const std::string& path);
    bool loaded() const { return !data_.empty(); }
    // Symbol für Art und Form (lädt die Textur beim ersten Gebrauch). nullptr, wenn keins.
    const C2D_Image* get(u16 species, u8 form);

private:
    std::vector<u8> data_;
    std::vector<u16> start_;
    std::vector<u8> count_;
    u16 iconCount_ = 0;
    std::vector<C3D_Tex*> tex_;
    std::vector<C2D_Image> img_;
    Tex3DS_SubTexture sub_{40, 30, 12.0f / 64, 1.0f, 52.0f / 64, 1.0f - 30.0f / 32};
};
