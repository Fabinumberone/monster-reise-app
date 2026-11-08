// Weltkarte für die Reise-Optik (eigenes Bild des Nutzers, nur auf der SD: weltkarte.bin, erzeugt mit
// tools/karte.mjs oder monster-reise-daten.html). Als Kacheln geladen, damit die App hineinzoomen kann.
#pragma once
#include <citro2d.h>

#include <vector>

class WorldMap {
public:
    bool load(const char* path);
    bool loaded() const { return !img_.empty(); }
    int width() const { return width_; }
    int height() const { return height_; }
    // Zeichnet den Ausschnitt um (cx, cy) in Kartenpixeln mit Maßstab scale (Bildschirmpixel je Kartenpixel)
    // auf den oberen Bildschirm (400 × 240).
    void draw(float cx, float cy, float scale) const;

private:
    int width_ = 0, height_ = 0, tile_ = 0, cols_ = 0, rows_ = 0;
    std::vector<C3D_Tex*> tex_;
    std::vector<C2D_Image> img_;
    Tex3DS_SubTexture sub_{};
};
