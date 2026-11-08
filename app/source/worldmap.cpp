#include "worldmap.hpp"

#include <cstdio>
#include <cstring>

bool WorldMap::load(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    unsigned char h[20];
    bool ok = std::fread(h, 1, sizeof h, f) == sizeof h && std::memcmp(h, "PW3DMAP2", 8) == 0;
    auto rd16 = [&](int o) { return int(h[o] | (h[o + 1] << 8)); };
    if (ok) {
        width_ = rd16(8);
        height_ = rd16(10);
        tile_ = rd16(12);
        cols_ = rd16(14);
        rows_ = rd16(16);
        ok = tile_ == 512 && cols_ > 0 && rows_ > 0 && cols_ * rows_ <= 16;
    }
    for (int i = 0; ok && i < cols_ * rows_; i++) {
        C3D_Tex* t = new C3D_Tex;
        if (!C3D_TexInit(t, u16(tile_), u16(tile_), GPU_RGB565)) { delete t; ok = false; break; }
        size_t n = size_t(tile_) * size_t(tile_) * 2;
        if (std::fread(t->data, 1, n, f) != n) { C3D_TexDelete(t); delete t; ok = false; break; }
        C3D_TexFlush(t);
        C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        tex_.push_back(t);
    }
    std::fclose(f);
    if (!ok) {
        for (C3D_Tex* t : tex_) { C3D_TexDelete(t); delete t; }
        tex_.clear();
        return false;
    }
    // Ganze Kachel; obere Zeile bei v = 1 (wie bei den Symbolen)
    sub_ = Tex3DS_SubTexture{u16(tile_), u16(tile_), 0.0f, 1.0f, 1.0f, 0.0f};
    for (C3D_Tex* t : tex_) img_.push_back(C2D_Image{t, &sub_});
    return true;
}

void WorldMap::draw(float cx, float cy, float scale) const {
    for (int r = 0; r < rows_; r++)
        for (int c = 0; c < cols_; c++) {
            float x = (float(c * tile_) - cx) * scale + 200.0f;
            float y = (float(r * tile_) - cy) * scale + 120.0f;
            float s = float(tile_) * scale;
            if (x > 400 || y > 240 || x + s < 0 || y + s < 0) continue;
            // +0.5 Pixel Überlappung gegen feine Nähte zwischen den Kacheln
            C2D_DrawImageAt(img_[size_t(r * cols_ + c)], x, y, 0.0f, nullptr, scale * (1.0f + 1.0f / float(tile_)), scale * (1.0f + 1.0f / float(tile_)));
        }
}
