// Monster Reise App – Reise-App mit grafischer Oberfläche (citro2d, Systemschrift).
// Oben: Kopf mit Trainer und Region, darunter Team, PC oder Beutel.
// Unten: Touch-Menü, Regionswahl und Reiseablauf.
// Tasten: Steuerkreuz bewegt die Auswahl oben (Team/PC/Beutel), L/R wechselt die Box,
//         A nimmt ein Pokémon auf bzw. legt es ab (tauscht), B bricht ab,
//         Y wechselt die Ansicht, X öffnet „Reisen“, START beendet.
#include <3ds.h>
#include <citro2d.h>

#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "bank.hpp"
#include "bytes.hpp"
#include "games.hpp"
#include "gb.hpp"
#include "gba.hpp"
#include "gbaconvert.hpp"
#include "gbconvert.hpp"
#include "nds.hpp"
#include "ndsconvert.hpp"
#include "icons.hpp"
#include "updater.hpp"
#include "species.hpp"
#include "items.hpp"
#include "names.hpp"
#include "savefile.hpp"
#include "sync.hpp"
#include "world.hpp"

#include <dirent.h>

using namespace pw3d;

namespace {

const char* DIR = "sdmc:/pokemon-world-3d";
const char* BANK = "sdmc:/pokemon-world-3d/speicher.bin";
const char* LAST = "sdmc:/pokemon-world-3d/zuletzt.txt";
const char* BACKUPS = "sdmc:/pokemon-world-3d/sicherungen";
const char* NAMES = "sdmc:/pokemon-world-3d/namen.txt";
const char* ICONS = "sdmc:/pokemon-world-3d/icons.bin";
const char* SPECIES = "sdmc:/pokemon-world-3d/arten.bin";

// ---------- Farben ----------
const u32 C_BG = C2D_Color32(0x1E, 0x2A, 0x3A, 0xFF);
const u32 C_BG2 = C2D_Color32(0x27, 0x37, 0x4B, 0xFF);
const u32 C_HEAD = C2D_Color32(0x2E, 0x86, 0xAB, 0xFF);
const u32 C_CARD = C2D_Color32(0xF4, 0xF1, 0xEA, 0xFF);
const u32 C_CARD_DIM = C2D_Color32(0x8A, 0x92, 0x9C, 0xFF);
const u32 C_TEXT = C2D_Color32(0x22, 0x22, 0x22, 0xFF);
const u32 C_MUTED = C2D_Color32(0x6B, 0x72, 0x7B, 0xFF);
const u32 C_WHITE = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 C_LIGHT = C2D_Color32(0xB9, 0xC6, 0xD3, 0xFF);
const u32 C_SEL = C2D_Color32(0xFF, 0xC8, 0x3D, 0xFF);
const u32 C_OK = C2D_Color32(0x3F, 0xB5, 0x5A, 0xFF);
const u32 C_ERR = C2D_Color32(0xD9, 0x4A, 0x3A, 0xFF);
const u32 C_MALE = C2D_Color32(0x3B, 0x6F, 0xD9, 0xFF);
const u32 C_FEMALE = C2D_Color32(0xD9, 0x4A, 0x7A, 0xFF);

u32 regionColor(const char* region) {
    std::string r = region;
    if (r == "Kanto") return C2D_Color32(0x4C, 0xA6, 0x4C, 0xFF);
    if (r == "Johto") return C2D_Color32(0x8A, 0x5C, 0xC8, 0xFF);
    if (r == "Hoenn") return C2D_Color32(0xD9, 0x4A, 0x3A, 0xFF);
    if (r == "Sinnoh") return C2D_Color32(0x5A, 0x7D, 0x9A, 0xFF);
    if (r == "Einall") return C2D_Color32(0x55, 0x55, 0x5F, 0xFF);
    if (r == "Kalos") return C2D_Color32(0x3B, 0x6F, 0xD9, 0xFF);
    return C2D_Color32(0xE0, 0xA8, 0x22, 0xFF); // Alola
}

// ---------- Zeichnen ----------
C2D_TextBuf g_buf;
Names g_names;
IconSet g_icons;
SpeciesData g_species;

enum Align { LEFT = C2D_AlignLeft, CENTER = C2D_AlignCenter, RIGHT = C2D_AlignRight };

// Text zeichnen; wird bei maxW > 0 horizontal gestaucht, damit er passt.
void text(const std::string& s, float x, float y, float size, u32 color, Align a = LEFT, float maxW = 0) {
    C2D_Text t;
    C2D_TextParse(&t, g_buf, s.c_str());
    C2D_TextOptimize(&t);
    float sx = size;
    if (maxW > 0) {
        float w, h;
        C2D_TextGetDimensions(&t, size, size, &w, &h);
        if (w > maxW) sx = size * maxW / w;
    }
    C2D_DrawText(&t, C2D_WithColor | a, x, y, 0.5f, sx, size, color);
}

void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); }

void frame(float x, float y, float w, float h, u32 c, float t = 2) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y, t, h, c);
    rect(x + w - t, y, t, h, c);
}

// Symbol des Pokémon (oder Platzhalterkreis), grau getönt, wenn hier nicht sichtbar.
void drawIcon(const PK6& pk, float x, float y, float scale, bool dim) {
    const C2D_Image* img = pk.isEgg() ? nullptr : g_icons.get(pk.species(), pk.form(), pk.gender());
    if (img) {
        C2D_ImageTint tint;
        C2D_PlainImageTint(&tint, C2D_Color32(0x40, 0x40, 0x40, 0xFF), dim ? 0.6f : 0.0f);
        C2D_DrawImageAt(*img, x, y, 0.5f, &tint, scale, scale);
    } else {
        float r = 12 * scale;
        C2D_DrawCircleSolid(x + 20 * scale, y + 15 * scale, 0.5f, r, dim ? C_CARD_DIM : C2D_Color32(0xD9, 0x4A, 0x3A, 0xFF));
        C2D_DrawCircleSolid(x + 20 * scale, y + 15 * scale, 0.5f, r * 0.35f, C_WHITE);
    }
}

// ---------- Zustand ----------
enum class Screen { Menu, Info, SaveMoves, Release, Dupes, Travel, Confirm, Busy, Done, Error, Update, Backups };

// Ein Sicherungsstand: alle Dateien mit derselben Zeit (ein Abgleich bzw. ein Schreibvorgang)
struct BackupSet {
    std::string stamp;               // „20261108-102205“
    std::vector<std::string> files;  // Dateinamen in sicherungen/
};
enum class View { Team, PC, Bag, List };

// Liste: alle Pokémon nach Nationaldex-Nummer gruppiert (Positionen wie State::held).
struct ListGroup {
    u16 species = 0;
    std::vector<int> pos;
};

struct State {
    Bank bank;   // gemeinsamer Speicher (wie gespeichert)
    bool bankOk = false, loading = true;
    bool synced = false;  // aktueller Spielstand beim Start übernommen (Reisen ohne erneutes Lesen)
    bool dirty = false;   // in der App umgeräumt, noch nicht ins Spiel übernommen
    int held = -1;        // aufgenommenes Pokémon: 0–5 Team, ab 6 Boxplatz (6 + Index)
    int teamCursor = 0;
    std::vector<ListGroup> list;  // für die Liste, neu aufgebaut beim Öffnen und nach Änderungen
    int listSel = 0, listScroll = 0;      // Arten
    int listOpen = -1, entrySel = 0;      // geöffnete Art und gewähltes Pokémon darin
    std::set<int> marked;                 // zum Entlassen markiert (Positionen, über Arten hinweg)
    bool dupeShiny = false;               // „Doppelte“: schillernde Pokémon einbeziehen
    std::string updVersion;              // neueste Version auf GitHub
    bool updDone = false;                 // Update installiert
    bool updStartup = false;              // Prüfung beim Start (vor dem Auslesen)
    std::vector<BackupSet> backups;       // Sicherungen, neueste zuerst
    int bkSel = 0, bkScroll = 0, bkStep = 0; // 0 Liste, 1 Frage, 2 zweite Bestätigung, 3 erledigt
    std::vector<std::string> bkResult;
    int releaseStep = 0;                  // Entlassen: 0 erste Frage, 1 zweite Bestätigung (mit Y)
    std::string bankErr;
    std::vector<SaveSlot> installed;
    std::vector<WorldSave> saves;       // je installed[i] der gelesene Spielstand
    const SaveSlot* current = nullptr;  // zuletzt gestartetes Spiel (für „hier sichtbar“), kann fehlen
    const SaveSlot* target = nullptr;
    std::vector<std::string> notice;    // Meldungen des Abgleichs beim Start
    int travelRegion = -1, gameScroll = 0; // Spielauswahl: gewählte Region, Blättern in der Liste
    Screen screen = Screen::Menu;
    View view = View::Team;
    int box = 0, cursor = 0, bagScroll = 0;
    std::vector<std::string> steps;
    std::string message;
    bool quit = false;
};

u32 readLast() {
    FILE* f = std::fopen(LAST, "r");
    if (!f) return 0;
    unsigned long v = 0;
    if (std::fscanf(f, "%lx", &v) != 1) v = 0;
    std::fclose(f);
    return u32(v);
}

void writeLast(u32 lowId) {
    FILE* f = std::fopen(LAST, "w");
    if (!f) return;
    std::fprintf(f, "%08lx\n", (unsigned long)lowId);
    std::fclose(f);
}

