// Gemeinsamer Speicher: Team und 32 Boxen, für alle Spiele gleich.
//
// - Jedes Pokémon liegt in voller Gen-7-Form vor, dazu (falls es schon einmal in einem
//   Gen-6-Spiel war) der letzte Gen-6-Stand, damit Gen-6-eigene Felder nicht verloren gehen.
// - Zuordnung über Verschlüsselungskonstante (EC) und PID.
// - abreisen(): Team und Boxen des Spiels werden zum neuen Stand. Was beim Ankommen in diesem
//   Spiel stand und jetzt fehlt, wurde freigelassen. Was das Spiel nicht anzeigen konnte
//   (inGame = false), bleibt erhalten und wird bei Platzkonflikten in einen freien Platz gelegt.
// - ankommen(): Team und Boxen des Zielspiels werden aus dem Speicher geschrieben. Was das
//   Spiel nicht kennt (siehe fitsIn), wird dort nicht angezeigt.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "savefile.hpp"

namespace pw3d {

struct BankEntry {
    bool used = false;
    bool inGame = false;   // beim letzten Ankommen ins aktuelle Spiel geschrieben
    bool hasGen6 = false;  // gen6 enthält den letzten Gen-6-Stand
    PK6 pk;                // Gen-7-Form, Teamformat (0x104)
    PK6 gen6;
};

// Item im gemeinsamen Beutel. written = beim letzten Ankommen ins Spiel geschriebene Anzahl;
// der Rest (count − written) war dort nicht sichtbar und bleibt beim Abreisen erhalten.
struct BankItem {
    uint16_t item = 0;
    uint16_t written = 0;
    uint32_t count = 0;
};

class Bank {
public:
    static constexpr int PARTY = 6, BOXES = 32, SLOTS = 30;

    Trainer6 trainer;
    std::string current; // Spiel, in dem man sich gerade befindet (kindName), leer = keins
    std::array<BankEntry, PARTY> party{};
    std::vector<BankEntry> boxes = std::vector<BankEntry>(BOXES * SLOTS);
    std::vector<BankItem> items; // reisende Taschen (siehe items.hpp)
    uint32_t money = 0;
    bool moneyKnown = false;     // false bei Speichern der Version 1, bis zur nächsten Abreise

    // Items eines weiteren Spielstands dazulegen (unsichtbar im aktuellen Spiel).
    void addItemsFrom(const SaveFile& sav);

    // Neu einrichten: start bestimmt Trainer, Team und Boxen. Pokémon, die es nur in den
    // weiteren Spielständen gibt, kommen ans Team (falls Platz) oder in freie Boxplätze.
    // Doppelte (gleiche EC+PID) werden übersprungen; das Startspiel gewinnt.
    void create(const SaveFile& start, const std::vector<const SaveFile*>& others);

    // Stand aus dem Spiel übernehmen, in dem man gerade ist. Fehlertext bei falschem Spiel.
    bool depart(const SaveFile& sav, std::string& err);

    // In das Zielspiel schreiben (Team, Boxen, Pokédex, Trainer). finalize() macht der Aufrufer.
    // Gibt die Anzahl der nicht angezeigten Pokémon in hidden zurück.
    bool arrive(SaveFile& sav, int& hidden, std::string& err);

    int count() const;

    bool save(const std::string& path, std::string& err) const;
    bool load(const std::string& path, std::string& err);

private:
    BankEntry* find(const PK6& pk);
    BankEntry* freeBoxSlot(std::vector<BankEntry>& bx);
};

} // namespace pw3d
