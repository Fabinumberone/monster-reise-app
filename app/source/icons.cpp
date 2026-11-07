#include "icons.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr size_t ICON_BYTES = 64 * 32 * 2;
u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
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
    if (!ok || std::memcmp(d.data(), "PW3DICON", 8) != 0 || rd32(&d[8]) != 2) return false;
    u16 species = rd16(&d[12]);
    iconCount_ = rd16(&d[14]);
    size_t p = 16;
    if (d.size() < p + (species + 1) * 4 + 4) return false;
    offset_.resize(species + 1);
    for (u16 s = 0; s <= species; s++) offset_[s] = rd32(&d[p + s * 4]);
    p += (species + 1) * 4;
    u32 tabLen = rd32(&d[p]);
    p += 4;
    if (d.size() < p + tabLen + size_t(iconCount_) * ICON_BYTES) return false;
    table_.assign(d.begin() + long(p), d.begin() + long(p + tabLen));
    p += tabLen;
    data_.assign(d.begin() + long(p), d.begin() + long(p + size_t(iconCount_) * ICON_BYTES));
    tex_.assign(iconCount_, nullptr);
    img_.resize(iconCount_);
    return true;
}

const C2D_Image* IconSet::get(u16 species, u8 form, u8 gender) {
    if (!loaded() || species >= offset_.size()) return nullptr;
    const u8* e = &table_[offset_[species]];
    u8 forms = e[0];
    if (!forms) return nullptr;
    if (form >= forms) form = 0;
    u16 i = rd16(e + 2 + form * 4 + (gender == 1 ? 2 : 0));
    if (i == 0 || i >= iconCount_) return nullptr;
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
