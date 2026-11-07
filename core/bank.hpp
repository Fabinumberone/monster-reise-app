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

#include "gb.hpp"
#include "gbconvert.hpp"
#include "savefile.hpp"

namespace pw3d {

struct BankEntry {
    bool used = false;
    bool inGame = false;   // beim letzten Ankommen ins aktuelle Spiel geschrieben
    bool hasGen6 = false;  // gen6 enthält den letzten Gen-6-Stand
    bool hasGb = false;    // gb enthält den letzten Gen-1/2-Stand (wie zuletzt geschrieben/gelesen)
    PK6 pk;                // Gen-7-Form, Teamformat (0x104)
    PK6 gen6;
    uint32_t gbKey = 0;    // Schlüssel des Gen-1/2-Stands (TID, DVs, OT-Name)
    GBMon gb;
};

// Item im gemeinsamen Beutel. written = beim letzten Ankommen ins Spiel geschriebene Anzahl;
// der Rest (count − written) war dort nicht sichtbar und bleibt beim Abreisen erhalten.
// Ein Spielstand, den der Speicher kennt (Spiel + Medium) und sein ursprünglicher Trainer.
// Zeigt ein Spielstand später einen anderen Trainer (weder diesen noch den des Speichers),
// wurde dort ein neues Spiel begonnen.
struct BankSlot {
    uint32_t lowId = 0;
    uint8_t media = 0; // 1 SD/installiert, 2 Modul (wie FS_MediaType)
    uint8_t gen = 0;
    Trainer6 trainer;  // bei Gen 1/2: sid = 0
};

// Ergebnis des Einlesens eines Spielstands, in dem man gerade nicht ist.
struct IntegrateResult {
    int added = 0;     // neue Pokémon
    int updated = 0;   // bekannte Pokémon mit mehr EP (dort weitergespielt), nicht im aktuellen Spiel
    int noRoom = 0;    // kein Platz mehr im Speicher
    bool newSave = false; // Spielstand zum ersten Mal gesehen: Items und Geld dazugelegt
};

struct BankItem {
    uint16_t item = 0;
    uint16_t written = 0;
    uint32_t count = 0;
};

class Bank {
public:
    // 100 Boxen: 1–31/32 entsprechen den Boxen der Spiele, der Rest ist nur im Speicher (in der App sichtbar).
    static constexpr int PARTY = 6, BOXES = 100, SLOTS = 30;
    static constexpr int OLD_BOXES = 32; // Dateiversion 1–4

    Trainer6 trainer;
    std::string current; // Spiel, in dem man sich gerade befindet (kindName), leer = keins
    std::array<BankEntry, PARTY> party{};
    std::vector<BankEntry> boxes = std::vector<BankEntry>(BOXES * SLOTS);
    std::vector<BankItem> items; // reisende Taschen (siehe items.hpp)
    uint32_t money = 0;
    bool moneyKnown = false;     // false bei Speichern der Version 1, bis zur nächsten Abreise

    // Version 4
    uint32_t moneyWritten = 0;   // beim Ankommen ins Spiel geschriebenes Geld; der Rest liegt nur im Speicher
    uint32_t currentLow = 0;     // aktueller Spielstand genau (0 = unbekannt, dann nur current)
    uint8_t currentMedia = 0;
    std::vector<BankSlot> slots; // bekannte Spielstände
    std::vector<uint64_t> seen;  // je gesehene Pokémon (EC·2^32 + PID), auch freigelassene
    std::vector<uint64_t> seenGb; // je gesehene Gen-1/2-Pokémon (GB-Schlüssel·2^16 + Familie)
    uint32_t fileVersion = 0;    // Version der zuletzt geladenen Datei (unter 4: Spielstände noch nicht erfasst)
    const SpeciesData* species = nullptr; // für die Familie der Gen-1/2-Pokémon (vom Aufrufer gesetzt)

    bool hasTrainer() const { return !trainer.name.empty(); }

    // Items eines weiteren Spielstands dazulegen (unsichtbar im aktuellen Spiel).
    void addItemsFrom(const SaveFile& sav);

