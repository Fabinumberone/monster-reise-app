// Pokémon World 3DS – Reise-App mit grafischer Oberfläche (citro2d, Systemschrift).
// Oben: Kopf mit Trainer und Region, darunter Team, PC oder Beutel.
// Unten: Touch-Menü, Regionswahl und Reiseablauf.
// Tasten: Steuerkreuz bewegt die Auswahl oben (Team/PC/Beutel), L/R wechselt die Box,
//         A nimmt ein Pokémon auf bzw. legt es ab (tauscht), B bricht ab,
//         Y wechselt die Ansicht, X öffnet „Reisen“, START beendet.
#include <3ds.h>
#include <citro2d.h>

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "bank.hpp"
#include "bytes.hpp"
#include "games.hpp"
#include "gb.hpp"
#include "gbconvert.hpp"
#include "icons.hpp"
#include "species.hpp"
#include "items.hpp"
#include "names.hpp"
#include "savefile.hpp"
#include "sync.hpp"

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
enum class Screen { Menu, Info, SaveMoves, Release, Travel, Confirm, Busy, Done, Error };
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
    std::string bankErr;
    std::vector<SaveSlot> installed;
    const SaveSlot* current = nullptr;  // hier ist man gerade (nullptr: neuer Speicher, noch in keinem Spiel)
    const SaveSlot* target = nullptr;
    std::vector<std::string> notice;    // Meldungen des Abgleichs beim Start
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

