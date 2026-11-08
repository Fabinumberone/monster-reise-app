// Gen 3 (GBA: Rubin/Saphir, Smaragd, Feuerrot/Blattgrün), Spielstände von mGBA (.sav, 128 KB).
//
// Aufbau (nach PKHeX SAV3*/SaveBlock3*/PlayerBag3*): zwei Bereiche à 14 Sektoren (0x1000 Byte,
// davon 0xF80 Daten; Fußzeile: +0xFF4 u16 Sektor-ID, +0xFF6 u16 Prüfsumme, +0xFF8 u32 0x08012025,
// +0xFFC u32 Speicherzähler). Aktiv ist der Bereich mit allen Sektoren und dem höheren Zähler.
// Sektor 0 = „klein“ (Trainer, Pokédex), 1–4 = „groß“ (Team, Geld, Items), 5–13 = PC.
// Pokémon (PK3, 80 Byte, im Team 100): 32 Byte Kopf, 48 Byte in vier 12-Byte-Blöcken
// (Wachstum, Attacken, EVs, Sonstiges), Reihenfolge nach PID % 24, XOR mit PID ^ OTID.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pw3d {

enum class GBAGame { RS, E, FRLG };
const char* gbaGameName(GBAGame g);

// Entschlüsseltes Pokémon in fester Reihenfolge: 0x00 Kopf, 0x20 Wachstum, 0x2C Attacken,
// 0x38 EVs/Wettbewerb, 0x44 Sonstiges, 0x50 Teamwerte (nur im Team).
struct PK3 {
    std::array<uint8_t, 100> d{};
    bool party = false;

    uint32_t pid() const;
    uint16_t tid() const;
    uint16_t sid() const;
    uint8_t language() const { return d[0x12]; } // 1 JP, 2 EN, 3 FR, 4 IT, 5 DE, 7 ES
    bool isEgg() const;
    bool present() const; // Art gesetzt, Prüfsumme stimmt
    uint16_t speciesInternal() const;
    uint16_t species() const; // Pokédex-Nummer
    uint16_t heldItem() const; // Gen-3-Nummer
    uint32_t exp() const;
    uint8_t friendship() const { return d[0x29]; }
    std::array<uint16_t, 4> moves() const;
    uint32_t ivWord() const;   // IVs (je 5 Bit), Bit 30 Ei, Bit 31 Fähigkeit
    uint8_t ball() const;      // aus dem Herkunftsfeld
    uint8_t version() const;   // Herkunftsspiel (1 Saphir, 2 Rubin, 3 Smaragd, 4 Feuerrot, 5 Blattgrün, 15 Colosseum/XD)
    uint8_t partyLevel() const { return party ? d[0x54] : 0; }
    std::string nickname() const;
    std::string otName() const;
    uint16_t checksum() const;     // gespeicherte Prüfsumme
    uint16_t calcChecksum() const; // über die 48 Datenbytes
};

// Entschlüsseln/Verschlüsseln (Rohdaten wie gespeichert <-> PK3).
PK3 pk3Decrypt(const uint8_t* raw, bool party);
void pk3Encrypt(const PK3& pk, uint8_t* raw);

// Zeichenkette im Gen-3-Zeichensatz (bis 0xFF) und zurück (unbekannte Zeichen -> „?“, Rest 0xFF).
std::string gen3String(const uint8_t* s, size_t max);
void gen3Encode(const std::string& utf8, uint8_t* out, size_t max);

struct GBAItem {
    uint16_t id = 0;  // Gen-3-Nummer
    uint16_t count = 0;
};

class SaveGBA {
public:
    bool load(std::vector<uint8_t> data, std::string& err);
    bool checksumsValid() const;
    GBAGame game() const { return game_; }
    const std::vector<uint8_t>& raw() const { return data_; }

    std::string trainerName() const;
    uint8_t trainerGender() const { return small_[0x08]; }
    uint16_t tid() const;
    uint16_t sid() const;
    uint32_t money() const;
    uint32_t securityKey() const;

    std::vector<PK3> party() const;
    static constexpr int BOXES = 14, SLOTS = 30;
    PK3 boxSlot(int box, int slot) const;
    int currentBox() const { return storage_[0]; }

    // Taschen: Items, Schlüssel-Items, Bälle, TM/VM, Beeren, Item-PC
    enum class Pouch { Items, Key, Balls, TMs, Berries, PC };
    std::vector<GBAItem> pouch(Pouch p) const;
    int pouchCapacity(Pouch p) const { return pouchInfo(p).count; }
    uint16_t maxStack(Pouch p) const { return (p == Pouch::PC || game_ == GBAGame::FRLG) ? 999 : 99; }

    // Schreiben (finalize() danach: Daten zurück in die Sektoren, Prüfsummen)
    void setParty(const std::vector<PK3>& mons);
    void setBoxSlot(int box, int slot, const PK3* pk); // nullptr = leer
    void setPouch(Pouch p, const std::vector<GBAItem>& items);
    void setMoney(uint32_t v);
    void setTrainer(const std::string& name, uint16_t tid, uint16_t sid, uint8_t gender);
    void registerDex(uint16_t species); // gesehen + gefangen
    // Trainerpass (nur lesen, Stellen nach PKHeX): Spielzeit in Minuten, Orden (Anzahl gesetzter Bits) und Höchstzahl
    int playMinutes() const;
    int badgeCount() const;
    int badgeMax() const;
    void finalize();

private:
    struct PouchInfo { uint32_t ofs; int count; };
    PouchInfo pouchInfo(Pouch p) const;
    uint32_t inventoryOfs() const { return game_ == GBAGame::FRLG ? 0x298 : 0x498; }
    uint32_t partyCountOfs() const { return game_ == GBAGame::FRLG ? 0x34 : 0x234; }
    uint32_t moneyOfs() const { return game_ == GBAGame::FRLG ? 0x290 : 0x490; }
    uint32_t seen2Ofs() const { return game_ == GBAGame::RS ? 0x938 : game_ == GBAGame::E ? 0x988 : 0x5F8; }
    uint32_t seen3Ofs() const { return game_ == GBAGame::RS ? 0x3A8C : game_ == GBAGame::E ? 0x3B24 : 0x3A18; }

    std::vector<uint8_t> data_;
    int active_ = 0;
    GBAGame game_ = GBAGame::RS;
    std::vector<uint8_t> small_, large_, storage_; // zusammengesetzte Daten (0xF80 je Sektor)
};

} // namespace pw3d
