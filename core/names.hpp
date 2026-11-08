// Deutsche Namen für Pokémon, Items und Attacken aus namen.txt (erzeugt von tools/daten.mjs
// aus dem eigenen Dump, liegt nur auf der SD). Fehlt die Datei, liefern die Funktionen Nummern.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pw3d {

class Names {
public:
    bool load(const std::string& path);
    bool loaded() const { return !species_.empty(); }

    std::string species(uint16_t n) const { return get(species_, n, "#"); }
    std::string item(uint16_t n) const { return get(items_, n, "Item "); }
    std::string move(uint16_t n) const { return get(moves_, n, "Attacke "); }
    std::string speciesEn(uint16_t n) const { return get(speciesEn_, n, "#"); }

private:
    static std::string get(const std::vector<std::string>& v, uint16_t n, const char* fallback);
    std::vector<std::string> species_, items_, moves_, speciesEn_;
};

} // namespace pw3d