bool readFileAll(const char* path, std::vector<u8>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    out.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

void timeStamp(char* buf, size_t n) {
    std::time_t now = std::time(nullptr);
    std::strftime(buf, n, "%Y%m%d-%H%M%S", std::localtime(&now));
}

// Protokoll des letzten Starts (sdmc:/pokemon-world-3d/protokoll.txt), Zeile für Zeile geschrieben,
// damit nach einem Absturz sichtbar ist, wie weit die App kam.
const char* LOGFILE = "sdmc:/pokemon-world-3d/protokoll.txt";
void logLine(const std::string& s) {
    if (FILE* f = std::fopen(LOGFILE, "a")) {
        std::fputs(s.c_str(), f);
        std::fputc(10, f);
        std::fclose(f);
    }
}

// Sicherungen: höchstens KEEP je Spielstand (Dateiname „<Zeit>-<was>“, nach Zeit sortiert).
constexpr int KEEP = 10;
void pruneBackups(const std::string& what) {
    std::vector<std::string> names;
    if (::DIR* d = opendir(BACKUPS)) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > what.size() + 16 && n.compare(n.size() - what.size(), what.size(), what) == 0 && n[15] == '-') names.push_back(n);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i + KEEP < names.size(); i++) std::remove((std::string(BACKUPS) + "/" + names[i]).c_str());
}

constexpr int BK_ROWS = 6;

// „20261108-102205“ -> „08.11.2026 10:22:05“
std::string stampLabel(const std::string& s) {
    if (s.size() < 15) return s;
    return s.substr(6, 2) + "." + s.substr(4, 2) + "." + s.substr(0, 4) + " " + s.substr(9, 2) + ":" + s.substr(11, 2) + ":" + s.substr(13, 2);
}
// Was gesichert wurde („Platin-005a Pokemon Platin (DE).sav“, „speicher.bin“)
std::string backupWhat(const std::string& file) { return file.size() > 16 ? file.substr(16) : file; }

bool backupFile(const char* stamp, const std::string& what, const std::vector<u8>& data) {
    std::string p = std::string(BACKUPS) + "/" + stamp + "-" + what;
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    pruneBackups(what);
    return ok;
}

void render(const struct State& st);

// Abgleich (sync = true: erst Änderungen aus gespielten Spielständen übernehmen) und alle
// Spielstände schreiben, die vom Speicher abweichen. Vorher wird jeder gesichert.
void commit(State& st, bool sync) {
    std::vector<std::vector<u8>> before;
    for (auto& w : st.saves) before.push_back(w.data);
    std::vector<u8> bankBefore;
    bool hadBank = readFileAll(BANK, bankBefore);
    WorldReport rep;
    if (sync) worldSync(st.bank, st.saves, g_species, g_names, rep);
    else worldWrite(st.bank, st.saves, g_species, g_names, rep);
    if (!st.bank.hasTrainer()) {
        st.bankOk = false;
        st.bankErr = "Keine Spielstände gefunden";
        return;
    }
    st.bankOk = true;
    mkdir(DIR, 0777);
    mkdir(BACKUPS, 0777);
    char stamp[32];
    timeStamp(stamp, sizeof stamp);
    int written = 0;
    for (size_t i = 0; i < st.saves.size(); i++) {
        WorldSave& w = st.saves[i];
        if (!w.changed) continue;
        std::string err;
        const SaveSlot& slot = st.installed[i];
        logLine("speichere " + slot.label());
        if (!backupFile(stamp, slot.label() + "-" + saveName(*slot.game), before[i]) || !writeSave(slot, w.data, err)) {
            // nicht geschrieben: beim nächsten Start wie neu einlesen, damit nichts verloren geht
            if (BankSlot* bs = st.bank.slot(w.lowId, w.media)) bs->synced = false;
            w.data = before[i];
            rep.lines.push_back(slot.label() + ": konnte nicht geschrieben werden " + err);
            continue;
        }
        w.changed = false;
        written++;
    }
    std::string err;
    if (hadBank) backupFile(stamp, "speicher.bin", bankBefore);
    if (!st.bank.save(BANK, err)) rep.lines.push_back("Speicher nicht gespeichert: " + err);
    st.dirty = false;
    st.held = -1;
    st.notice = rep.lines;
    if (rep.added || rep.updated || rep.released || written) {
        std::string l = std::to_string(written) + " Spielstände aktualisiert";
        if (rep.added) l += ", " + std::to_string(rep.added) + " neu";
        if (rep.updated) l += ", " + std::to_string(rep.updated) + " verändert";
        if (rep.released) l += ", " + std::to_string(rep.released) + " freigelassen";
        st.notice.push_back(l);
    }
    if (rep.missing) {
        st.notice.push_back("Fehlende Pokémon (Pension, Resort, Kampfbox …) bleiben im Speicher,");
        st.notice.push_back("bis sie wieder auftauchen. Freigelassen? Dann in der Liste entlassen.");
    }
}

// Beim Start: alle Spielstände lesen, Änderungen übernehmen, alle aktualisieren (world.hpp).
void loadState(State& st) {
    mkdir(DIR, 0777);
    setWorldLog(logLine);
    logLine("Start");
    g_names.load(NAMES);
    g_species.load(SPECIES);
    g_icons.load(ICONS);
    st.installed = findSaves();
    logLine(cartInfo());
    st.current = nullptr;
    st.bank = Bank{};
    st.bank.species = &g_species;
    std::vector<u8> oldFile;
    if (readFileAll(BANK, oldFile)) {
        st.bankOk = st.bank.load(BANK, st.bankErr);
        st.bank.species = &g_species;
        if (!st.bankOk) { st.loading = false; return; } // beschädigt: nichts anfassen
    }
    st.saves.clear();
    for (auto& s : st.installed) {
        WorldSave w;
        w.label = s.label();
        w.lowId = s.game->lowId;
        w.media = u8(s.media);
        w.gen = s.game->gen;
        w.version = s.game->gbVersion;
        w.language = s.game->language;
        std::string err;
        if (!readSave(s, w.data, err)) st.notice.push_back(err);
        logLine("gelesen: " + w.label + " (" + std::to_string(w.data.size()) + " Byte)");
        st.saves.push_back(std::move(w));
    }
    commit(st, true);
    logLine("fertig");
    // Zuletzt gestartetes Spiel (für die Anzeige „hier sichtbar“)
    u32 last = readLast();
    for (auto& s : st.installed)
        if (s.game->lowId == last && (!st.current || !s.cart())) st.current = &s;
    st.synced = true;
    st.loading = false;
    if (!st.notice.empty()) st.screen = Screen::Info;
}

// Ist das Pokémon im aktuellen Spiel sichtbar?
bool visibleHere(const State& st, const PK6& pk) {
    if (!st.current) return true;
    if (st.current->game->gen == 4 || st.current->game->gen == 5) return fitsGen45(pk, NDSGame(st.current->game->sub));
    if (st.current->game->gen == 3) return fitsGen3(pk);
    if (st.current->game->gen <= 2) return fitsGB(pk, st.current->game->gen);
    return fitsIn(pk, st.current->game->kind);
}

// Boxen des Spiels (Gen 1/2: Plätze zu je 20, linear über die Speicherboxen verteilt).
int gameBoxes(const Game& g) {
    if (g.gen == 1) return 12;
    if (g.gen == 2 || g.gen == 3) return 14;
    if (g.gen == 4) return 18;
    if (g.gen == 5) return 24;
    return (g.kind == GameKind::XY || g.kind == GameKind::ORAS) ? 31 : 32;
}

// Liegt dieser Boxplatz (Index in bank.boxes) im aktuellen Spiel in einer Box?
bool slotVisibleHere(const State& st, int idx) {
    if (!st.current) return true;
    const Game& g = *st.current->game;
    if (g.gen <= 2) return idx < gameBoxes(g) * 20;
    return idx / Bank::SLOTS < gameBoxes(g);
}

BankEntry& entryAt(State& st, int pos) { return pos < Bank::PARTY ? st.bank.party[size_t(pos)] : st.bank.boxes[size_t(pos - Bank::PARTY)]; }
const BankEntry& entryAt(const State& st, int pos) { return pos < Bank::PARTY ? st.bank.party[size_t(pos)] : st.bank.boxes[size_t(pos - Bank::PARTY)]; }

void buildList(State& st) {
    st.list.clear();
    auto add = [&](int pos) {
        const BankEntry& e = entryAt(st, pos);
        if (!e.used) return;
        u16 sp = e.pk.isEgg() ? 0 : e.pk.species(); // Eier zuerst
        for (auto& g : st.list)
            if (g.species == sp) { g.pos.push_back(pos); return; }
        st.list.push_back({sp, {pos}});
    };
    for (int i = 0; i < Bank::PARTY; i++) add(i);
    for (int i = 0; i < Bank::BOXES * Bank::SLOTS; i++) add(Bank::PARTY + i);
    std::sort(st.list.begin(), st.list.end(), [](const ListGroup& a, const ListGroup& b) { return a.species < b.species; });
    st.listSel = std::min(st.listSel, std::max(0, int(st.list.size()) - 1));
    if (st.listOpen >= int(st.list.size())) st.listOpen = -1;
    if (st.listOpen >= 0) st.entrySel = std::min(st.entrySel, int(st.list[size_t(st.listOpen)].pos.size()) - 1);
}

std::string gameTitle(const Game& g);

// Name des Spielstands, in dem ein Pokémon fehlt
std::string awayLabel(const State& st, const BankEntry& e) {
    for (auto& s : st.installed)
        if (s.game->lowId == e.awayLow && u8(s.media) == e.awayMedia) return gameTitle(*s.game) + (s.cart() ? " (Modul)" : "");
    return "einem Spielstand";
}

std::string placeName(const State& st, int pos) {
    if (entryAt(st, pos).away()) return "fehlt in " + awayLabel(st, entryAt(st, pos)) + " (Pension o. Ä.?)";
    if (pos < Bank::PARTY) return "Team " + std::to_string(pos + 1);
    int idx = pos - Bank::PARTY;
    std::string p = "Box " + std::to_string(idx / Bank::SLOTS + 1) + ", Platz " + std::to_string(idx % Bank::SLOTS + 1);
    if (!slotVisibleHere(st, idx)) p += " (nur im Speicher)";
    return p;
}

