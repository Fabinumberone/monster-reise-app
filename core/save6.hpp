// Spielstand der 6. Generation (Datei „main“ von XY und ORAS).
//
// Aufbau (nach PKHeX, an echten Spielständen noch zu bestätigen):
// - Die letzten 0x200 Byte sind die Blocktabelle mit Kennung 0x42454546 („BEEF“).
//   Danach je Block 8 Byte: Länge (u32), Kennung (u16), CRC16 (u16).
// - Blöcke liegen hintereinander ab 0, jeweils auf 0x200 aufgerundet.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pk6.hpp"

namespace pw3d {

enum class Game6 { Unknown, XY, ORAS };

const char* gameName(Game6 g);

struct Block6 {
    uint32_t offset = 0;
    uint32_t length = 0;
    uint16_t id = 0;
    uint16_t storedCrc = 0;
    uint16_t computedCrc = 0;
    bool ok() const { return storedCrc == computedCrc; }
};

struct Trainer6 {
    uint16_t tid = 0;
    uint16_t sid = 0;
    uint8_t gender = 0;
    std::string name;
};

struct Layout6 {
    uint32_t size;
    uint32_t trainer;   // MyStatus
    uint32_t party;     // 6 × 0x104, danach Anzahl
    uint32_t box;       // 31 × 30 × 0xE8
    uint32_t boxCount;
};

class Save6 {
public:
    // Erkennt das Spiel an der Dateigröße und liest die Blocktabelle. Fehlertext in err.
    bool load(std::vector<uint8_t> data, std::string& err);

    Game6 game() const { return game_; }
    const Layout6& layout() const { return layout_; }
    const std::vector<Block6>& blocks() const { return blocks_; }
    bool checksumsValid() const;
    void updateChecksums();

    Trainer6 trainer() const;
    void setTrainer(const Trainer6& t);

    int partyCount() const;
    PK6 partySlot(int i) const;
    void setPartySlot(int i, PK6 pk);

    static constexpr int BOX_SLOTS = 30;
    int boxCount() const { return int(layout_.boxCount); }
    bool boxSlotEmpty(int box, int slot) const;
    PK6 boxSlot(int box, int slot) const;
    void setBoxSlot(int box, int slot, PK6 pk);

    std::vector<uint8_t>& raw() { return data_; }
    const std::vector<uint8_t>& raw() const { return data_; }

private:
    bool readBlockTable(std::string& err);
    uint32_t boxOffset(int box, int slot) const;

    std::vector<uint8_t> data_;
    std::vector<Block6> blocks_;
    Game6 game_ = Game6::Unknown;
    Layout6 layout_{};
};

} // namespace pw3d