    // Neu einrichten: start bestimmt Trainer, Team und Boxen. Pokémon, die es nur in den
    // weiteren Spielständen gibt, kommen ans Team (falls Platz) oder in freie Boxplätze.
    // Doppelte (gleiche EC+PID) werden übersprungen; das Startspiel gewinnt.
    void create(const SaveFile& start, const std::vector<const SaveFile*>& others);

    // Stand aus dem Spiel übernehmen, in dem man gerade ist. Fehlertext bei falschem Spiel.
    // stay = true: man bleibt im Spiel (Abgleich beim Start der App), der Spielstand gilt danach
    // als gerade geschrieben (wie nach einer Ankunft).
    bool depart(const SaveFile& sav, std::string& err, bool stay = false);

    // Spielstand, in dem man gerade nicht ist: Pokémon, die der Speicher noch nie gesehen hat,
    // kommen dazu (Team, falls Platz, sonst Boxen); eigene Pokémon dieses Spielstands werden auf
    // den Trainer des Speichers umgeschrieben. Bekannte Pokémon mit mehr EP ersetzen den Stand im
    // Speicher, wenn sie nicht gerade im aktuellen Spiel stehen. Bei einem neuen Spielstand
    // werden Items und Geld einmalig dazugelegt.
    // forceNew: Items und Geld auf jeden Fall übernehmen (Spielstand, aus dem ein neuer Speicher den Trainer hat).
    IntegrateResult integrate(const SaveFile& sav, uint32_t lowId, uint8_t media, bool forceNew = false);
    IntegrateResult integrateGB(const SaveGB& sav, const GBContext& ctx, uint32_t lowId, uint8_t media, bool forceNew = false);

    // In das Zielspiel schreiben (Team, Boxen, Pokédex, Trainer). finalize() macht der Aufrufer.
    // Gibt die Anzahl der nicht angezeigten Pokémon in hidden zurück.
    bool arrive(SaveFile& sav, int& hidden, std::string& err);

    // Dasselbe für Gen-1/2-Spiele (Virtual Console). Boxen werden linear abgebildet:
    // GB-Box b, Platz i <-> Speicherplatz b·20+i (zählend über alle Speicherboxen).
    // Items: Gen 1 Beutel (20) + Item-PC (50), Gen 2 Item-Tasche (20), Ball-Tasche (12), Item-PC (50);
    // Reihenfolge beim Ankommen: Bälle, Medizin, Schutz, Rest; höchstens 99 je Item.
    bool departGB(const SaveGB& sav, const GBContext& ctx, std::string& err, bool stay = false);
    // Pokémon und Items eines GB-Spielstands, in dem man (noch) nicht ist, einmalig dazulegen
    // (unsichtbar im aktuellen Spiel; Team, falls Platz, sonst freie Boxplätze). Gibt die Anzahl
    // neu aufgenommener Pokémon zurück.
    int addFromGB(const SaveGB& sav, const GBContext& ctx);
    bool arriveGB(SaveGB& sav, const GBContext& ctx, int& hidden, std::string& err);

    // Bekannter Spielstand; record legt ihn an (Trainer wie im Spielstand), ohne Items zu übernehmen.
    BankSlot* slot(uint32_t lowId, uint8_t media);
    void record(uint32_t lowId, uint8_t media, int gen, const Trainer6& t);

    // Nach dem Abgleich beim Start: abreisen, ohne den Spielstand erneut zu lesen (der Speicher
    // enthält ihn bereits, dazu Umräumen in der App).
    void leave();

    int count() const;

    std::vector<uint8_t> serialize() const;
    bool save(const std::string& path, std::string& err) const;
    bool load(const std::string& path, std::string& err);

private:
    BankEntry* find(const PK6& pk);
    bool ownTrainer(uint16_t tid, uint16_t sid, const std::string& name, bool gb) const;
    uint16_t familyOf(uint16_t s) const { return species ? species->family(s) : s; }
    void remember(); // alle Pokémon im Speicher als gesehen merken
    BankEntry* freeBoxSlot(std::vector<BankEntry>& bx);
};

} // namespace pw3d