const char* gameOfVersion(u8 v) {
    switch (v) {
    case 1: return "Saphir"; case 2: return "Rubin"; case 3: return "Smaragd"; case 4: return "Feuerrot"; case 5: return "Blattgrün";
    case 7: return "HeartGold"; case 8: return "SoulSilver"; case 10: return "Diamant"; case 11: return "Perl"; case 12: return "Platin";
    case 15: return "Colosseum/XD"; case 20: return "Weiß"; case 21: return "Schwarz"; case 22: return "Weiß 2"; case 23: return "Schwarz 2";
    case 24: return "X"; case 25: return "Y"; case 26: return "Alpha Saphir"; case 27: return "Omega Rubin";
    case 30: return "Sonne"; case 31: return "Mond"; case 32: return "Ultrasonne"; case 33: return "Ultramond";
    case 35: return "Rot"; case 36: return "Grün"; case 37: return "Blau"; case 38: return "Gelb";
    case 39: return "Gold"; case 40: return "Silber"; case 41: return "Kristall";
    default: return "?";
    }
}

// Wo liegt das Item im aktuellen Spiel? nullptr = dort nicht sichtbar.
const char* itemPlaceHere(const State& st, uint16_t item) {
    if (!st.current) return "";
    if (st.current->game->gen == 4 || st.current->game->gen == 5) {
        if (pouchOf(GameKind::USUM, item) == Pouch::None) return nullptr;
        switch (ndsTravelPouch(NDSGame(st.current->game->sub), item)) {
        case 0: return "Items";
        case 4: return "Medizin";
        case 5: return "Beeren";
        case 6: return "Bälle";
        case 7: return "Kampf-Items";
        default: return nullptr;
        }
    }
    if (st.current->game->gen == 3) {
        uint16_t g3 = modernItemToGen3(item);
        if (!g3 || pouchOf(GameKind::USUM, item) == Pouch::None) return nullptr;
        return g3 <= 12 ? "Ball-Tasche" : (g3 >= 133 && g3 <= 175) ? "Beeren-Tasche" : "Items";
    }
    if (st.current->game->gen == 1) return modernItemToGen1(item) ? "Beutel" : nullptr;
    if (st.current->game->gen == 2) {
        if (!modernItemToGen2(item) || pouchOf(GameKind::USUM, item) == Pouch::None) return nullptr;
        return (item <= 16 || (item >= 492 && item <= 500)) ? "Ball-Tasche" : "Item-Tasche";
    }
    Pouch p = pouchOf(st.current->game->kind, item);
    return p == Pouch::None ? nullptr : pouchName(p);
}

// Level aus den EP (Box-Pokémon aus Gen 6/7 haben keine Teamwerte)
int levelOf(const PK6& pk) { return levelForExp(g_species.get(pk.species()).growth, pk.exp()); }

std::string monName(const PK6& pk) {
    if (pk.isEgg()) return "Ei";
    std::string art = g_names.species(pk.species());
    std::string nick = pk.nickname();
    if (pk.isNicknamed() && !nick.empty() && nick != art) return nick;
    return art;
}

std::string money(u32 v) { // 99999 -> 99.999
    std::string s = std::to_string(v);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ".");
    return s;
}

// ---------- Oberer Bildschirm ----------
void drawHeader(const State& st) {
    rect(0, 0, 400, 26, C_HEAD);
    text("Monster Reise App", 8, 4, 0.6f, C_WHITE);
    text("v" APP_VERSION, 166, 8, 0.4f, C_LIGHT);
    if (st.current) {
        u32 rc = regionColor(st.current->game->region);
        std::string r = std::string(st.current->game->region) + " · " + st.current->label();
        rect(250, 4, 144, 18, rc);
        text(r, 322, 5, 0.45f, C_WHITE, CENTER, 136);
    }
    rect(0, 26, 400, 18, C_BG2);
    if (st.bankOk) {
        const Trainer6& t = st.bank.trainer;
        text(t.name + "   ID " + std::to_string(t.tid) + "   Geld " + money(st.bank.money), 8, 28, 0.45f, C_LIGHT);
        text(std::to_string(st.bank.count()) + " Pokémon", 392, 28, 0.45f, C_LIGHT, RIGHT);
    }
}

void drawTeam(const State& st) {
    for (int i = 0; i < Bank::PARTY; i++) {
        const BankEntry& e = st.bank.party[size_t(i)];
        float x = 8 + (i % 2) * 196, y = 50 + (i / 2) * 62;
        if (!e.used) {
            rect(x, y, 188, 58, C_BG2);
            text("leer", x + 94, y + 20, 0.45f, C_LIGHT, CENTER);
        } else {
            bool vis = (!e.away() && visibleHere(st, e.pk));
            rect(x, y, 188, 58, vis ? C_CARD : C_CARD_DIM);
            drawIcon(e.pk, x + 2, y + 4, 1.0f, !vis);
            text(monName(e.pk), x + 46, y + 3, 0.52f, C_TEXT, LEFT, 110);
            if (!e.pk.isEgg()) {
                text("Lv. " + std::to_string(levelOf(e.pk)), x + 46, y + 21, 0.42f, C_MUTED);
                if (e.pk.gender() < 2)
                    text(e.pk.gender() == 0 ? "♂" : "♀", x + 180, y + 3, 0.55f, e.pk.gender() == 0 ? C_MALE : C_FEMALE, RIGHT);
                if (e.pk.isShiny()) text("★", x + 162, y + 3, 0.5f, C_SEL, RIGHT);
            }
            if (!vis) {
                text("hier nicht sichtbar", x + 46, y + 38, 0.42f, C_TEXT);
            } else {
                u16 hp = rd16(e.pk.d.data() + 0xF0), maxHp = rd16(e.pk.d.data() + 0xF2);
                float fr = maxHp ? std::min(1.0f, float(hp) / maxHp) : 0;
                u32 hc = fr > 0.5f ? C_OK : fr > 0.2f ? C_SEL : C_ERR;
                rect(x + 46, y + 42, 100, 8, C2D_Color32(0xD8, 0xD3, 0xC8, 0xFF));
                rect(x + 46, y + 42, 100 * fr, 8, hc);
                text(std::to_string(hp) + "/" + std::to_string(maxHp), x + 182, y + 38, 0.4f, C_MUTED, RIGHT);
            }
        }
        if (st.held == i) frame(x, y, 188, 58, C_ERR, 3);
        if (i == st.teamCursor) frame(x, y, 188, 58, C_SEL);
    }
}

void drawPC(const State& st) {
    text("◀  Box " + std::to_string(st.box + 1) + "  ▶", 146, 48, 0.55f, C_WHITE, CENTER);
    bool boxHidden = st.current && !slotVisibleHere(st, st.box * Bank::SLOTS);
    text(boxHidden ? "nur im Speicher (L / R)" : "L / R", 146, 66, 0.38f, boxHidden ? C_SEL : C_LIGHT, CENTER);
    for (int s = 0; s < Bank::SLOTS; s++) {
        const BankEntry& e = st.bank.boxes[st.box * Bank::SLOTS + s];
        float x = 8 + (s % 6) * 46, y = 82 + (s / 6) * 31;
        rect(x, y, 44, 29, C_BG2);
        int idx = st.box * Bank::SLOTS + s;
        if (e.used) drawIcon(e.pk, x + 2, y - 1, 1.0f, !(!e.away() && visibleHere(st, e.pk)) || !slotVisibleHere(st, idx));
        if (st.held == Bank::PARTY + idx) frame(x, y, 44, 29, C_ERR, 3);
        if (s == st.cursor) frame(x, y, 44, 29, C_SEL);
    }
    // Detailansicht rechts
    const BankEntry& sel = st.bank.boxes[st.box * Bank::SLOTS + st.cursor];
    rect(290, 48, 102, 188, C_CARD);
    if (!sel.used) {
        text("leer", 341, 130, 0.5f, C_MUTED, CENTER);
        return;
    }
    bool vis = (!sel.away() && visibleHere(st, sel.pk)) && slotVisibleHere(st, st.box * Bank::SLOTS + st.cursor);
    drawIcon(sel.pk, 301, 52, 2.0f, !vis);
    text(monName(sel.pk), 341, 114, 0.5f, C_TEXT, CENTER, 96);
    if (!sel.pk.isEgg() && g_names.species(sel.pk.species()) != monName(sel.pk))
        text(g_names.species(sel.pk.species()), 341, 130, 0.4f, C_MUTED, CENTER, 96);
    float y = 148;
    for (u16 m : sel.pk.moves())
        if (m) { text(g_names.move(m), 296, y, 0.4f, C_TEXT, LEFT, 92); y += 14; }
    if (sel.away()) text("fehlt in " + awayLabel(st, sel), 341, 218, 0.36f, C_ERR, CENTER, 96);
    else if (!vis) text("hier nicht sichtbar", 341, 218, 0.38f, C_ERR, CENTER, 96);
}

constexpr int BAG_ROWS = 10;

void drawBag(const State& st) {
    text("Beutel", 8, 48, 0.55f, C_WHITE);
    text("nur Taschen, die mitreisen", 392, 50, 0.4f, C_LIGHT, RIGHT);
    if (st.bank.items.empty()) { text("leer", 200, 130, 0.6f, C_LIGHT, CENTER); return; }
    for (int r = 0; r < BAG_ROWS; r++) {
        int i = st.bagScroll + r;
        if (i >= int(st.bank.items.size())) break;
        const BankItem& b = st.bank.items[size_t(i)];
        const char* place = itemPlaceHere(st, b.item);
        float y = 70 + r * 17;
        rect(8, y, 384, 16, r % 2 ? C_BG2 : C_BG);
        u32 c = place ? C_WHITE : C_CARD_DIM;
        text(g_names.item(b.item), 14, y + 1, 0.45f, c, LEFT, 210);
        text("× " + std::to_string(b.count), 280, y + 1, 0.45f, c, RIGHT);
        text(place ? place : "hier nicht sichtbar", 386, y + 2, 0.4f, place ? C_LIGHT : C_ERR, RIGHT);
    }
    if (st.bank.items.size() > size_t(BAG_ROWS))
        text("▲▼ blättern", 392, 242 - 18, 0.38f, C_LIGHT, RIGHT);
}

