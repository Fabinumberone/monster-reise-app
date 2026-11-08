// Spielstände der 1. und 2. Generation (Virtual Console: Rot/Blau/Gelb, Gold/Silber/Kristall),
// internationale Fassungen (Englisch, Deutsch, …). Japanische und koreanische Fassungen nicht.
//
// Aufbau nach PKHeX (SAV1, SAV1Offsets, SAV2, SAV2Offsets, PokeList1/2, PK1/PK2):
// - Gen 1: Hauptbereich 0x2598–0x3522, 8-Bit-Prüfsumme (invertierte Summe) bei 0x3523.
//   Team 0x2F2C, aktuelle Box 0x30C0 (gespiegelt), Boxen 1–6 ab 0x4000, 7–12 ab 0x6000.
// - Gen 2 Kristall: Hauptbereich 0x2009–0x2B82, 16-Bit-Summe bei 0x2D0D und 0x1F0D,
//   Kopie des Hauptbereichs ab 0x1209. Team 0x2865, aktuelle Box 0x2D10, Boxen 1–7 ab 0x4000,
//   8–14 ab 0x6000. Gold/Silber: andere Positionen (siehe gb.cpp).
// - Listen: Anzahl, Artenliste (Kapazität + 1, Ende 0xFF), Datensätze, OT-Namen, Spitznamen (je 11).
// - VC-Spielstand von Kristall hat 16 Byte mehr (Echtzeituhr), die unverändert bleiben.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pw3d {

// Zeichensatz Gen 1/2 (international) <-> UTF-8. Ende 0x50.
std::string gbToUtf8(const uint8_t* p, size_t maxLen);
void utf8ToGb(const std::string& s, uint8_t* p, size_t maxLen); // füllt mit 0x50

uint16_t gen1InternalToNational(uint8_t internal);
uint8_t gen1NationalToInternal(uint16_t species);

// Item-Nummern Gen 1/2 <-> heutige Nummern (Gen 3+/7). 0 = keine Entsprechung.
uint16_t gen1ItemToModern(uint8_t item);
uint8_t modernItemToGen1(uint16_t item);
uint16_t gen2ItemToModern(uint8_t item);
uint8_t modernItemToGen2(uint16_t item);

enum class GBGame { Unknown, RBY, GS, Crystal };
const char* gbGameName(GBGame g);

struct GBMon {
    int gen = 1;
    uint16_t species = 0; // Pokédex-Nummer
    uint8_t level = 0;
    uint32_t exp = 0;
    std::array<uint16_t, 4> moves{};
    std::array<uint8_t, 4> pp{};      // inkl. AP-Plus in Bit 6–7
    uint16_t tid = 0;
    uint16_t dvs = 0;                 // Angriff, Verteidigung, Initiative, Spezial (je 4 Bit)
    std::array<uint16_t, 5> statExp{}; // KP, Angriff, Verteidigung, Initiative, Spezial
    uint8_t heldItem = 0;             // Gen 2 (Gen-2-Nummer); Gen 1: Fangrate-Byte
    uint8_t friendship = 0;           // Gen 2
    uint16_t hp = 0, maxHp = 0;
    std::string nickname, ot;
    std::array<uint8_t, 11> nickRaw{}, otRaw{}; // Namen im Spiel-Zeichensatz
    std::vector<uint8_t> raw;         // Datensatz wie gespeichert (33/44 bzw. 32/48 Byte)

    bool shiny() const;    // nach Gen-2-Regel aus den DVs
    uint8_t dv(int i) const { return uint8_t((dvs >> (12 - 4 * i)) & 15); } // 0 Atk, 1 Def, 2 Init, 3 Spez
};

// Einen Datensatz (Team- oder Boxformat, je nach len) mit Namen im Spiel-Zeichensatz auslesen.
GBMon gbFromRaw(int gen, const uint8_t* raw, size_t len, const uint8_t* otRaw, const uint8_t* nickRaw);

struct GBItem {
    uint8_t id = 0;
    uint8_t count = 0;
};

class SaveGB {
public:
    bool load(std::vector<uint8_t> data, std::string& err);
    GBGame game() const { return game_; }
    int generation() const { return game_ == GBGame::RBY ? 1 : 2; }
    bool checksumValid() const;

    std::string trainerName() const;
    uint16_t tid() const;
    uint32_t money() const;

    std::vector<GBMon> party() const;
    int boxCount() const { return game_ == GBGame::RBY ? 12 : 14; }
    int boxCapacity() const { return 20; }
    int currentBox() const;
    std::vector<GBMon> box(int b) const; // aktuelle Box aus dem Hauptbereich

    // Taschen: Gen 1 „Beutel“, Gen 2 „Items“, „Bälle“, „Basis“; dazu der Item-PC.
    std::vector<GBItem> bag() const;   // Gen 1: Beutel; Gen 2: Item-Tasche
    std::vector<GBItem> balls() const; // nur Gen 2
    std::vector<uint8_t> keyItems() const; // nur Gen 2
    std::vector<GBItem> pcItems() const;

    const std::vector<uint8_t>& raw() const { return data_; }

    // ---- Schreiben (finalize() danach aufrufen) ----
    void setParty(const std::vector<GBMon>& mons);     // raw im Teamformat
    void setBox(int b, const std::vector<GBMon>& mons); // raw im Team- oder Boxformat
    void setBag(const std::vector<GBItem>& items);
    void setBalls(const std::vector<GBItem>& items);
    void setPcItems(const std::vector<GBItem>& items);
    void setMoney(uint32_t v);
    void setTrainer(const std::string& name, uint16_t tid);
    void registerDex(uint16_t species);
    // Trainerpass (nur lesen, Stellen nach PKHeX): Spielzeit in Minuten, Orden (Anzahl gesetzter Bits) und Höchstzahl
    int playMinutes() const;
    int badgeCount() const;
    int badgeMax() const;
    // Prüfsummen (Gen 1 inkl. Box-Bänke und „Boxen eingerichtet“), Gen 2 inkl. Sicherungskopie.
    void finalize();

private:
    std::vector<GBMon> readList(uint32_t ofs, int capacity, bool party) const;
    std::vector<GBItem> readItems(uint32_t ofs, int capacity) const;
    void writeList(uint32_t ofs, int capacity, bool party, const std::vector<GBMon>& mons);
    void writeItems(uint32_t ofs, int capacity, const std::vector<GBItem>& items);
    uint32_t boxOffset(int b) const;

    std::vector<uint8_t> data_;
    GBGame game_ = GBGame::Unknown;
};

} // namespace pw3d
