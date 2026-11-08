// Gen 4 (Diamant/Perl, Platin, HeartGold/SoulSilver) und Gen 5 (Schwarz/Weiß, Schwarz 2/Weiß 2),
// Spielstände als .sav (512 KB, z. B. von TWiLight Menu++).
//
// Aufbau nach PKHeX (SAV4*, SAV5*, SaveBlockAccessor5*, PlayerBag4*/5*):
// - Gen 4: zwei Partitionen à 0x40000 mit je „allgemein“ und „PC“-Block; aktiv ist der Block mit
//   den höheren Zählern im Fuß (Ende − 0x14). Prüfsumme CRC16-CCITT über den Block ohne Fuß
//   (DP/Pt 0x14, HGSS 0x10), gespeichert in den letzten 2 Byte.
// - Gen 5: Blöcke mit CRC16-CCITT, gespeichert hinter jedem Block und in einer Prüfsummentabelle
//   (BW 0x23F00, B2W2 0x25F00).
// Pokémon (PK4/PK5): 136 Byte, im Team 236 (Gen 5: 220). Vier 32-Byte-Blöcke ab 0x08, Reihenfolge
// nach ((PID >> 13) & 31) % 24, XOR per LCRNG mit der Prüfsumme (Teamteil mit der PID).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pw3d {

enum class NDSGame { DP, Pt, HGSS, BW, B2W2 };
const char* ndsGameName(NDSGame g);
inline int ndsGeneration(NDSGame g) { return (g == NDSGame::BW || g == NDSGame::B2W2) ? 5 : 4; }

// Entschlüsseltes PK4/PK5 in fester Blockreihenfolge (A, B, C, D), Teamteil ab 0x88.
struct PK45 {
    std::array<uint8_t, 236> d{};
    int gen = 4;
    bool party = false;

    uint32_t pid() const;
    uint16_t checksum() const;
    uint16_t calcChecksum() const;
    bool present() const;
    uint16_t species() const;
    uint16_t heldItem() const;
    uint16_t tid() const;
    uint16_t sid() const;
    uint32_t exp() const;
    uint8_t friendship() const { return d[0x14]; }
    uint8_t ability() const { return d[0x15]; }
    uint8_t language() const { return d[0x17]; }
    std::array<uint16_t, 4> moves() const;
    uint32_t iv32() const;
    bool isEgg() const { return (iv32() >> 30) & 1; }
    bool isNicknamed() const { return (iv32() >> 31) & 1; }
    uint8_t gender() const { return (d[0x40] >> 1) & 3; }
    uint8_t form() const { return d[0x40] >> 3; }
    uint8_t nature() const; // Gen 4: PID % 25, Gen 5: eigenes Feld
    uint8_t version() const { return d[0x5F]; }
    uint8_t ball() const;
    uint8_t partyLevel() const { return d[0x8C]; }
    std::string nickname() const;
    std::string otName() const;
};

PK45 pk45Decrypt(const uint8_t* raw, int gen, bool party);
void pk45Encrypt(const PK45& pk, uint8_t* raw); // Prüfsumme muss gesetzt sein

// Zeichenkette Gen 4 (eigener Zeichensatz) bzw. Gen 5 (UTF-16), bis 0xFFFF; und zurück
// (maxChars inkl. Ende-Zeichen 0xFFFF, Rest 0).
std::string ndsString(const uint8_t* s, size_t maxChars, int gen);
void ndsEncodeString(const std::string& utf8, uint8_t* out, size_t maxChars, int gen);

struct NDSItem {
    uint16_t id = 0; // heutige Nummer (Gen 4/5 nutzen sie schon)
    uint16_t count = 0;
};

class SaveNDS {
public:
    bool load(std::vector<uint8_t> data, std::string& err);
    bool checksumsValid() const;
    NDSGame game() const { return game_; }
    int generation() const { return ndsGeneration(game_); }
    const std::vector<uint8_t>& raw() const { return data_; }

    std::string trainerName() const;
    uint16_t tid() const;
    uint16_t sid() const;
    uint8_t trainerGender() const;
    uint8_t language() const;
    uint32_t money() const;

    std::vector<PK45> party() const;
    int boxCount() const { return generation() == 5 ? 24 : 18; }
    static constexpr int SLOTS = 30;
    PK45 boxSlot(int box, int slot) const;

    enum class Pouch { Items, Key, TMs, Mail, Medicine, Berries, Balls, Battle };
    bool hasPouch(Pouch p) const;
    std::vector<NDSItem> pouch(Pouch p) const;
    int pouchCapacity(Pouch p) const { return pouchInfo(p).count; }

    // Schreiben (finalize() danach: Prüfsummen)
    void setParty(const std::vector<PK45>& mons);
    void setBoxSlot(int box, int slot, const PK45* pk); // nullptr = leer
    void setPouch(Pouch p, const std::vector<NDSItem>& items);
    // Gen 5: alle 24 Boxen freischalten (Gen 4 hat alle Boxen von Anfang an)
    void unlockAllBoxes();
    void setMoney(uint32_t v);
    void setTrainer(const std::string& name, uint16_t tid, uint16_t sid, uint8_t gender);
    // Pokédex: gefangen, gesehen (Geschlecht, Gen 5 auch Schillern), Sprache (nach PKHeX Zukan4/Zukan5).
    void registerDex(const PK45& pk);
    int dexCaughtCount() const;
    void finalize();

private:
    struct PouchInfo { uint32_t ofs; int count; };
    PouchInfo pouchInfo(Pouch p) const; // count 0 = gibt es nicht
    uint32_t trainerOfs() const;  // Gen 4: im allgemeinen Block; Gen 5: absolut
    uint32_t partyOfs() const;
    uint32_t boxOfs(int box, int slot) const;

    std::vector<uint8_t> data_;
    NDSGame game_ = NDSGame::Pt;
    uint32_t general_ = 0, storage_ = 0;   // Gen 4: Anfang der aktiven Blöcke
    uint32_t generalSize_ = 0, storageSize_ = 0, footer_ = 0x14;
};

// In welche reisende Tasche gehört das Item in diesem Spiel? (int(SaveNDS::Pouch) oder −1;
// erzeugt von tools/gen-ndsitems.mjs)
int ndsTravelPouch(NDSGame g, uint16_t item);

} // namespace pw3d
