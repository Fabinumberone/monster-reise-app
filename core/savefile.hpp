// Gemeinsame Schnittstelle für Spielstände der 6. und 7. Generation.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pk6.hpp"

namespace pw3d {

struct Trainer6 {
    uint16_t tid = 0;
    uint16_t sid = 0;
    uint8_t gender = 0;
    std::string name;
};

struct Block6 {
    uint32_t offset = 0;
    uint32_t length = 0;
    uint16_t id = 0;
    uint16_t storedCrc = 0;
    uint16_t computedCrc = 0;
    bool ok() const { return storedCrc == computedCrc; }
};

class SaveFile {
public:
    virtual ~SaveFile() = default;

    virtual int generation() const = 0;
    virtual const char* gameTitle() const = 0;
    virtual uint16_t maxSpecies() const = 0;

    // Alle Prüfsummen (und bei Gen 7 die Signatur) stimmen.
    virtual bool valid() const = 0;
    // Prüfsummen (und Signatur) neu berechnen. Vor dem Schreiben aufrufen.
    virtual void finalize() = 0;
    virtual const std::vector<Block6>& blocks() const = 0;

    virtual Trainer6 trainer() const = 0;
    virtual void setTrainer(const Trainer6& t) = 0;

    virtual int partyCount() const = 0;
    virtual void setPartyCount(int n) = 0;
    virtual PK6 partySlot(int i) const = 0;
    virtual void setPartySlot(int i, PK6 pk) = 0;

    static constexpr int BOX_SLOTS = 30;
    virtual int boxCount() const = 0;
    virtual PK6 boxSlot(int box, int slot) const = 0;
    virtual void setBoxSlot(int box, int slot, PK6 pk) = 0;
    bool boxSlotEmpty(int box, int slot) const;

    // Pokédex: gesehen, gefangen, angezeigt, Sprache (Grundform; Formen noch nicht).
    virtual void registerDex(const PK6& pk) = 0;

    // Hängt ein Pokémon (Teamformat, 0x104 Byte) hinten ans Team an. false, wenn voll.
    bool addToParty(const PK6& pk);

    // Setzt den Trainer auf t und schreibt alle Pokémon (Team, Boxen), deren Originaltrainer
    // der bisherige ist, auf t um. Gibt deren Anzahl zurück.
    int adoptTrainer(const Trainer6& t);

    std::vector<uint8_t>& raw() { return data_; }
    const std::vector<uint8_t>& raw() const { return data_; }

protected:
    std::vector<uint8_t> data_;
};

// Erkennt das Spiel an der Größe. nullptr und Fehlertext bei unbekanntem Spielstand.
std::unique_ptr<SaveFile> openSave(std::vector<uint8_t> data, std::string& err);

} // namespace pw3d
