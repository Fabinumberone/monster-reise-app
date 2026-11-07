// Spielstand der 6. Generation (Datei „main“ von XY und ORAS). An echten Spielständen bestätigt.
// - Die letzten 0x200 Byte sind die Blocktabelle mit Kennung 0x42454546 („BEEF“),
//   danach je Block 8 Byte: Länge (u32), Kennung (u16), CRC16-CCITT (u16).
// - Blöcke liegen hintereinander ab 0, jeweils auf 0x200 aufgerundet.
#pragma once
#include "savefile.hpp"

namespace pw3d {

enum class Game6 { Unknown, XY, ORAS };

struct Layout6 {
    uint32_t size;
    uint32_t trainer;   // MyStatus
    uint32_t party;     // 6 × 0x104, danach Anzahl
    uint32_t box;       // 31 × 30 × 0xE8
    uint32_t boxCount;
    uint32_t dex;       // Pokédex-Block
    uint32_t dexLang;   // Sprachbits relativ zu dex (7 je Art)
};

class Save6 : public SaveFile {
public:
    static bool knownSize(size_t n) { return n == 0x65600 || n == 0x76000; }
    bool load(std::vector<uint8_t> data, std::string& err);

    Game6 game() const { return game_; }
    int generation() const override { return 6; }
    GameKind kind() const override { return game_ == Game6::XY ? GameKind::XY : GameKind::ORAS; }
    const char* gameTitle() const override;
    uint16_t maxSpecies() const override { return 721; }

    bool valid() const override;
    void finalize() override;
    const std::vector<Block6>& blocks() const override { return blocks_; }

    Trainer6 trainer() const override;
    void setTrainer(const Trainer6& t) override;
    uint32_t money() const override;      // Misc-Block 0x4200 + 8 (XY und ORAS)
    void setMoney(uint32_t v) override;

    int partyCount() const override;
    void setPartyCount(int n) override;
    PK6 partySlot(int i) const override;
    void setPartySlot(int i, PK6 pk) override;

    int boxCount() const override { return int(layout_.boxCount); }
    PK6 boxSlot(int box, int slot) const override;
    void setBoxSlot(int box, int slot, PK6 pk) override;

    void registerDex(const PK6& pk) override;

private:
    uint32_t boxOffset(int box, int slot) const;

    std::vector<Block6> blocks_;
    uint32_t tableAt_ = 0;
    Game6 game_ = Game6::Unknown;
    Layout6 layout_{};
};

} // namespace pw3d