constexpr int LIST_ROWS = 9;

void drawList(const State& st) {
    if (st.listOpen < 0) {
        text("Liste nach Pokédex-Nummer", 8, 48, 0.5f, C_WHITE);
        text(std::to_string(st.list.size()) + " Arten", 392, 50, 0.4f, C_LIGHT, RIGHT);
        for (int r = 0; r < LIST_ROWS; r++) {
            int i = st.listScroll + r;
            if (i >= int(st.list.size())) break;
            const ListGroup& g = st.list[size_t(i)];
            const BankEntry& e = entryAt(st, g.pos[0]);
            float y = 68 + r * 17;
            rect(8, y, 384, 16, i == st.listSel ? C_SEL : (r % 2 ? C_BG2 : C_BG));
            u32 c = i == st.listSel ? C_TEXT : C_WHITE;
            char nr[8];
            std::snprintf(nr, sizeof nr, "%03u", unsigned(g.species));
            text(g.species ? nr : "Ei", 14, y + 1, 0.42f, c);
            text(g.species ? g_names.species(g.species) : std::string("Eier"), 52, y + 1, 0.42f, c, LEFT, 200);
            (void)e;
            if (g.pos.size() > 1) text("× " + std::to_string(g.pos.size()), 386, y + 1, 0.42f, i == st.listSel ? C_TEXT : C_SEL, RIGHT);
            int mk = 0;
            for (int p : g.pos) mk += int(st.marked.count(p));
            if (mk) text(std::to_string(mk) + " markiert", 330, y + 1, 0.4f, C_ERR, RIGHT);
        }
        if (st.marked.empty()) text("A: öffnen   ▲▼ ◀▶ blättern   SELECT: Doppelte", 392, 226, 0.36f, C_LIGHT, RIGHT, 384);
        else text("A: öffnen   SELECT: " + std::to_string(st.marked.size()) + " markierte entlassen   X: Markierungen löschen", 392, 226, 0.36f, C_ERR, RIGHT, 384);
        return;
    }
    const ListGroup& g = st.list[size_t(st.listOpen)];
    char nr[8];
    std::snprintf(nr, sizeof nr, "%03u", unsigned(g.species));
    text(std::string(g.species ? nr : "") + " " + (g.species ? g_names.species(g.species) : std::string("Eier")) + "  ×" +
             std::to_string(g.pos.size()), 8, 48, 0.5f, C_WHITE, LEFT, 384);
    int first = std::max(0, std::min(st.entrySel - 2, int(g.pos.size()) - 4));
    for (int r = 0; r < 4 && first + r < int(g.pos.size()); r++) {
        int i = first + r;
        const BankEntry& e = entryAt(st, g.pos[size_t(i)]);
        float y = 68 + r * 38;
        bool sel = i == st.entrySel;
        bool mk = st.marked.count(g.pos[size_t(i)]) > 0;
        rect(8, y, 384, 36, sel ? C_CARD : C_BG2);
        if (mk) rect(8, y, 5, 36, C_ERR);
        u32 c = sel ? C_TEXT : C_WHITE, m = sel ? C_MUTED : C_LIGHT;
        drawIcon(e.pk, 10, y + 3, 1.0f, false);
        std::string name = monName(e.pk);
        if (e.pk.isShiny()) name += " ★";
        text(name, 54, y + 2, 0.45f, c, LEFT, 150);
        if (!e.pk.isEgg()) text("Lv. " + std::to_string(levelOf(e.pk)), 210, y + 2, 0.42f, c);
        text(std::string("aus ") + gameOfVersion(e.pk.version()), 386, y + 2, 0.4f, m, RIGHT, 120);
        text(placeName(st, g.pos[size_t(i)]), 54, y + 19, 0.38f, m, LEFT, 220);
        text(mk ? std::string("wird entlassen") : "OT " + e.pk.otName(), 386, y + 19, 0.38f, mk ? C_ERR : m, RIGHT, 110);
    }
    if (st.marked.empty()) text("A: zum Entlassen markieren   B: zurück", 392, 226, 0.38f, C_LIGHT, RIGHT);
    else text("A: markieren   SELECT: " + std::to_string(st.marked.size()) + " markierte entlassen   B: zurück", 392, 226, 0.36f, C_ERR, RIGHT, 384);
}

void drawTop(const State& st) {
    drawHeader(st);
    if (st.loading) {
        text(st.updStartup ? "Suche nach Updates …" : "Lese Spielstände …", 200, 110, 0.6f, C_LIGHT, CENTER);
        return;
    }
    if (!st.bankOk) {
        text("Gemeinsamer Speicher nicht nutzbar:", 200, 100, 0.55f, C_ERR, CENTER);
        text(st.bankErr, 200, 124, 0.45f, C_LIGHT, CENTER, 380);
        return;
    }
    switch (st.view) {
    case View::Team: drawTeam(st); break;
    case View::PC: drawPC(st); break;
    case View::Bag: drawBag(st); break;
    case View::List: drawList(st); break;
    }
    if (st.held >= 0 && (st.view == View::Team || st.view == View::PC)) {
        rect(0, 222, 400, 18, C_HEAD);
        text("Aufgenommen: " + monName(entryAt(st, st.held).pk) + "  –  A legt ab (tauscht), B bricht ab", 200, 223, 0.42f, C_WHITE, CENTER, 392);
    } else if (!g_icons.loaded() || !g_names.loaded())
        text(std::string(!g_icons.loaded() ? "icons.bin" : "namen.txt") + " fehlt auf der SD", 392, 226, 0.36f, C_ERR, RIGHT);
}

std::vector<int> duplicates(const State& st);

// ---------- Unterer Bildschirm ----------
struct Button {
    float x, y, w, h;
    bool hit(const touchPosition& t) const { return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h; }
};

void button(const Button& b, const std::string& label, u32 bg, u32 fg, float size = 0.6f) {
    rect(b.x, b.y, b.w, b.h, bg);
    text(label, b.x + b.w / 2, b.y + b.h / 2 - size * 15, size, fg, CENTER, b.w - 8);
}

const Button B_TRAVEL{8, 32, 304, 44}, B_TEAM{8, 82, 148, 42}, B_PC{164, 82, 148, 42}, B_BAG{8, 128, 148, 42}, B_LIST{164, 128, 148, 42};
const Button B_EXIT{216, 196, 96, 36}, B_UPDATE{8, 196, 96, 36}, B_BACKUPS{112, 196, 96, 36}, B_BACK{8, 200, 304, 32};
const Button B_YES{8, 186, 148, 46}, B_NO{164, 186, 148, 46};
const Button B_SHINY{8, 140, 304, 36};
const Button B_YES2{164, 186, 148, 46}, B_NO2{8, 186, 148, 46}; // zweite Bestätigung: Seiten getauscht

// Spielauswahl in zwei Stufen: erst die Region (große Knöpfe), dann die Spiele dieser Region.
constexpr int NREG = 7;
const char* REGIONS[NREG] = {"Kanto", "Johto", "Hoenn", "Sinnoh", "Einall", "Kalos", "Alola"};
constexpr int GAME_ROWS = 4;

// Anzeigename: Virtual-Console-Spiele mit „VC“ („Blau (EN, VC)“, „Gelb (VC)“).
std::string gameTitle(const Game& g) {
    std::string n = g.name;
    if (g.gen > 2) return n;
    if (!n.empty() && n.back() == ')') return n.substr(0, n.size() - 1) + ", VC)";
    return n + " (VC)";
}

std::vector<const Game*> gamesIn(const State& st, int r) {
    std::vector<const Game*> out;
    for (const SaveSlot& s : st.installed)
        if (std::string(s.game->region) == REGIONS[r] && std::find(out.begin(), out.end(), s.game) == out.end()) out.push_back(s.game);
    return out;
}

Button regionButton(int r) { return Button{8.0f + (r % 2) * 156, 32.0f + (r / 2) * 40, 148, 36}; }

// Knöpfe der Spiele in der gewählten Region (sichtbarer Ausschnitt).
std::vector<std::pair<Button, const Game*>> gameButtons(const State& st) {
    std::vector<std::pair<Button, const Game*>> out;
    if (st.travelRegion < 0) return out;
    auto games = gamesIn(st, st.travelRegion);
    for (int k = 0; k < GAME_ROWS && st.gameScroll + k < int(games.size()); k++)
        out.push_back({Button{8, 58.0f + k * 34, 304, 30}, games[size_t(st.gameScroll + k)]});
    return out;
}
const Button B_UP{8, 200, 60, 32}, B_DOWN{252, 200, 60, 32}, B_BACK_MID{72, 200, 176, 32};

const SaveSlot* slotOf(const State& st, const Game* g, FS_MediaType m) {
    for (auto& s : st.installed)
        if (s.game == g && s.media == m) return &s;
    return nullptr;
}

const Button B_SD{8, 96, 148, 24}, B_CART{164, 96, 148, 24};

