#include "names.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pw3d {

bool Names::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    species_.clear();
    items_.clear();
    moves_.clear();
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char* tab1 = std::strchr(line, '\t');
        if (!tab1) continue;
        char* tab2 = std::strchr(tab1 + 1, '\t');
        if (!tab2) continue;
        long n = std::strtol(tab1 + 1, nullptr, 10);
        std::string name = tab2 + 1;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        std::vector<std::string>* v = line[0] == 'P' ? &species_ : line[0] == 'I' ? &items_ : line[0] == 'A' ? &moves_ : nullptr;
        if (!v || n <= 0 || n > 4096) continue;
        if (v->size() <= size_t(n)) v->resize(size_t(n) + 1);
        (*v)[size_t(n)] = name;
    }
    std::fclose(f);
    return loaded();
}

std::string Names::get(const std::vector<std::string>& v, uint16_t n, const char* fallback) {
    if (n < v.size() && !v[n].empty()) return v[n];
    return fallback + std::to_string(n);
}

} // namespace pw3d
