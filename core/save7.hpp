// Spielstand der 7. Generation (Datei „main“ von Sonne/Mond und Ultrasonne/Ultramond).
//
// Gleiche Blocktabelle wie Gen 6, aber gespiegeltes und invertiertes CRC16. Block 0x24 enthält
// ab +0x100 eine 0x80-Byte-Signatur (siehe memecrypto.hpp); seine CRC wird mit genullter
// Signatur berechnet. Pokémon-Datensätze haben dieselbe Größe und Verschlüsselung wie in Gen 6.
#pragma once
#include "savefile.hpp"

namespace pw3d {

enum class Game7 { Unknown, SM, USUM };

struct Layout7 {
    uint32_t size;
    uint32_t trainer; // MyStatus: TID, SID, Spiel +4, Geschlecht +5, Sprache +0x35, Name +0x38
    uint32_t party;
    uint32_t box;
    uint32_t boxCount;
    uint32_t money;
    uint32_t dex;
    uint16_t maxSpecies; // SM 802, USUM 807
};

class Save7 : public SaveFile {
public:
    bool load(std::vector<uint8_t> data, std::string& err);

    Game7 game() const { return game_; }
    int generation() const override { return 7; }
    GameKind kind() const override { return game_ == Game7::SM ? GameKind::SM : GameKind::USUM; }
    const char* gameTitle() const override;
    uint16_t maxSpecies() const override { return layout_.maxSpecies; }

    bool valid() const override; // Prüfsummen und Signatur
    bool checksumsValid() const;
    bool signatureValid() const;
    void finalize() override;    // Prüfsummen, danach Signatur
    const std::vector<Block6>& blocks() const override { return blocks_; }

    Trainer6 trainer() const override;
    void setTrainer(const Trainer6& t) override;
    uint32_t money() const;
    void setMoney(uint32_t v);

    int partyCount() const override;
    void setPartyCount(int n) override;
    PK6 partySlot(int i) const override;
    void setPartySlot(int i, PK6 pk) override;

    int boxCount() const override { return int(layout_.boxCount); }
    PK6 boxSlot(int box, int slot) const override;
    void setBoxSlot(int box, int slot, PK6 pk) override;

    void registerDex(const PK6& pk) override;

private:
    uint16_t blockCrc(const Block6& b) const;
    uint32_t boxOffset(int box, int slot) const;

    std::vector<Block6> blocks_;
    uint32_t tableAt_ = 0;
    Game7 game_ = Game7::Unknown;
    Layout7 layout_{};
};

} // namespace pw3d