void drawBottom(const State& st) {
    rect(0, 0, 320, 26, C_HEAD);
    switch (st.screen) {
    case Screen::Menu:
        text("Menü", 160, 4, 0.6f, C_WHITE, CENTER);
        button(B_TRAVEL, "Spielen", C2D_Color32(0x3F, 0xB5, 0x5A, 0xFF), C_WHITE, 0.75f);
        button(B_TEAM, "Team", st.view == View::Team ? C_SEL : C_BG2, st.view == View::Team ? C_TEXT : C_WHITE, 0.7f);
        button(B_PC, "PC", st.view == View::PC ? C_SEL : C_BG2, st.view == View::PC ? C_TEXT : C_WHITE, 0.7f);
        button(B_BAG, "Beutel", st.view == View::Bag ? C_SEL : C_BG2, st.view == View::Bag ? C_TEXT : C_WHITE, 0.7f);
        button(B_LIST, "Liste", st.view == View::List ? C_SEL : C_BG2, st.view == View::List ? C_TEXT : C_WHITE, 0.7f);
        text("X: Spielen  Y: Ansicht  L/R: Box  A: umräumen", 160, 178, 0.4f, st.dirty ? C_SEL : C_LIGHT, CENTER, 304);
        button(B_UPDATE, "Update", C_BG2, C_LIGHT, 0.5f);
        button(B_BACKUPS, "Sicherungen", C_BG2, C_LIGHT, 0.45f);
        button(B_EXIT, "Beenden", C_BG2, C_LIGHT, 0.5f);
        break;
    case Screen::Done: break;
    case Screen::Info: {
        text("Abgleich der Spielstände", 160, 4, 0.55f, C_WHITE, CENTER);
        float y = 34;
        for (size_t i = 0; i < st.notice.size() && i < 9; i++, y += 17) text(st.notice[i], 8, y, 0.4f, C_WHITE, LEFT, 304);
        if (st.notice.size() > 9) text("… und " + std::to_string(st.notice.size() - 9) + " weitere", 8, y, 0.4f, C_LIGHT);
        button(B_BACK, "Weiter (A)", C_OK, C_WHITE, 0.55f);
        break;
    }
    case Screen::SaveMoves:
        text("Änderungen", 160, 4, 0.6f, C_WHITE, CENTER);
        text("Du hast Pokémon umgeräumt oder entlassen.", 160, 50, 0.5f, C_WHITE, CENTER);
        text("In alle Spielstände übernehmen?", 160, 74, 0.5f, C_WHITE, CENTER, 300);
        text("Sie werden dafür neu geschrieben (mit Sicherung).", 160, 100, 0.4f, C_LIGHT, CENTER, 300);
        text("Beim Verwerfen bleibt alles wie vorher.", 160, 140, 0.4f, C_LIGHT, CENTER, 300);
        button(B_YES, "Übernehmen (A)", C_OK, C_WHITE, 0.5f);
        button(B_NO, "Verwerfen (B)", C_BG2, C_WHITE, 0.5f);
        break;
    case Screen::Dupes: {
        text("Doppelte entlassen", 160, 4, 0.6f, C_WHITE, CENTER);
        size_t n = duplicates(st).size();
        if (!n) {
            text("Keine doppelten Pokémon.", 160, 80, 0.5f, C_WHITE, CENTER);
        } else {
            text(std::to_string(n) + " doppelte Pokémon gefunden", 160, 40, 0.55f, C_WHITE, CENTER);
            text("Je Art (und Form) bleibt das mit dem höchsten Level.", 160, 66, 0.4f, C_LIGHT, CENTER, 300);
            text(st.dupeShiny ? "Teammitglieder bleiben immer." : "Teammitglieder und schillernde Pokémon bleiben immer.", 160, 82, 0.4f, C_LIGHT, CENTER, 300);
            text("Sie werden nur markiert: In der Liste kannst du", 160, 104, 0.4f, C_LIGHT, CENTER, 300);
            text("einzelne mit A wieder abwählen, SELECT entlässt.", 160, 120, 0.4f, C_LIGHT, CENTER, 300);
        }
        button(B_SHINY, st.dupeShiny ? "[x] Schillernde einbeziehen (X)" : "[ ] Schillernde einbeziehen (X)", st.dupeShiny ? C_SEL : C_BG2, st.dupeShiny ? C_TEXT : C_WHITE, 0.45f);
        button(B_YES, n ? "Markieren (A)" : "OK (A)", n ? C_ERR : C_BG2, C_WHITE, 0.5f);
        button(B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.5f);
        break;
    }
    case Screen::Release: {
        text("Entlassen", 160, 4, 0.6f, C_WHITE, CENTER);
        size_t n = st.marked.size(), shiny = 0;
        for (int p : st.marked) shiny += entryAt(st, p).pk.isShiny();
        int x = 0;
        for (int p : st.marked) { // bis zu 7 Symbole zeigen
            if (x == 7) break;
            drawIcon(entryAt(st, p).pk, 16 + x++ * 42, 30, 1.0f, false);
        }
        if (n == 1) {
            const BankEntry& e = entryAt(st, *st.marked.begin());
            text(monName(e.pk) + (e.pk.isEgg() ? "" : "  Lv. " + std::to_string(levelOf(e.pk))) + " wirklich entlassen?", 160, 72, 0.5f, C_WHITE, CENTER, 300);
        } else {
            text(std::to_string(n) + " markierte Pokémon wirklich entlassen?", 160, 72, 0.5f, C_WHITE, CENTER, 300);
        }
        if (shiny) text("Darunter " + std::to_string(shiny) + " schillernde!", 160, 94, 0.45f, C_SEL, CENTER, 300);
        text("Sie verschwinden aus dem Speicher und bei der nächsten", 160, 118, 0.4f, C_LIGHT, CENTER, 300);
        text("Ankunft aus jedem Spiel. Alte Kopien kommen nicht zurück.", 160, 134, 0.4f, C_LIGHT, CENTER, 300);
        if (st.releaseStep == 0) {
            text("Noch wird nichts gespeichert: Beim Beenden kannst du", 160, 154, 0.4f, C_LIGHT, CENTER, 300);
            text("alles verwerfen.", 160, 168, 0.4f, C_LIGHT, CENTER, 300);
            button(B_YES, "Entlassen (A)", C_ERR, C_WHITE, 0.5f);
        } else {
            rect(0, 150, 320, 34, C_ERR);
            text("Sicher? Zum Bestätigen Y drücken", 160, 158, 0.5f, C_WHITE, CENTER, 300);
            button(B_YES2, "Ja, entlassen (Y)", C_ERR, C_WHITE, 0.5f);
        }
        button(st.releaseStep ? B_NO2 : B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.5f);
        break;
    }
    case Screen::Travel: {
        if (st.travelRegion < 0) {
            text("Welche Region?", 160, 4, 0.6f, C_WHITE, CENTER);
            for (int r = 0; r < NREG; r++) {
                int n = int(gamesIn(st, r).size());
                Button b = regionButton(r);
                button(b, REGIONS[r], n ? regionColor(REGIONS[r]) : C_BG2, n ? C_WHITE : C_CARD_DIM, 0.6f);
                text(std::to_string(n) + (n == 1 ? " Spiel" : " Spiele"), b.x + b.w - 6, b.y + b.h - 13, 0.34f, n ? C_WHITE : C_CARD_DIM, RIGHT);
            }
            button(Button{164, 192, 148, 36}, "Zurück (B)", C_BG2, C_WHITE, 0.5f);
            break;
        }
        rect(0, 0, 320, 26, regionColor(REGIONS[st.travelRegion]));
        text(std::string(REGIONS[st.travelRegion]) + ": welches Spiel?", 160, 4, 0.55f, C_WHITE, CENTER);
        auto games = gamesIn(st, st.travelRegion);
        for (auto& [b, g] : gameButtons(st)) {
            bool isHere = st.current && g == st.current->game;
            bool cart = slotOf(st, g, MEDIATYPE_GAME_CARD) != nullptr;
            button(b, gameTitle(*g), C_CARD, C_TEXT, 0.5f);
            if (cart) text("Modul", b.x + 6, b.y + 2, 0.32f, C_MUTED);
            if (isHere) text("zuletzt", b.x + b.w - 6, b.y + 2, 0.32f, C_SEL, RIGHT);
        }
        bool more = int(games.size()) > GAME_ROWS;
        if (more) {
            button(B_UP, "▲", st.gameScroll > 0 ? C_BG2 : C_BG, C_WHITE, 0.6f);
            button(B_DOWN, "▼", st.gameScroll + GAME_ROWS < int(games.size()) ? C_BG2 : C_BG, C_WHITE, 0.6f);
        }
        button(more ? B_BACK_MID : B_BACK, "Andere Region (B)", C_BG2, C_WHITE, 0.5f);
        break;
    }
    case Screen::Confirm:
        text("Spiel starten", 160, 4, 0.6f, C_WHITE, CENTER);
    {
        const Game& tg = *st.target->game;
        rect(8, 50, 304, 30, regionColor(tg.region));
        text(gameTitle(tg) + (st.target->cart() ? " (Modul)" : "") + " – " + tg.region, 160, 56, 0.55f, C_WHITE, CENTER, 296);
        bool both = slotOf(st, &tg, MEDIATYPE_SD) && slotOf(st, &tg, MEDIATYPE_GAME_CARD);
        if (both) {
            button(B_SD, "Installiert", st.target->cart() ? C_BG2 : C_SEL, st.target->cart() ? C_WHITE : C_TEXT, 0.45f);
            button(B_CART, "Modul", st.target->cart() ? C_SEL : C_BG2, st.target->cart() ? C_TEXT : C_WHITE, 0.45f);
        }
        text("Alle Spielstände sind auf dem gleichen Stand.", 160, both ? 130 : 104, 0.42f, C_LIGHT, CENTER, 300);
        text("Nach dem Spielen: speichern, Spiel beenden, App öffnen.", 160, both ? 146 : 120, 0.42f, C_LIGHT, CENTER, 300);
    }
        button(B_YES, "Starten (A)", C_OK, C_WHITE, 0.55f);
        button(B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.55f);
        break;
    case Screen::Backups: {
        text("Sicherungen", 160, 4, 0.6f, C_WHITE, CENTER);
        if (st.bkStep == 3) {
            float y = 34;
            for (size_t i = 0; i < st.bkResult.size() && i < 8; i++, y += 17) text(st.bkResult[i], 8, y, 0.4f, C_WHITE, LEFT, 304);
            button(B_BACK, "Beenden (A)", C_OK, C_WHITE, 0.55f);
            break;
        }
        if (st.backups.empty()) {
            text("Noch keine Sicherungen vorhanden.", 160, 90, 0.5f, C_WHITE, CENTER);
            button(B_BACK, "Zurück (B)", C_BG2, C_WHITE, 0.55f);
            break;
        }
        const BackupSet& b = st.backups[size_t(st.bkSel)];
        if (st.bkStep == 0) {
            for (int r = 0; r < BK_ROWS; r++) {
                int i = st.bkScroll + r;
                if (i >= int(st.backups.size())) break;
                const BackupSet& x = st.backups[size_t(i)];
                float y = 30 + r * 24;
                rect(8, y, 304, 22, i == st.bkSel ? C_SEL : C_BG2);
                u32 c = i == st.bkSel ? C_TEXT : C_WHITE;
                text(stampLabel(x.stamp), 14, y + 3, 0.45f, c);
                text(std::to_string(x.files.size()) + (x.files.size() == 1 ? " Datei" : " Dateien"), 306, y + 4, 0.4f, c, RIGHT);
            }
            text("A: zurückspielen   ▲▼ wählen", 160, 176, 0.4f, C_LIGHT, CENTER);
            button(B_UP, "▲", C_BG2, C_WHITE, 0.6f);
            button(B_DOWN, "▼", C_BG2, C_WHITE, 0.6f);
            button(B_BACK_MID, "Zurück (B)", C_BG2, C_WHITE, 0.5f);
            break;
        }
        text("Stand vor dem " + stampLabel(b.stamp), 160, 30, 0.5f, C_WHITE, CENTER, 300);
        float y = 52;
        for (size_t i = 0; i < b.files.size() && i < 6; i++, y += 15) text(backupWhat(b.files[i]), 160, y, 0.38f, C_LIGHT, CENTER, 300);
        if (b.files.size() > 6) text("… und " + std::to_string(b.files.size() - 6) + " weitere", 160, y, 0.38f, C_LIGHT, CENTER);
        if (st.bkStep == 1) {
            text("Diese Dateien zurückschreiben? Der jetzige Stand wird vorher", 160, 150, 0.38f, C_WHITE, CENTER, 304);
            text("gesichert. Danach wird die App beendet.", 160, 164, 0.38f, C_WHITE, CENTER, 304);
            button(B_YES, "Zurückspielen (A)", C_ERR, C_WHITE, 0.45f);
            button(B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.5f);
        } else {
            rect(0, 150, 320, 30, C_ERR);
            text("Sicher? Zum Bestätigen Y drücken", 160, 156, 0.5f, C_WHITE, CENTER, 300);
            button(B_YES2, "Ja, zurück (Y)", C_ERR, C_WHITE, 0.5f);
            button(B_NO2, "Abbrechen (B)", C_BG2, C_WHITE, 0.5f);
        }
        break;
    }
    case Screen::Update: {
        text("Update", 160, 4, 0.6f, C_WHITE, CENTER);
        if (st.updDone) {
            text("Update auf Version " + st.updVersion + " installiert.", 160, 60, 0.5f, C_OK, CENTER, 300);
            text("Die App wird jetzt beendet.", 160, 90, 0.45f, C_WHITE, CENTER, 300);
            text("Danach einfach wieder starten.", 160, 108, 0.45f, C_WHITE, CENTER, 300);
            button(B_BACK, "Beenden (A)", C_OK, C_WHITE, 0.55f);
        } else if (updater::isNewer(st.updVersion, APP_VERSION)) {
            text("Neue Version " + st.updVersion + " verfügbar", 160, 50, 0.55f, C_WHITE, CENTER, 300);
            text("Installiert: " APP_VERSION, 160, 76, 0.45f, C_LIGHT, CENTER, 300);
            text("Speicher und Spielstände bleiben erhalten.", 160, 104, 0.42f, C_LIGHT, CENTER, 300);
            text("Danach startest du die App neu.", 160, 120, 0.42f, C_LIGHT, CENTER, 300);
            button(B_YES, "Installieren (A)", C_OK, C_WHITE, 0.5f);
            button(B_NO, "Später (B)", C_BG2, C_WHITE, 0.5f);
        } else {
            text("Du hast die neueste Version.", 160, 70, 0.5f, C_WHITE, CENTER, 300);
            text("Installiert: " APP_VERSION + std::string(", auf GitHub: ") + st.updVersion, 160, 96, 0.42f, C_LIGHT, CENTER, 300);
            button(B_BACK, "Zurück (B)", C_BG2, C_WHITE, 0.55f);
        }
        break;
    }
    case Screen::Busy: {
        text("Bitte warten", 160, 4, 0.6f, C_WHITE, CENTER);
        text(st.message, 160, 100, 0.5f, C_WHITE, CENTER, 300);
        break;
    }
    case Screen::Error: {
        text("Hinweis", 160, 4, 0.6f, C_WHITE, CENTER);
        // Meldung zeilenweise
        float y = 40;
        size_t p = 0;
        while (p <= st.message.size()) {
            size_t nl = st.message.find('\n', p);
            std::string line = st.message.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
            text(line, 160, y, 0.46f, y == 40 ? C_ERR : C_WHITE, CENTER, 300);
            y += 18;
            if (nl == std::string::npos) break;
            p = nl + 1;
        }
        button(B_BACK, "Zurück (B)", C_BG2, C_WHITE, 0.55f);
        break;
    }
    }
}