GBContext gbCtx(const Game& g, const Trainer6& t) {
    GBContext c;
    c.species = &g_species;
    c.names = &g_names;
    c.trainer = t;
    c.gen = g.gen;
    c.version = g.gbVersion;
    c.language = g.language;
    return c;
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

// Beim Start: alle Spielstände lesen und mit dem Speicher abgleichen (siehe sync.hpp). In die
// Spielstände wird dabei nichts geschrieben; der Speicher wird vorher gesichert.
void loadState(State& st) {
    g_names.load(NAMES);
    g_species.load(SPECIES);
    g_icons.load(ICONS);
    st.installed = findSaves();
    st.current = nullptr;
    st.bank = Bank{};
    st.bank.species = &g_species;
    std::vector<u8> oldFile;
    bool exists = readFileAll(BANK, oldFile);
    if (exists) {
        st.bankOk = st.bank.load(BANK, st.bankErr);
        st.bank.species = &g_species;
        if (!st.bankOk) { st.loading = false; return; } // beschädigt: nichts anfassen
    }
    // Aktueller Spielstand: genau gemerkt, sonst zuletzt.txt, sonst das erste passende Spiel.
    if (!st.bank.current.empty()) {
        for (auto& s : st.installed)
            if (st.bank.currentLow && s.game->lowId == st.bank.currentLow && u8(s.media) == st.bank.currentMedia) st.current = &s;
        u32 last = readLast();
        if (!st.current)
            for (auto& s : st.installed)
                if (s.game->lowId == last && st.bank.current == bankKind(*s.game)) { st.current = &s; break; }
        if (!st.current)
            for (auto& s : st.installed)
                if (st.bank.current == bankKind(*s.game)) { st.current = &s; break; }
    }
    std::vector<SyncInput> in;
    for (auto& s : st.installed) {
        SyncInput x;
        x.label = s.label();
        x.lowId = s.game->lowId;
        x.media = u8(s.media);
        x.gen = s.game->gen;
        x.gbVersion = s.game->gbVersion;
        x.language = s.game->language;
        x.current = &s == st.current;
        x.known = exists && st.bank.fileVersion < 4; // älterer Speicher: Items/Geld sind schon drin
        std::string err;
        if (!readSave(s, x.data, err)) { st.notice.push_back(err); continue; }
        in.push_back(std::move(x));
    }
    std::vector<u8> before = exists ? st.bank.serialize() : std::vector<u8>{};
    SyncReport rep;
    syncAll(st.bank, in, g_species, g_names, rep);
    if (st.current) {
        st.bank.currentLow = st.current->game->lowId;
        st.bank.currentMedia = u8(st.current->media);
    }
    for (auto& l : rep.lines) st.notice.push_back(l);
    if (!st.bank.hasTrainer()) {
        st.bankOk = false;
        st.bankErr = "Keine Spielstände gefunden";
    } else {
        st.bankOk = true;
        if (st.bank.serialize() != before) {
            mkdir(DIR, 0777);
            mkdir(BACKUPS, 0777);
            if (exists) {
                char stamp[32];
                timeStamp(stamp, sizeof stamp);
                std::string p = std::string(BACKUPS) + "/" + stamp + "-speicher-vor-abgleich.bin";
                if (FILE* f = std::fopen(p.c_str(), "wb")) {
                    std::fwrite(oldFile.data(), 1, oldFile.size(), f);
                    std::fclose(f);
                }
            }
            std::string err;
            if (!st.bank.save(BANK, err)) st.notice.push_back("Speicher nicht gespeichert: " + err);
        }
    }
    st.synced = rep.currentSynced;
    st.loading = false;
    if (!st.notice.empty()) st.screen = Screen::Info;
}

// Ist das Pokémon im aktuellen Spiel sichtbar?
bool visibleHere(const State& st, const PK6& pk) {
    if (!st.current) return true;
    if (st.current->game->gen <= 2) return fitsGB(pk, st.current->game->gen);
    return fitsIn(pk, st.current->game->kind);
}

// Boxen des Spiels (Gen 1/2: Plätze zu je 20, linear über die Speicherboxen verteilt).
int gameBoxes(const Game& g) {
    if (g.gen == 1) return 12;
    if (g.gen == 2) return 14;
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

std::string placeName(const State& st, int pos) {
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
    if (st.current->game->gen == 1) return modernItemToGen1(item) ? "Beutel" : nullptr;
    if (st.current->game->gen == 2) {
        if (!modernItemToGen2(item) || pouchOf(GameKind::USUM, item) == Pouch::None) return nullptr;
        return (item <= 16 || (item >= 492 && item <= 500)) ? "Ball-Tasche" : "Item-Tasche";
    }
    Pouch p = pouchOf(st.current->game->kind, item);
    return p == Pouch::None ? nullptr : pouchName(p);
}

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
    text("Pokémon World 3DS", 8, 4, 0.6f, C_WHITE);
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
            bool vis = visibleHere(st, e.pk);
            rect(x, y, 188, 58, vis ? C_CARD : C_CARD_DIM);
            drawIcon(e.pk, x + 2, y + 4, 1.0f, !vis);
            text(monName(e.pk), x + 46, y + 3, 0.52f, C_TEXT, LEFT, 110);
            if (!e.pk.isEgg()) {
                text("Lv. " + std::to_string(e.pk.partyLevel()), x + 46, y + 21, 0.42f, C_MUTED);
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
        if (e.used) drawIcon(e.pk, x + 2, y - 1, 1.0f, !visibleHere(st, e.pk) || !slotVisibleHere(st, idx));
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
    bool vis = visibleHere(st, sel.pk) && slotVisibleHere(st, st.box * Bank::SLOTS + st.cursor);
    drawIcon(sel.pk, 301, 52, 2.0f, !vis);
    text(monName(sel.pk), 341, 114, 0.5f, C_TEXT, CENTER, 96);
    if (!sel.pk.isEgg() && g_names.species(sel.pk.species()) != monName(sel.pk))
        text(g_names.species(sel.pk.species()), 341, 130, 0.4f, C_MUTED, CENTER, 96);
    float y = 148;
    for (u16 m : sel.pk.moves())
        if (m) { text(g_names.move(m), 296, y, 0.4f, C_TEXT, LEFT, 92); y += 14; }
    if (!vis) text("hier nicht sichtbar", 341, 218, 0.38f, C_ERR, CENTER, 96);
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
        }
        text("A: öffnen   ▲▼ ◀▶ blättern", 392, 226, 0.38f, C_LIGHT, RIGHT);
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
        rect(8, y, 384, 36, sel ? C_CARD : C_BG2);
        u32 c = sel ? C_TEXT : C_WHITE, m = sel ? C_MUTED : C_LIGHT;
        drawIcon(e.pk, 10, y + 3, 1.0f, false);
        std::string name = monName(e.pk);
        if (e.pk.isShiny()) name += " ★";
        text(name, 54, y + 2, 0.45f, c, LEFT, 150);
        if (!e.pk.isEgg()) text("Lv. " + std::to_string(e.pk.partyLevel()), 210, y + 2, 0.42f, c);
        text(std::string("aus ") + gameOfVersion(e.pk.version()), 386, y + 2, 0.4f, m, RIGHT, 120);
        text(placeName(st, g.pos[size_t(i)]), 54, y + 19, 0.38f, m, LEFT, 220);
        text("OT " + e.pk.otName(), 386, y + 19, 0.38f, m, RIGHT, 110);
    }
    text("A: entlassen   B: zurück", 392, 226, 0.38f, C_LIGHT, RIGHT);
}

