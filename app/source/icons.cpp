#include "icons.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr size_t ICON_BYTES = 64 * 32 * 2;
u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
} // namespace

bool IconSet::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<u8> d(size_t(n > 0 ? n : 0));
    bool ok = n > 16 && std::fread(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    if (!ok || std::memcmp(d.data(), "PW3DICON", 8) != 0) return false;
    u16 species = rd16(&d[12]);
    iconCount_ = rd16(&d[14]);
    size_t p = 16;
    if (d.size() < p + (species + 1) * 3 + size_t(iconCount_) * ICON_BYTES) return false;
    start_.resize(species + 1);
    count_.resize(species + 1);
    for (u16 s = 0; s <= species; s++) start_[s] = rd16(&d[p + s * 2]);
    p += (species + 1) * 2;
    for (u16 s = 0; s <= species; s++) count_[s] = d[p + s];
    p += species + 1;
    data_.assign(d.begin() + long(p), d.begin() + long(p + size_t(iconCount_) * ICON_BYTES));
    tex_.assign(iconCount_, nullptr);
    img_.resize(iconCount_);
    return true;
}

const C2D_Image* IconSet::get(u16 species, u8 form) {
    if (!loaded() || species >= start_.size() || !count_[species]) return nullptr;
    u16 i = u16(start_[species] + (form < count_[species] ? form : 0));
    if (i >= iconCount_) return nullptr;
    if (!tex_[i]) {
        C3D_Tex* t = new C3D_Tex;
        if (!C3D_TexInit(t, 64, 32, GPU_RGBA5551)) { delete t; return nullptr; }
        std::memcpy(t->data, &data_[size_t(i) * ICON_BYTES], ICON_BYTES);
        C3D_TexFlush(t);
        C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
        tex_[i] = t;
        img_[i] = C2D_Image{t, &sub_};
    }
    return &img_[i];
}