C3D_RenderTarget *g_top, *g_bot;

void render(const State& st) {
    C2D_TextBufClear(g_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(g_top, C_BG);
    C2D_SceneBegin(g_top);
    drawTop(st);
    C2D_TargetClear(g_bot, C_BG);
    C2D_SceneBegin(g_bot);
    drawBottom(st);
    C3D_FrameEnd(0);
}

// ---------- Spiel starten ----------
State* g_state = nullptr;

// ---------- Eingabe ----------
void startTravel(State& st) {
    if (!st.bankOk) {
        st.message = "Spielen nicht möglich\n" + st.bankErr;
        st.screen = Screen::Error;
    } else {
        st.travelRegion = -1;
        st.screen = Screen::Travel;
    }
}

// Gewähltes Spiel starten; Umräumen/Entlassen vorher in alle Spielstände schreiben.
void doLaunch(State& st) {
    if (st.dirty) {
        st.notice.clear();
        st.message = "Schreibe Spielstände …";
        st.screen = Screen::Busy;
        render(st);
        commit(st, false);
    }
    writeLast(st.target->game->lowId);
    if (launchGame(*st.target)) {
        st.quit = true;
        return;
    }
    st.current = st.target;
    st.message = launcherName(*st.target)
                     ? std::string("Alles aktuell\nStarte jetzt ") + launcherName(*st.target) + " und lade\n" + gameTitle(*st.target->game) + "."
                     : std::string("Spiel konnte nicht gestartet werden\nBitte starte ") + st.target->label() + " selbst.";
    st.screen = Screen::Error;
}

// ---------- Sicherungen zurückspielen ----------
void openBackups(State& st) {
    st.backups.clear();
    std::map<std::string, std::vector<std::string>> byStamp;
    if (::DIR* d = opendir(BACKUPS)) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 16 && n[8] == '-' && n[15] == '-') byStamp[n.substr(0, 15)].push_back(n);
        }
        closedir(d);
    }
    for (auto it = byStamp.rbegin(); it != byStamp.rend(); ++it) {
        std::sort(it->second.begin(), it->second.end());
        st.backups.push_back({it->first, it->second});
    }
    st.bkSel = st.bkScroll = st.bkStep = 0;
    st.bkResult.clear();
    st.screen = Screen::Backups;
}

// Ziel einer Sicherung: Speicher oder ein Spielstand (über Name + Datei, wie beim Sichern)
const SaveSlot* backupSlot(const State& st, const std::string& what) {
    for (auto& s : st.installed)
        if (s.label() + "-" + saveName(*s.game) == what) return &s;
    return nullptr;
}

void restoreBackup(State& st) {
    const BackupSet set = st.backups[size_t(st.bkSel)];
    st.message = "Spiele Sicherung zurück …";
    st.screen = Screen::Busy;
    render(st);
    // Erst alles einlesen (das Sichern des jetzigen Stands kann alte Sicherungen löschen)
    struct Item { std::string what; std::vector<u8> data; const SaveSlot* slot; bool bank; };
    std::vector<Item> items;
    std::vector<std::string> res;
    for (const std::string& file : set.files) {
        Item it{backupWhat(file), {}, nullptr, false};
        it.bank = it.what == "speicher.bin";
        if (!it.bank) it.slot = backupSlot(st, it.what);
        if (!it.bank && !it.slot) { res.push_back(it.what + ": Spielstand nicht gefunden, übersprungen"); continue; }
        if (!readFileAll((std::string(BACKUPS) + "/" + file).c_str(), it.data)) { res.push_back(it.what + ": nicht lesbar"); continue; }
        items.push_back(std::move(it));
    }
    char stamp[32];
    timeStamp(stamp, sizeof stamp);
    int ok = 0;
    for (Item& it : items) {
        std::vector<u8> cur;
        std::string err;
        bool have = it.bank ? readFileAll(BANK, cur) : readSave(*it.slot, cur, err);
        if (have && !backupFile(stamp, it.what, cur)) { res.push_back(it.what + ": jetziger Stand nicht sicherbar, übersprungen"); continue; }
        bool w = false;
        if (it.bank) {
            if (FILE* f = std::fopen(BANK, "wb")) {
                w = std::fwrite(it.data.data(), 1, it.data.size(), f) == it.data.size();
                w = (std::fclose(f) == 0) && w;
            }
        } else {
            w = writeSave(*it.slot, it.data, err);
        }
        logLine(std::string("Sicherung ") + set.stamp + " zurück: " + it.what + (w ? " ok" : " FEHLER " + err));
        if (w) ok++;
        else res.push_back(it.what + ": Schreiben fehlgeschlagen " + err);
    }
    res.insert(res.begin(), std::to_string(ok) + " von " + std::to_string(set.files.size()) + " Dateien zurückgespielt (Stand " + stampLabel(set.stamp) + ").");
    res.push_back("Der vorherige Stand ist als neue Sicherung abgelegt.");
    res.push_back("Die App wird beendet. Beim nächsten Start wird abgeglichen.");
    st.bkResult = res;
    st.bkStep = 3;
    st.dirty = false; // Speicher im Arbeitsspeicher ist veraltet: nichts mehr schreiben
    st.screen = Screen::Backups;
}

