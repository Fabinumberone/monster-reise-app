// Spielstand der 7. Generation (Datei „main“ von Sonne/Mond und Ultrasonne/Ultramond).
//
// Gleiche Blocktabelle wie Gen 6 (Kennung BEEF, 8 Byte je Block, 0x200-Ausrichtung), aber
// gespiegeltes und invertiertes CRC16. Block 0x24 enthält ab +0x100 eine 0x80-Byte-Signatur
// (MemeCrypto); seine CRC wird mit genullter Signatur berechnet.
// Pokémon-Datensätze haben dieselbe Größe und Verschlüsselung wie in Gen 6.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "pk6.hpp"
#include "save6.hpp" // Block6, Trainer6

namespace pw3d {

enum class Game7 { Unknown, SM, USUM };

const char* gameName(Game7 g);

constexpr uint16_t GEN7_MAX_SPECIES = 807;

struct Layout7 {
    uint32_t size;
    uint32_t trainer; // MyStatus: TID, SID, Spiel, Geschlecht, … Name bei +0x38
    uint32_t party;
    uint32_t box;
    uint32_t boxCount;
    uint32_t money;
};

class Save7 {
public:
    bool load(std::vector<uint8_t> data, std::string& err);

    Game7 game() const { return game_; }
    const std::vector<Block6>& blocks() const { return blocks_; }
    bool checksumsValid() const;
    void updateChecksums();

    Trainer6 trainer() const;
    uint32_t money() const;
    void setMoney(uint32_t v);

    int partyCount() const;
    PK6 partySlot(int i) const;

    static constexpr int BOX_SLOTS = 30;
    int boxCount() const { return int(layout_.boxCount); }
    bool boxSlotEmpty(int box, int slot) const;
    PK6 boxSlot(int box, int slot) const;

    std::vector<uint8_t>& raw() { return data_; }
    const std::vector<uint8_t>& raw() const { return data_; }

private:
    uint16_t blockCrc(const Block6& b) const;
    uint32_t boxOffset(int box, int slot) const;

    std::vector<uint8_t> data_;
    std::vector<Block6> blocks_;
    uint32_t tableAt_ = 0; // erster Tabelleneintrag
    Game7 game_ = Game7::Unknown;
    Layout7 layout_{};
};

} // namespace pw3d
