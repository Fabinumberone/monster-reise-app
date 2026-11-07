// „Alle Spielstände immer aktuell“ (Wunsch des Nutzers 2026-11-07).
//
// Der gemeinsame Speicher ist der Hauptstand; jeder Spielstand ist eine Ansicht davon. Beim Start:
//  1. Alle Spielstände lesen. Für jeden kennt der Speicher, was er zuletzt hineingeschrieben hat
//     (je Pokémon der geschriebene Datensatz, Items, Geld, Fingerabdruck der Datei).
//  2. Weicht der Fingerabdruck ab, wurde dort gespielt: Änderungen übernehmen (Training, Entwicklung,
//     Neufänge, Freilassen, Team, Items und Geld als Differenz). Wurde dasselbe Pokémon in mehreren
//     Spielen verändert, gewinnt der Stand mit mehr EP.
//  3. Spielstände ohne diese Angaben (neu oder aus älteren Speicher-Versionen) werden wie bisher
//     eingelesen (sync.hpp).
//  4. Danach wird jeder Spielstand aus dem Speicher neu geschrieben, wenn er abweicht:
//     - Gen 6/7: Team und Boxen 1:1 (Boxen 1–31/32).
//     - Gen 1–5: Team wie im Speicher; freie Teamplätze mit den passenden Pokémon mit dem höchsten
//       Level aus dem PC („geliehen“, gehen beim Zurücklesen an ihren Platz zurück); PC als nach
//       Pokédex-Nummer sortierte Ansicht aller passenden Pokémon, bis die Boxen voll sind.
//       Umräumen im PC dieser Spiele wird nicht übernommen.
// Die Spielstände selbst schreibt der Aufrufer (data/changed), vorher mit Sicherung.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "bank.hpp"
#include "names.hpp"
#include "species.hpp"

namespace pw3d {

struct WorldSave {
    std::string label;      // „Platin“, „Ultramond (Modul)“
    uint32_t lowId = 0;
    uint8_t media = 1;      // 1 SD/installiert, 2 Modul
    int gen = 6;            // 1–7
    uint8_t version = 0;    // Gen 1–5: Herkunft für neu umgewandelte bzw. spätere Pokémon
    uint8_t language = 5;   // Gen 1/2
    std::vector<uint8_t> data; // gelesen; nach dem Abgleich ggf. der neue Inhalt
    bool ok = false;        // geöffnet und gültig
    bool changed = false;   // data neu, muss geschrieben werden
    bool played = false;    // seit dem letzten Schreiben verändert (dort gespielt)

    // intern
    std::string kind;
    std::unique_ptr<SaveFile> s67;
    SaveGB gb;
    SaveGBA gba;
    SaveNDS nds;
};

struct WorldReport {
    std::vector<std::string> lines;
    int added = 0, updated = 0, released = 0, written = 0;
};

// Protokoll (z. B. Datei auf der SD), wird bei jedem Schritt aufgerufen; nullptr = keins.
using WorldLog = void (*)(const std::string&);
void setWorldLog(WorldLog fn);

// Einlesen und danach alle Spielstände schreiben (siehe oben).
void worldSync(Bank& bank, std::vector<WorldSave>& saves, const SpeciesData& species, const Names& names, WorldReport& rep);
// Nur schreiben (z. B. nach Umräumen oder Entlassen in der App).
void worldWrite(Bank& bank, std::vector<WorldSave>& saves, const SpeciesData& species, const Names& names, WorldReport& rep);

} // namespace pw3d