// ---------- Update ----------
// Beim Start, vor dem Auslesen der Spielstände (Wunsch des Nutzers): kurz nach einer neuen Version
// suchen. Ohne WLAN oder bei Fehlern still weiter (nur Protokoll). Gibt es eine, fragt die App.
void input(State& st, u32 down, const touchPosition& t);

void startupUpdateCheck(State& st) {
    mkdir(DIR, 0777);
    if (FILE* f = std::fopen(LOGFILE, "w")) std::fclose(f); // Protokoll je Start neu
    st.updStartup = true;
    render(st);
    std::string err;
    if (!updater::init(err) || !updater::latestVersion(st.updVersion, err, 10)) {
        logLine("Update-Prüfung beim Start: " + err);
        st.updStartup = false;
        return;
    }
    logLine("Update-Prüfung beim Start: installiert " APP_VERSION ", GitHub " + st.updVersion);
    if (updater::isNewer(st.updVersion, APP_VERSION)) {
        st.updDone = false;
        st.screen = Screen::Update;
        // Eigene kleine Schleife, bis installiert (App endet) oder „Später“ (weiter zum Auslesen)
        while (aptMainLoop() && !st.quit && st.screen != Screen::Menu) {
            hidScanInput();
            u32 down = hidKeysDown();
            touchPosition t{};
            if (down & KEY_TOUCH) hidTouchRead(&t);
            if (down) input(st, down, t);
            render(st);
        }
    }
    st.screen = Screen::Menu;
    st.updStartup = false;
}

void checkUpdate(State& st) {
    if (st.dirty) {
        st.message = "Erst Änderungen übernehmen\nUmräumen oder Entlassen ist noch nicht gespeichert.\nBitte erst spielen oder beenden („Übernehmen“).";
        st.screen = Screen::Error;
        return;
    }
    st.message = "Suche nach Updates …";
    st.screen = Screen::Busy;
    render(st);
    std::string err;
    if (!updater::init(err) || !updater::latestVersion(st.updVersion, err)) {
        logLine("Update: " + err);
        st.message = "Update nicht möglich\n" + err;
        st.screen = Screen::Error;
        return;
    }
    logLine("Update: installiert " APP_VERSION ", GitHub " + st.updVersion);
    st.updDone = false;
    st.screen = Screen::Update;
}

void installUpdate(State& st) {
    st.screen = Screen::Busy;
    st.message = "Lade Update …";
    render(st);
    std::string err;
    bool ok = updater::downloadAndInstall([&](double now, double total) {
        char b[64];
        if (total > 0) std::snprintf(b, sizeof b, "Lade Update … %d %%  (%.1f MB)", int(now * 100 / total), total / 1048576.0);
        else std::snprintf(b, sizeof b, "Lade Update … %.1f MB", now / 1048576.0);
        st.message = b;
        render(st);
    }, err);
    logLine(ok ? "Update installiert: " + st.updVersion : "Update fehlgeschlagen: " + err);
    if (!ok) {
        st.message = "Update fehlgeschlagen\n" + err + "\nDie bisherige Version bleibt installiert.";
        st.screen = Screen::Error;
        return;
    }
    st.updDone = true;
    st.screen = Screen::Update;
}

void requestQuit(State& st) {
    if (st.dirty) st.screen = Screen::SaveMoves;
    else st.quit = true;
}

// A im Team/PC: aufnehmen bzw. ablegen (tauschen).
void pickOrDrop(State& st) {
    int pos = st.view == View::Team ? st.teamCursor : Bank::PARTY + st.box * Bank::SLOTS + st.cursor;
    if (st.held < 0) {
        if (entryAt(st, pos).used) st.held = pos;
        return;
    }
    if (st.held != pos) {
        std::swap(entryAt(st, st.held), entryAt(st, pos));
        for (int p : {st.held, pos}) { // ins Team geholt: Teamwerte (KP, Level) berechnen
            BankEntry& e = entryAt(st, p);
            if (p < Bank::PARTY && e.used && (e.pk.d[0xEC] == 0 || rd16(e.pk.d.data() + 0xF2) == 0)) refreshParty7(e.pk, g_species);
        }
        st.dirty = true;
    }
    st.held = -1;
}

void openList(State& st) {
    st.held = -1;
    st.marked.clear();
    st.view = View::List;
    buildList(st);
}

// Markierte Pokémon entlassen: Kennung bleibt als „gesehen“ im Speicher, damit sie nicht zurückkommen.
void release(State& st) {
    int team = 0, teamMarked = 0;
    for (int i = 0; i < Bank::PARTY; i++) {
        team += st.bank.party[size_t(i)].used;
        teamMarked += st.bank.party[size_t(i)].used && st.marked.count(i);
    }
    if (teamMarked && teamMarked >= team) {
        st.message = "Entlassen nicht möglich\nEin Pokémon muss im Team bleiben.";
        st.screen = Screen::Error;
        return;
    }
    u16 sp = st.listOpen >= 0 ? st.list[size_t(st.listOpen)].species : 0;
    bool open = st.listOpen >= 0;
    st.bank.remember();
    size_t n = 0;
    for (int p : st.marked)
        if (entryAt(st, p).used) { entryAt(st, p) = BankEntry{}; n++; }
    st.marked.clear();
    st.dirty = true;
    buildList(st);
    st.listOpen = -1; // Gruppe kann weggefallen sein: neu suchen
    if (open)
        for (size_t i = 0; i < st.list.size(); i++)
            if (st.list[i].species == sp) { st.listOpen = int(i); st.listSel = int(i); }
    if (st.listOpen >= 0) st.entrySel = std::min(st.entrySel, int(st.list[size_t(st.listOpen)].pos.size()) - 1);
    st.notice = {std::to_string(n) + (n == 1 ? " Pokémon entlassen." : " Pokémon entlassen."), "Beim Spielen oder Beenden werden alle Spielstände aktualisiert.",
                 "Versehen? Beim Beenden „Verwerfen“ wählen, dann bleibt alles wie vorher."};
    st.screen = Screen::Info;
}

// Doppelte: je Art und Form bleibt das Pokémon mit dem höchsten Level (bei Gleichstand das ältere);
// Teammitglieder, Eier und fehlende nie; schillernde nur, wenn gewählt (Nutzer: Living-Dex-Test mit PKHeX).
std::vector<int> duplicates(const State& st) {
    std::map<uint32_t, int> best; // Art·256 + Form -> Position
    auto key = [](const PK6& pk) { return uint32_t(pk.species()) << 8 | pk.form(); };
    auto better = [&](int a, int b) {
        const PK6 &x = entryAt(st, a).pk, &y = entryAt(st, b).pk;
        int lx = levelOf(x), ly = levelOf(y);
        if (lx != ly) return lx > ly;
        if (x.exp() != y.exp()) return x.exp() > y.exp();
        return entryAt(st, a).uid < entryAt(st, b).uid;
    };
    const int total = Bank::PARTY + Bank::BOXES * Bank::SLOTS;
    for (int p = 0; p < total; p++) {
        const BankEntry& e = entryAt(st, p);
        if (!e.used || e.pk.isEgg()) continue;
        auto it = best.find(key(e.pk));
        if (it == best.end() || better(p, it->second)) best[key(e.pk)] = p;
    }
    std::vector<int> out;
    for (int p = Bank::PARTY; p < total; p++) { // nur Boxen
        const BankEntry& e = entryAt(st, p);
        if (!e.used || e.pk.isEgg() || (e.pk.isShiny() && !st.dupeShiny) || e.away()) continue;
        if (best[key(e.pk)] != p) out.push_back(p);
    }
    return out;
}

// Doppelte nur markieren; entlassen wird erst mit SELECT nach Prüfung in der Liste.
void markDuplicates(State& st) {
    for (int p : duplicates(st)) st.marked.insert(p);
    st.screen = Screen::Menu;
}

void listInput(State& st, u32 down) {
    if (st.list.empty()) return;
    if (st.listOpen < 0) {
        int n = int(st.list.size());
        if (down & KEY_DDOWN) st.listSel = std::min(n - 1, st.listSel + 1);
        if (down & KEY_DUP) st.listSel = std::max(0, st.listSel - 1);
        if (down & KEY_DRIGHT) st.listSel = std::min(n - 1, st.listSel + LIST_ROWS);
        if (down & KEY_DLEFT) st.listSel = std::max(0, st.listSel - LIST_ROWS);
        if (st.listSel < st.listScroll) st.listScroll = st.listSel;
        if (st.listSel >= st.listScroll + LIST_ROWS) st.listScroll = st.listSel - LIST_ROWS + 1;
        if (down & KEY_A) { st.listOpen = st.listSel; st.entrySel = 0; }
        return;
    }
    int n = int(st.list[size_t(st.listOpen)].pos.size());
    if (down & KEY_DDOWN) st.entrySel = std::min(n - 1, st.entrySel + 1);
    if (down & KEY_DUP) st.entrySel = std::max(0, st.entrySel - 1);
    if (down & KEY_B) st.listOpen = -1;
    else if (down & KEY_A) {
        int p = st.list[size_t(st.listOpen)].pos[size_t(st.entrySel)];
        if (!st.marked.erase(p)) st.marked.insert(p);
        if (st.entrySel + 1 < n) st.entrySel++; // gleich zum nächsten
    }
}