void drawTop(const State& st) {
    drawHeader(st);
    if (st.loading) {
        text("Lese Spielstände …", 200, 110, 0.6f, C_LIGHT, CENTER);
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
const Button B_EXIT{8, 196, 304, 36}, B_BACK{8, 200, 304, 32};
const Button B_YES{8, 186, 148, 46}, B_NO{164, 186, 148, 46};

// Schaltflächen der Regionswahl: fünf Spalten, je Region die installierten Spiele.
constexpr int NREG = 5;
const char* REGIONS[NREG] = {"Kanto", "Johto", "Hoenn", "Kalos", "Alola"};
constexpr float COLW = 60, COLGAP = 3, COLX = 4;
// Ein Knopf je Spiel; gibt es Modul und installierte Fassung, wählt man beim Bestätigen.
std::vector<std::pair<Button, const Game*>> travelButtons(const State& st) {
    std::vector<std::pair<Button, const Game*>> out;
    for (int r = 0; r < NREG; r++) {
        int n = 0;
        const Game* prev = nullptr;
        for (const SaveSlot& s : st.installed)
            if (std::string(s.game->region) == REGIONS[r] && s.game != prev) {
                out.push_back({Button{COLX + r * (COLW + COLGAP), 56.0f + n++ * 34, COLW, 30}, s.game});
                prev = s.game;
            }
    }
    return out;
}

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
        button(B_TRAVEL, "Reisen", C2D_Color32(0x3F, 0xB5, 0x5A, 0xFF), C_WHITE, 0.75f);
        button(B_TEAM, "Team", st.view == View::Team ? C_SEL : C_BG2, st.view == View::Team ? C_TEXT : C_WHITE, 0.7f);
        button(B_PC, "PC", st.view == View::PC ? C_SEL : C_BG2, st.view == View::PC ? C_TEXT : C_WHITE, 0.7f);
        button(B_BAG, "Beutel", st.view == View::Bag ? C_SEL : C_BG2, st.view == View::Bag ? C_TEXT : C_WHITE, 0.7f);
        button(B_LIST, "Liste", st.view == View::List ? C_SEL : C_BG2, st.view == View::List ? C_TEXT : C_WHITE, 0.7f);
        text("X: Reisen  Y: Ansicht  L/R: Box  A: umräumen", 160, 178, 0.4f, st.dirty ? C_SEL : C_LIGHT, CENTER, 304);
        button(B_EXIT, "Beenden", C_BG2, C_LIGHT, 0.55f);
        break;
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
        if (st.current) {
            text("In " + st.current->label() + " übernehmen?", 160, 74, 0.5f, C_WHITE, CENTER, 300);
            text("Der Spielstand wird dafür neu geschrieben (mit Sicherung).", 160, 100, 0.4f, C_LIGHT, CENTER, 300);
        } else {
            text("Im Speicher sichern?", 160, 74, 0.5f, C_WHITE, CENTER);
        }
        text("Beim Verwerfen bleibt alles wie vorher.", 160, 140, 0.4f, C_LIGHT, CENTER, 300);
        button(B_YES, "Übernehmen (A)", C_OK, C_WHITE, 0.5f);
        button(B_NO, "Verwerfen (B)", C_BG2, C_WHITE, 0.5f);
        break;
    case Screen::Release: {
        text("Entlassen", 160, 4, 0.6f, C_WHITE, CENTER);
        const BankEntry& e = entryAt(st, st.list[size_t(st.listOpen)].pos[size_t(st.entrySel)]);
        drawIcon(e.pk, 140, 36, 1.0f, false);
        text(monName(e.pk) + (e.pk.isEgg() ? "" : "  Lv. " + std::to_string(e.pk.partyLevel())), 160, 72, 0.55f, C_WHITE, CENTER, 300);
        text("wirklich entlassen?", 160, 94, 0.5f, C_WHITE, CENTER);
        text("Es verschwindet aus dem Speicher und bei der nächsten", 160, 120, 0.4f, C_LIGHT, CENTER, 300);
        text("Ankunft aus jedem Spiel. Alte Kopien kommen nicht zurück.", 160, 136, 0.4f, C_LIGHT, CENTER, 300);
        text("Rückgängig nur über die Sicherungen.", 160, 156, 0.4f, C_SEL, CENTER, 300);
        button(B_YES, "Entlassen (A)", C_ERR, C_WHITE, 0.5f);
        button(B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.5f);
        break;
    }
    case Screen::Travel: {
        text("Wohin möchtest du reisen?", 160, 4, 0.55f, C_WHITE, CENTER);
        for (int r = 0; r < NREG; r++) {
            rect(COLX + r * (COLW + COLGAP), 32, COLW, 20, regionColor(REGIONS[r]));
            text(REGIONS[r], COLX + r * (COLW + COLGAP) + COLW / 2, 33, 0.45f, C_WHITE, CENTER, COLW - 4);
        }
        for (auto& [b, g] : travelButtons(st)) {
            bool isHere = st.current && g == st.current->game;
            button(b, g->name, isHere ? C_BG2 : C_CARD, isHere ? C_LIGHT : C_TEXT, 0.42f);
            if (isHere) text("hier", b.x + b.w - 4, b.y + 1, 0.32f, C_SEL, RIGHT);
        }
        button(B_BACK, "Zurück", C_BG2, C_WHITE, 0.55f);
        break;
    }
    case Screen::Confirm:
        text("Reise bestätigen", 160, 4, 0.6f, C_WHITE, CENTER);
    {
        const Game& tg = *st.target->game;
        rect(8, 36, 304, 26, st.current ? regionColor(st.current->game->region) : C_BG2);
        text(st.current ? "Von: " + st.current->label() + " (" + st.current->game->region + ")" : std::string("Von: noch in keinem Spiel"),
             16, 40, 0.5f, C_WHITE, LEFT, 288);
        rect(8, 66, 304, 26, regionColor(tg.region));
        text("Nach: " + st.target->label() + " (" + tg.region + ")", 16, 70, 0.5f, C_WHITE, LEFT, 288);
        bool both = slotOf(st, &tg, MEDIATYPE_SD) && slotOf(st, &tg, MEDIATYPE_GAME_CARD);
        if (both) {
            button(B_SD, "Installiert", st.target->cart() ? C_BG2 : C_SEL, st.target->cart() ? C_WHITE : C_TEXT, 0.45f);
            button(B_CART, "Modul", st.target->cart() ? C_SEL : C_BG2, st.target->cart() ? C_TEXT : C_WHITE, 0.45f);
        }
        text("Team, PC, Beutel und Geld werden übertragen,", 160, both ? 126 : 104, 0.42f, C_LIGHT, CENTER);
        text("danach startet das Spiel. Vorher wird alles gesichert.", 160, both ? 142 : 120, 0.42f, C_LIGHT, CENTER, 300);
        if (st.current) text("Hast du im aktuellen Spiel gespeichert?", 160, 162, 0.48f, C_SEL, CENTER, 300);
    }
        button(B_YES, "Reisen (A)", C_OK, C_WHITE, 0.55f);
        button(B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.55f);
        break;
    case Screen::Busy: {
        text(std::string("Reise nach ") + st.target->game->region, 160, 4, 0.6f, C_WHITE, CENTER);
        float y = 40;
        for (size_t i = 0; i < st.steps.size(); i++, y += 22)
            text((i + 1 < st.steps.size() ? "✓ " : "… ") + st.steps[i], 16, y, 0.48f, i + 1 < st.steps.size() ? C_OK : C_WHITE);
        break;
    }
    case Screen::Done:
        text("Gute Reise!", 160, 4, 0.6f, C_WHITE, CENTER);
        text(st.message, 160, 60, 0.48f, C_WHITE, CENTER, 300);
        button(B_YES, "Spiel starten (A)", C_OK, C_WHITE, 0.5f);
        button(B_NO, "Menü (B)", C_BG2, C_WHITE, 0.55f);
        break;
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

// ---------- Reisen ----------
State* g_state = nullptr;

void step(const char* t) {
    g_state->steps.push_back(t);
    render(*g_state);
}

bool backup(const char* stamp, const std::string& what, const std::vector<u8>& data) {
    std::string p = std::string(BACKUPS) + "/" + stamp + "-" + what;
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

bool travel(State& st, std::string& err) {
    const SaveSlot* from = st.current; // nullptr: neuer Speicher, nur ankommen
    const SaveSlot& to = *st.target;
    st.steps.clear();
    st.screen = Screen::Busy;

    step("Lese Spielstände");
    std::vector<u8> fromData, toData;
    if ((from && !readSave(*from, fromData, err)) || !readSave(to, toData, err)) return false;
    std::string e;
    // Gen 6/7 über SaveFile, Gen 1/2 über SaveGB
    std::unique_ptr<SaveFile> fromSav, toSav;
    SaveGB fromGB, toGB;
    auto openAny = [&](const SaveSlot& s, const std::vector<u8>& data, std::unique_ptr<SaveFile>& sf, SaveGB& gb) {
        if (s.game->gen <= 2) {
            if (!gb.load(data, e)) { err = s.label() + ": " + e; return false; }
            if (!gb.checksumValid()) { err = s.label() + ": Prüfsumme stimmt nicht"; return false; }
            return true;
        }
        sf = openSave(data, e);
        if (!sf) { err = s.label() + ": " + e; return false; }
        if (!sf->valid()) { err = s.label() + ": Prüfsummen stimmen nicht"; return false; }
        return true;
    };
    if ((from && !openAny(*from, fromData, fromSav, fromGB)) || !openAny(to, toData, toSav, toGB)) return false;
    if (((from && from->game->gen <= 2) || to.game->gen <= 2) && !g_species.loaded()) { err = "arten.bin fehlt auf der SD"; return false; }
    auto ctxFor = [&](const Game& g) { return gbCtx(g, st.bank.trainer); };

    step("Lege Sicherungen an");
    mkdir(DIR, 0777);
    mkdir(BACKUPS, 0777);
    char stamp[32];
    timeStamp(stamp, sizeof stamp);
    std::vector<u8> bankData;
    readFileAll(BANK, bankData);
    if ((from && !backup(stamp, from->label() + "-" + from->game->saveFile, fromData)) ||
        !backup(stamp, to.label() + "-" + to.game->saveFile, toData) || !backup(stamp, "speicher.bin", bankData)) {
        err = "Sicherung fehlgeschlagen";
        return false;
    }

    step("Übertrage Pokémon, Items und Geld");
    Bank bank = st.bank;
    if (from && st.synced) bank.leave(); // Stand wurde beim Start übernommen (plus Umräumen in der App)
    else if (from && (from->game->gen <= 2 ? !bank.departGB(fromGB, ctxFor(*from->game), err) : !bank.depart(*fromSav, err))) return false;
    int hidden = 0;
    if (to.game->gen <= 2 ? !bank.arriveGB(toGB, ctxFor(*to.game), hidden, err) : !bank.arrive(*toSav, hidden, err)) return false;
    bank.currentLow = to.game->lowId;
    bank.currentMedia = u8(to.media);

    step(to.game->gen == 7 ? "Berechne Prüfsummen und Signatur" : "Berechne Prüfsummen");
    std::vector<u8> outData;
    if (to.game->gen <= 2) {
        toGB.finalize();
        SaveGB check;
        if (!check.load(toGB.raw(), e) || !check.checksumValid()) { err = "Gegenprobe fehlgeschlagen, nichts geschrieben"; return false; }
        outData = toGB.raw();
    } else {
        toSav->finalize();
        auto check = openSave(toSav->raw(), e);
        if (!check || !check->valid()) { err = "Gegenprobe fehlgeschlagen, nichts geschrieben"; return false; }
        outData = toSav->raw();
    }

    step("Schreibe Spielstand");
    if (!writeSave(to, outData, err)) return false;
    if (!bank.save(BANK, err)) { err = "Spielstand geschrieben, aber Speicher nicht: " + err; return false; }
    writeLast(to.game->lowId);
    st.bank = bank;
    st.current = &to;
    st.synced = true; // der Speicher entspricht jetzt dem Zielspiel
    st.dirty = false;
    st.held = -1;

    st.message = std::to_string(bank.count()) + " Pokémon im Speicher, " + std::to_string(hidden) + " davon in " + to.label() +
                 " nicht sichtbar.";
    return true;
}

// ---------- Eingabe ----------
void startTravel(State& st) {
    if (!st.bankOk || (!st.current && !st.bank.current.empty())) {
        st.message = std::string("Reisen nicht möglich\n") +
                     (st.bankOk ? "Das aktuelle Spiel (" + st.bank.current + ") hat keinen lesbaren Spielstand." : st.bankErr);
        st.screen = Screen::Error;
    } else {
        st.screen = Screen::Travel;
    }
}

void doTravel(State& st) {
    std::string err;
    if (travel(st, err)) {
        st.screen = Screen::Done;
    } else {
        st.message = "Reise abgebrochen\n" + err + "\nDeine Spielstände sind unverändert.";
        st.screen = Screen::Error;
    }
}

void doLaunch(State& st) {
    if (launchGame(*st.target)) {
        st.quit = true;
    } else {
        st.message = std::string("Spiel konnte nicht gestartet werden\nDie Reise ist erledigt.\nBitte starte ") +
                     st.target->label() + " selbst.";
        st.screen = Screen::Error;
    }
}

void requestQuit(State& st) {
    if (st.dirty) st.screen = Screen::SaveMoves;
    else st.quit = true;
}

// A im Team/PC: aufnehmen bzw. ablegen (tauschen).
void pickOrDrop(State& st) {
    if (st.current && !st.synced) {
        st.message = "Umräumen nicht möglich\nDer Spielstand von " + st.current->label() + "\nkonnte beim Start nicht übernommen werden.";
        st.screen = Screen::Error;
        return;
    }
    int pos = st.view == View::Team ? st.teamCursor : Bank::PARTY + st.box * Bank::SLOTS + st.cursor;
    if (st.held < 0) {
        if (entryAt(st, pos).used) st.held = pos;
        return;
    }
    if (st.held != pos) {
        std::swap(entryAt(st, st.held), entryAt(st, pos));
        st.dirty = true;
    }
    st.held = -1;
}

void openList(State& st) {
    st.held = -1;
    st.view = View::List;
    buildList(st);
}

// Pokémon entlassen: Kennung bleibt als „gesehen“ im Speicher, damit es nicht zurückkommt.
void release(State& st) {
    int pos = st.list[size_t(st.listOpen)].pos[size_t(st.entrySel)];
    if (pos < Bank::PARTY) {
        int team = 0;
        for (auto& e : st.bank.party) team += e.used;
        if (team <= 1) {
            st.message = "Entlassen nicht möglich\nDas ist das letzte Pokémon im Team.";
            st.screen = Screen::Error;
            return;
        }
    }
    u16 sp = st.list[size_t(st.listOpen)].species;
    st.bank.remember();
    entryAt(st, pos) = BankEntry{};
    st.dirty = true;
    buildList(st);
    st.listOpen = -1; // Gruppe kann weggefallen sein: neu suchen
    for (size_t i = 0; i < st.list.size(); i++)
        if (st.list[i].species == sp) { st.listOpen = int(i); st.listSel = int(i); }
    if (st.listOpen >= 0) st.entrySel = std::min(st.entrySel, int(st.list[size_t(st.listOpen)].pos.size()) - 1);
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
        if (st.current && !st.synced) {
            st.message = "Entlassen nicht möglich\nDer Spielstand von " + st.current->label() + "\nkonnte beim Start nicht übernommen werden.";
            st.screen = Screen::Error;
        } else {
            st.screen = Screen::Release;
        }
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
            if (st.current) {
                st.target = st.current;
                doTravel(st);
            } else {
                std::string err;
                if (st.bank.save(BANK, err)) st.quit = true;
                else { st.message = "Speichern fehlgeschlagen\n" + err; st.screen = Screen::Error; }
            }
        } else if ((touch && B_NO.hit(t)) || (down & KEY_B)) {
            st.quit = true;
        }
        break;
    case Screen::Release:
        if ((touch && B_YES.hit(t)) || (down & KEY_A)) release(st);
        else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        break;
    case Screen::Menu:
        if (st.view == View::List && (down & (KEY_A | KEY_B | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT))) listInput(st, down);
        else if (st.view != View::Bag && st.view != View::List && (down & KEY_A)) pickOrDrop(st);
        else if (down & KEY_B) st.held = -1;
        else if ((touch && B_TRAVEL.hit(t)) || (down & KEY_X)) { st.held = -1; startTravel(st); }
        else if (touch && B_TEAM.hit(t)) st.view = View::Team;
        else if (touch && B_PC.hit(t)) st.view = View::PC;
        else if (touch && B_BAG.hit(t)) st.view = View::Bag;
        else if (touch && B_LIST.hit(t)) openList(st);
        else if (touch && B_EXIT.hit(t)) requestQuit(st);
        break;
    case Screen::Travel:
        if ((touch && B_BACK.hit(t)) || (down & KEY_B)) { st.screen = Screen::Menu; break; }
        if (touch)
            for (auto& [b, g] : travelButtons(st))
                if (b.hit(t)) {
                    // Standard: das Medium, das man gerade nutzt, sonst installiert
                    const SaveSlot* sd = slotOf(st, g, MEDIATYPE_SD);
                    const SaveSlot* cart = slotOf(st, g, MEDIATYPE_GAME_CARD);
                    bool preferCart = st.current && st.current->cart();
                    st.target = (preferCart && cart) ? cart : (sd ? sd : cart);
                    st.screen = Screen::Confirm;
                }
        break;
    case Screen::Confirm:
        if (touch && B_SD.hit(t) && slotOf(st, st.target->game, MEDIATYPE_SD) && slotOf(st, st.target->game, MEDIATYPE_GAME_CARD))
            st.target = slotOf(st, st.target->game, MEDIATYPE_SD);
        else if (touch && B_CART.hit(t) && slotOf(st, st.target->game, MEDIATYPE_GAME_CARD) && slotOf(st, st.target->game, MEDIATYPE_SD))
            st.target = slotOf(st, st.target->game, MEDIATYPE_GAME_CARD);
        else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Travel;
        else if ((touch && B_YES.hit(t)) || (down & KEY_A)) doTravel(st);
        break;
    case Screen::Done:
        if ((touch && B_YES.hit(t)) || (down & KEY_A)) doLaunch(st);
        else if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        break;
    case Screen::Error:
        if ((touch && B_BACK.hit(t)) || (down & KEY_B)) st.screen = Screen::Menu;
        break;
    case Screen::Busy: break;
    }
}

} // namespace

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
    loadState(st);

    while (aptMainLoop() && !st.quit) {
        hidScanInput();
        u32 down = hidKeysDown();
        touchPosition t{};
        if (down & KEY_TOUCH) hidTouchRead(&t);
        if (down) input(st, down, t);
        render(st);
    }

    C2D_TextBufDelete(g_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