void input(State& st, u32 down, const touchPosition& t) {
    bool touch = down & KEY_TOUCH;
    if (down & KEY_START) {
        if (st.screen == Screen::Menu) requestQuit(st);
        else if (st.screen != Screen::Busy && st.screen != Screen::SaveMoves) st.quit = true;
        return;
    }

    // Navigation im oberen Bildschirm (in allen Menüs außer während der Reise)
    if (st.screen == Screen::Menu || st.screen == Screen::Travel) {
        if (st.view == View::PC) {
            if (down & KEY_R) st.box = (st.box + 1) % Bank::BOXES;
            if (down & KEY_L) st.box = (st.box + Bank::BOXES - 1) % Bank::BOXES;
            if (down & KEY_DRIGHT) st.cursor = (st.cursor + 1) % Bank::SLOTS;
            if (down & KEY_DLEFT) st.cursor = (st.cursor + Bank::SLOTS - 1) % Bank::SLOTS;
            if (down & KEY_DDOWN) st.cursor = (st.cursor + 6) % Bank::SLOTS;
            if (down & KEY_DUP) st.cursor = (st.cursor + Bank::SLOTS - 6) % Bank::SLOTS;
        }
        if (st.view == View::Team) {
            if (down & (KEY_DRIGHT | KEY_DLEFT)) st.teamCursor ^= 1;
            if (down & KEY_DDOWN) st.teamCursor = (st.teamCursor + 2) % Bank::PARTY;
            if (down & KEY_DUP) st.teamCursor = (st.teamCursor + Bank::PARTY - 2) % Bank::PARTY;
        }
        if (st.view == View::Bag) {
            int maxScroll = std::max(0, int(st.bank.items.size()) - BAG_ROWS);
            if (down & KEY_DDOWN) st.bagScroll = std::min(maxScroll, st.bagScroll + 1);
            if (down & KEY_DUP) st.bagScroll = std::max(0, st.bagScroll - 1);
        }
        if (down & KEY_Y) {
            if (st.view == View::Bag) openList(st);
            else st.view = st.view == View::Team ? View::PC : st.view == View::PC ? View::Bag : View::Team;
        }
    }

    switch (st.screen) {
    case Screen::Info:
        if ((touch && B_BACK.hit(t)) || (down & (KEY_A | KEY_B))) st.screen = Screen::Menu;
        break;
    case Screen::SaveMoves:
        if ((touch && B_YES.hit(t)) || (down & KEY_A)) {
            st.message = "Schreibe Spielstände …";
            st.screen = Screen::Busy;
            render(st);
            commit(st, false);
            st.quit = true;
        } else if ((touch && B_NO.hit(t)) || (down & KEY_B)) {
            st.quit = true;
        }
        break;
    case Screen::Dupes:
        if ((touch && B_YES.hit(t)) || (down & KEY_A)) markDuplicates(st);
        else if ((touch && B_SHINY.hit(t)) || (down & KEY_X)) st.dupeShiny = !st.dupeShiny;
        else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        break;
    case Screen::Backups:
        if (st.bkStep == 3) { if ((touch && B_BACK.hit(t)) || (down & (KEY_A | KEY_B))) st.quit = true; break; }
        if (st.backups.empty()) { if ((touch && B_BACK.hit(t)) || (down & (KEY_A | KEY_B))) st.screen = Screen::Menu; break; }
        if (st.bkStep == 0) {
            int n = int(st.backups.size());
            if ((down & KEY_DDOWN) || (touch && B_DOWN.hit(t))) st.bkSel = std::min(n - 1, st.bkSel + 1);
            if ((down & KEY_DUP) || (touch && B_UP.hit(t))) st.bkSel = std::max(0, st.bkSel - 1);
            if (st.bkSel < st.bkScroll) st.bkScroll = st.bkSel;
            if (st.bkSel >= st.bkScroll + BK_ROWS) st.bkScroll = st.bkSel - BK_ROWS + 1;
            if (down & KEY_A) st.bkStep = 1;
            else if ((down & KEY_B) || (touch && B_BACK_MID.hit(t))) st.screen = Screen::Menu;
            else if (touch)
                for (int r = 0; r < BK_ROWS; r++)
                    if (Button{8, float(30 + r * 24), 304, 22}.hit(t) && st.bkScroll + r < n) {
                        if (st.bkSel == st.bkScroll + r) st.bkStep = 1;
                        st.bkSel = st.bkScroll + r;
                    }
        } else if (st.bkStep == 1) {
            if ((touch && B_YES.hit(t)) || (down & KEY_A)) st.bkStep = 2;
            else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.bkStep = 0;
        } else {
            if ((touch && B_YES2.hit(t)) || (down & KEY_Y)) restoreBackup(st);
            else if ((touch && B_NO2.hit(t)) || (down & KEY_B)) st.bkStep = 0;
        }
        break;
    case Screen::Update:
        if (st.updDone) { if ((touch && B_BACK.hit(t)) || (down & (KEY_A | KEY_B))) st.quit = true; }
        else if (updater::isNewer(st.updVersion, APP_VERSION)) {
            if ((touch && B_YES.hit(t)) || (down & KEY_A)) installUpdate(st);
            else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        } else if ((touch && B_BACK.hit(t)) || (down & (KEY_A | KEY_B))) st.screen = Screen::Menu;
        break;
    case Screen::Release:
        // Zweimal bestätigen: erst A, dann Y (Knopf auf der anderen Seite), damit nichts aus Versehen passiert.
        if (st.releaseStep == 0) {
            if ((touch && B_YES.hit(t)) || (down & KEY_A)) st.releaseStep = 1;
            else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        } else {
            if ((touch && B_YES2.hit(t)) || (down & KEY_Y)) { st.releaseStep = 0; release(st); }
            else if ((touch && B_NO2.hit(t)) || (down & KEY_B)) { st.releaseStep = 0; st.screen = Screen::Menu; }
        }
        break;
    case Screen::Menu:
        if (st.view == View::List && (down & KEY_SELECT)) {
            if (!st.marked.empty()) { st.releaseStep = 0; st.screen = Screen::Release; }
            else if (st.listOpen < 0) st.screen = Screen::Dupes;
        } else if (st.view == View::List && st.listOpen < 0 && !st.marked.empty() && (down & KEY_X)) st.marked.clear();
        else if (st.view == View::List && (down & (KEY_A | KEY_B | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT))) listInput(st, down);
        else if (st.view != View::Bag && st.view != View::List && (down & KEY_A)) pickOrDrop(st);
        else if (down & KEY_B) st.held = -1;
        else if ((touch && B_TRAVEL.hit(t)) || (down & KEY_X)) { st.held = -1; startTravel(st); }
        else if (touch && B_TEAM.hit(t)) st.view = View::Team;
        else if (touch && B_PC.hit(t)) st.view = View::PC;
        else if (touch && B_BAG.hit(t)) st.view = View::Bag;
        else if (touch && B_LIST.hit(t)) openList(st);
        else if (touch && B_EXIT.hit(t)) requestQuit(st);
        else if (touch && B_UPDATE.hit(t)) checkUpdate(st);
        else if (touch && B_BACKUPS.hit(t)) openBackups(st);
        break;
    case Screen::Travel: {
        if (st.travelRegion < 0) {
            if ((touch && Button{164, 192, 148, 36}.hit(t)) || (down & KEY_B)) { st.screen = Screen::Menu; break; }
            if (touch)
                for (int r = 0; r < NREG; r++)
                    if (regionButton(r).hit(t) && !gamesIn(st, r).empty()) { st.travelRegion = r; st.gameScroll = 0; }
            break;
        }
        auto games = gamesIn(st, st.travelRegion);
        bool more = int(games.size()) > GAME_ROWS;
        int maxScroll = std::max(0, int(games.size()) - GAME_ROWS);
        if ((touch && (more ? B_BACK_MID : B_BACK).hit(t)) || (down & KEY_B)) { st.travelRegion = -1; break; }
        if ((touch && more && B_UP.hit(t)) || (down & KEY_DUP)) st.gameScroll = std::max(0, st.gameScroll - 1);
        if ((touch && more && B_DOWN.hit(t)) || (down & KEY_DDOWN)) st.gameScroll = std::min(maxScroll, st.gameScroll + 1);
        if (touch)
            for (auto& [b, g] : gameButtons(st))
                if (b.hit(t)) {
                    // Standard: das zuletzt genutzte Medium dieses Spiels, sonst installiert
                    const SaveSlot* sd = slotOf(st, g, MEDIATYPE_SD);
                    const SaveSlot* cart = slotOf(st, g, MEDIATYPE_GAME_CARD);
                    bool preferCart = st.current && st.current->cart() && st.current->game == g;
                    st.target = (preferCart && cart) ? cart : (sd ? sd : cart);
                    st.screen = Screen::Confirm;
                }
        break;
    }
    case Screen::Confirm:
        if (touch && B_SD.hit(t) && slotOf(st, st.target->game, MEDIATYPE_SD) && slotOf(st, st.target->game, MEDIATYPE_GAME_CARD))
            st.target = slotOf(st, st.target->game, MEDIATYPE_SD);
        else if (touch && B_CART.hit(t) && slotOf(st, st.target->game, MEDIATYPE_GAME_CARD) && slotOf(st, st.target->game, MEDIATYPE_SD))
            st.target = slotOf(st, st.target->game, MEDIATYPE_GAME_CARD);
        else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Travel;
        else if ((touch && B_YES.hit(t)) || (down & KEY_A)) doLaunch(st);
        break;
    case Screen::Done: break;
    case Screen::Error:
        if ((touch && B_BACK.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        break;
    case Screen::Busy: break;
    }
}

} // namespace

// Größerer Stack (Standard 32 KB): der Abgleich legt größere Datenblöcke an.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

int main() {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_buf = C2D_TextBufNew(4096);

    State st;
    g_state = &st;
    render(st); // leerer Rahmen während des Ladens
    startupUpdateCheck(st);
    if (st.quit || !aptMainLoop()) { updater::exit(); C2D_TextBufDelete(g_buf); C2D_Fini(); C3D_Fini(); gfxExit(); return 0; }
    loadState(st);

    while (aptMainLoop() && !st.quit) {
        hidScanInput();
        u32 down = hidKeysDown();
        touchPosition t{};
        if (down & KEY_TOUCH) hidTouchRead(&t);
        if (down) input(st, down, t);
        render(st);
    }

    updater::exit();
    C2D_TextBufDelete(g_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
