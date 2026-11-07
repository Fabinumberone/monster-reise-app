// Pokémon World 3DS – Reise-App mit grafischer Oberfläche (citro2d, Systemschrift).
// Oben: Kopf mit Trainer und Region, darunter Team, PC oder Beutel.
// Unten: Touch-Menü, Regionswahl und Reiseablauf.
// Tasten: Steuerkreuz bewegt die Auswahl oben (PC/Beutel), L/R wechselt die Box,
//         Y wechselt die Ansicht, X öffnet „Reisen“, A/B bestätigen/abbrechen, START beendet.
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
enum class Screen { Menu, Travel, Confirm, Busy, Done, Error };
enum class View { Team, PC, Bag };

struct State {
    Bank bank;   // gemeinsamer Speicher (wie gespeichert)
    Bank shown;  // Anzeige: Speicher plus aktueller Spielstand (Vorschau einer Abreise)
    bool bankOk = false;
    std::string bankErr;
    std::vector<const Game*> installed;
    const Game* current = nullptr;
    const Game* target = nullptr;
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

// Vorschau: so, als würde man jetzt aus dem aktuellen Spiel abreisen (Speicher bleibt unverändert).
void makePreview(State& st) {
    st.shown = st.bank;
    if (!st.current) return;
    std::vector<u8> data;
    std::string err;
    if (!readSave(*st.current, data, err)) return;
    Bank b = st.bank;
    if (st.current->gen <= 2) {
        SaveGB s;
        if (!g_species.loaded() || !s.load(data, err) || !s.checksumValid()) return;
        if (!b.departGB(s, gbCtx(*st.current, b.trainer), err)) return;
    } else {
        auto s = openSave(data, err);
        if (!s || !s->valid() || !b.depart(*s, err)) return;
    }
    st.shown = b;
}

void loadState(State& st) {
    g_names.load(NAMES);
    g_species.load(SPECIES);
    g_icons.load(ICONS);
    st.installed.clear();
    for (auto& g : allGames())
        if (hasSave(g)) st.installed.push_back(&g);
    st.bankOk = st.bank.load(BANK, st.bankErr);
    st.current = nullptr;
    if (!st.bankOk) return;
    const Game* last = gameByLowId(readLast());
    if (last && st.bank.current == bankKind(*last)) st.current = last;
    if (!st.current)
        for (const Game* g : st.installed)
            if (st.bank.current == bankKind(*g)) { st.current = g; break; }
    makePreview(st);
}

// Ist das Pokémon im aktuellen Spiel sichtbar?
bool visibleHere(const State& st, const PK6& pk) {
    if (!st.current) return true;
    if (st.current->gen <= 2) return fitsGB(pk, st.current->gen);
    return fitsIn(pk, st.current->kind);
}

// Wo liegt das Item im aktuellen Spiel? nullptr = dort nicht sichtbar.
const char* itemPlaceHere(const State& st, uint16_t item) {
    if (!st.current) return "";
    if (st.current->gen == 1) return modernItemToGen1(item) ? "Beutel" : nullptr;
    if (st.current->gen == 2) {
        if (!modernItemToGen2(item) || pouchOf(GameKind::USUM, item) == Pouch::None) return nullptr;
        return (item <= 16 || (item >= 492 && item <= 500)) ? "Ball-Tasche" : "Item-Tasche";
    }
    Pouch p = pouchOf(st.current->kind, item);
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
        u32 rc = regionColor(st.current->region);
        std::string r = std::string(st.current->region) + " · " + st.current->name;
        rect(250, 4, 144, 18, rc);
        text(r, 322, 5, 0.45f, C_WHITE, CENTER, 136);
    }
    rect(0, 26, 400, 18, C_BG2);
    if (st.bankOk) {
        const Trainer6& t = st.shown.trainer;
        text(t.name + "   ID " + std::to_string(t.tid) + "   Geld " + money(st.shown.money), 8, 28, 0.45f, C_LIGHT);
        text(std::to_string(st.shown.count()) + " Pokémon", 392, 28, 0.45f, C_LIGHT, RIGHT);
    }
}

void drawTeam(const State& st) {
    int n = 0;
    for (int i = 0; i < Bank::PARTY; i++) {
        const BankEntry& e = st.shown.party[i];
        if (!e.used) continue;
        float x = 8 + (n % 2) * 196, y = 50 + (n / 2) * 62;
        n++;
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
            float f = maxHp ? std::min(1.0f, float(hp) / maxHp) : 0;
            u32 hc = f > 0.5f ? C_OK : f > 0.2f ? C_SEL : C_ERR;
            rect(x + 46, y + 42, 100, 8, C2D_Color32(0xD8, 0xD3, 0xC8, 0xFF));
            rect(x + 46, y + 42, 100 * f, 8, hc);
            text(std::to_string(hp) + "/" + std::to_string(maxHp), x + 182, y + 38, 0.4f, C_MUTED, RIGHT);
        }
    }
    if (!n) text("Kein Pokémon im Team", 200, 120, 0.6f, C_LIGHT, CENTER);
}

void drawPC(const State& st) {
    text("◀  Box " + std::to_string(st.box + 1) + "  ▶", 146, 48, 0.55f, C_WHITE, CENTER);
    text("L / R", 146, 66, 0.38f, C_LIGHT, CENTER);
    for (int s = 0; s < Bank::SLOTS; s++) {
        const BankEntry& e = st.shown.boxes[st.box * Bank::SLOTS + s];
        float x = 8 + (s % 6) * 46, y = 82 + (s / 6) * 31;
        rect(x, y, 44, 29, C_BG2);
        if (e.used) drawIcon(e.pk, x + 2, y - 1, 1.0f, !visibleHere(st, e.pk));
        if (s == st.cursor) frame(x, y, 44, 29, C_SEL);
    }
    // Detailansicht rechts
    const BankEntry& sel = st.shown.boxes[st.box * Bank::SLOTS + st.cursor];
    rect(290, 48, 102, 188, C_CARD);
    if (!sel.used) {
        text("leer", 341, 130, 0.5f, C_MUTED, CENTER);
        return;
    }
    bool vis = visibleHere(st, sel.pk);
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
    if (st.shown.items.empty()) { text("leer", 200, 130, 0.6f, C_LIGHT, CENTER); return; }
    for (int r = 0; r < BAG_ROWS; r++) {
        int i = st.bagScroll + r;
        if (i >= int(st.shown.items.size())) break;
        const BankItem& b = st.shown.items[size_t(i)];
        const char* place = itemPlaceHere(st, b.item);
        float y = 70 + r * 17;
        rect(8, y, 384, 16, r % 2 ? C_BG2 : C_BG);
        u32 c = place ? C_WHITE : C_CARD_DIM;
        text(g_names.item(b.item), 14, y + 1, 0.45f, c, LEFT, 210);
        text("× " + std::to_string(b.count), 280, y + 1, 0.45f, c, RIGHT);
        text(place ? place : "hier nicht sichtbar", 386, y + 2, 0.4f, place ? C_LIGHT : C_ERR, RIGHT);
    }
    if (st.shown.items.size() > size_t(BAG_ROWS))
        text("▲▼ blättern", 392, 242 - 18, 0.38f, C_LIGHT, RIGHT);
}

void drawTop(const State& st) {
    drawHeader(st);
    if (!st.bankOk) {
        text("Kein gemeinsamer Speicher gefunden.", 200, 100, 0.55f, C_ERR, CENTER);
        text("Einrichten am PC: pw3d-reise speicher-neu …", 200, 124, 0.45f, C_LIGHT, CENTER);
        return;
    }
    switch (st.view) {
    case View::Team: drawTeam(st); break;
    case View::PC: drawPC(st); break;
    case View::Bag: drawBag(st); break;
    }
    if (!g_icons.loaded() || !g_names.loaded())
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

const Button B_TRAVEL{8, 34, 148, 66}, B_TEAM{164, 34, 148, 66}, B_PC{8, 108, 148, 66}, B_BAG{164, 108, 148, 66};
const Button B_EXIT{8, 196, 304, 36}, B_BACK{8, 200, 304, 32};
const Button B_YES{8, 186, 148, 46}, B_NO{164, 186, 148, 46};

// Schaltflächen der Regionswahl: fünf Spalten, je Region die installierten Spiele.
constexpr int NREG = 5;
const char* REGIONS[NREG] = {"Kanto", "Johto", "Hoenn", "Kalos", "Alola"};
constexpr float COLW = 60, COLGAP = 3, COLX = 4;
std::vector<std::pair<Button, const Game*>> travelButtons(const State& st) {
    std::vector<std::pair<Button, const Game*>> out;
    for (int r = 0; r < NREG; r++) {
        int n = 0;
        for (const Game* g : st.installed)
            if (std::string(g->region) == REGIONS[r])
                out.push_back({Button{COLX + r * (COLW + COLGAP), 56.0f + n++ * 34, COLW, 30}, g});
    }
    return out;
}

void drawBottom(const State& st) {
    rect(0, 0, 320, 26, C_HEAD);
    switch (st.screen) {
    case Screen::Menu:
        text("Menü", 160, 4, 0.6f, C_WHITE, CENTER);
        button(B_TRAVEL, "Reisen", C2D_Color32(0x3F, 0xB5, 0x5A, 0xFF), C_WHITE, 0.75f);
        button(B_TEAM, "Team", st.view == View::Team ? C_SEL : C_BG2, st.view == View::Team ? C_TEXT : C_WHITE, 0.7f);
        button(B_PC, "PC", st.view == View::PC ? C_SEL : C_BG2, st.view == View::PC ? C_TEXT : C_WHITE, 0.7f);
        button(B_BAG, "Beutel", st.view == View::Bag ? C_SEL : C_BG2, st.view == View::Bag ? C_TEXT : C_WHITE, 0.7f);
        text("X: Reisen   Y: Ansicht   L/R: Box", 160, 178, 0.4f, C_LIGHT, CENTER);
        button(B_EXIT, "Beenden", C_BG2, C_LIGHT, 0.55f);
        break;
    case Screen::Travel: {
        text("Wohin möchtest du reisen?", 160, 4, 0.55f, C_WHITE, CENTER);
        for (int r = 0; r < NREG; r++) {
            rect(COLX + r * (COLW + COLGAP), 32, COLW, 20, regionColor(REGIONS[r]));
            text(REGIONS[r], COLX + r * (COLW + COLGAP) + COLW / 2, 33, 0.45f, C_WHITE, CENTER, COLW - 4);
        }
        for (auto& [b, g] : travelButtons(st)) {
            bool isHere = g == st.current;
            button(b, g->name, isHere ? C_BG2 : C_CARD, isHere ? C_LIGHT : C_TEXT, 0.42f);
            if (isHere) text("hier", b.x + b.w - 4, b.y + 1, 0.32f, C_SEL, RIGHT);
        }
        button(B_BACK, "Zurück", C_BG2, C_WHITE, 0.55f);
        break;
    }
    case Screen::Confirm:
        text("Reise bestätigen", 160, 4, 0.6f, C_WHITE, CENTER);
        rect(8, 36, 304, 26, regionColor(st.current ? st.current->region : "Alola"));
        text(std::string("Von: ") + (st.current ? st.current->name : "?") + " (" + (st.current ? st.current->region : "?") + ")", 16, 40, 0.5f, C_WHITE, LEFT, 288);
        rect(8, 66, 304, 26, regionColor(st.target->region));
        text(std::string("Nach: ") + st.target->name + " (" + st.target->region + ")", 16, 70, 0.5f, C_WHITE, LEFT, 288);
        text("Team, PC, Beutel und Geld werden übertragen,", 160, 104, 0.42f, C_LIGHT, CENTER);
        text("danach startet das Spiel. Vorher wird alles gesichert.", 160, 120, 0.42f, C_LIGHT, CENTER, 300);
        text("Hast du im aktuellen Spiel gespeichert?", 160, 148, 0.48f, C_SEL, CENTER, 300);
        button(B_YES, "Reisen (A)", C_OK, C_WHITE, 0.55f);
        button(B_NO, "Abbrechen (B)", C_BG2, C_WHITE, 0.55f);
        break;
    case Screen::Busy: {
        text(std::string("Reise nach ") + st.target->region, 160, 4, 0.6f, C_WHITE, CENTER);
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
    const Game& from = *st.current;
    const Game& to = *st.target;
    st.steps.clear();
    st.screen = Screen::Busy;

    step("Lese Spielstände");
    std::vector<u8> fromData, toData;
    if (!readSave(from, fromData, err) || !readSave(to, toData, err)) return false;
    std::string e;
    // Gen 6/7 über SaveFile, Gen 1/2 über SaveGB
    std::unique_ptr<SaveFile> fromSav, toSav;
    SaveGB fromGB, toGB;
    auto openAny = [&](const Game& g, const std::vector<u8>& data, std::unique_ptr<SaveFile>& sf, SaveGB& gb) {
        if (g.gen <= 2) {
            if (!gb.load(data, e)) { err = std::string(g.name) + ": " + e; return false; }
            if (!gb.checksumValid()) { err = std::string(g.name) + ": Prüfsumme stimmt nicht"; return false; }
            return true;
        }
        sf = openSave(data, e);
        if (!sf) { err = std::string(g.name) + ": " + e; return false; }
        if (!sf->valid()) { err = std::string(g.name) + ": Prüfsummen stimmen nicht"; return false; }
        return true;
    };
    if (!openAny(from, fromData, fromSav, fromGB) || !openAny(to, toData, toSav, toGB)) return false;
    if ((from.gen <= 2 || to.gen <= 2) && !g_species.loaded()) { err = "arten.bin fehlt auf der SD"; return false; }
    auto ctxFor = [&](const Game& g) { return gbCtx(g, st.bank.trainer); };

    step("Lege Sicherungen an");
    mkdir(DIR, 0777);
    mkdir(BACKUPS, 0777);
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    std::vector<u8> bankData;
    if (FILE* bf = std::fopen(BANK, "rb")) {
        std::fseek(bf, 0, SEEK_END);
        bankData.resize(size_t(std::ftell(bf)));
        std::fseek(bf, 0, SEEK_SET);
        std::fread(bankData.data(), 1, bankData.size(), bf);
        std::fclose(bf);
    }
    if (!backup(stamp, std::string(from.name) + "-" + from.saveFile, fromData) ||
        !backup(stamp, std::string(to.name) + "-" + to.saveFile, toData) || !backup(stamp, "speicher.bin", bankData)) {
        err = "Sicherung fehlgeschlagen";
        return false;
    }

    step("Übertrage Pokémon, Items und Geld");
    Bank bank = st.bank;
    if (from.gen <= 2 ? !bank.departGB(fromGB, ctxFor(from), err) : !bank.depart(*fromSav, err)) return false;
    int hidden = 0;
    if (to.gen <= 2 ? !bank.arriveGB(toGB, ctxFor(to), hidden, err) : !bank.arrive(*toSav, hidden, err)) return false;

    step(to.gen == 7 ? "Berechne Prüfsummen und Signatur" : "Berechne Prüfsummen");
    std::vector<u8> outData;
    if (to.gen <= 2) {
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
    writeLast(to.lowId);
    st.bank = bank;
    st.shown = bank;
    st.current = &to;

    st.message = std::to_string(bank.count()) + " Pokémon im Speicher, " + std::to_string(hidden) + " davon in " + to.name +
                 " nicht sichtbar.";
    return true;
}

// ---------- Eingabe ----------
void startTravel(State& st) {
    if (!st.bankOk || !st.current) {
        st.message = std::string("Reisen nicht möglich\n") +
                     (st.bankOk ? "Das aktuelle Spiel (" + st.bank.current + ") hat keinen Spielstand." : "Kein gemeinsamer Speicher.");
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
                     st.target->name + " selbst.";
        st.screen = Screen::Error;
    }
}

void input(State& st, u32 down, const touchPosition& t) {
    bool touch = down & KEY_TOUCH;
    if (down & KEY_START) { st.quit = true; return; }

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
        if (st.view == View::Bag) {
            int maxScroll = std::max(0, int(st.shown.items.size()) - BAG_ROWS);
            if (down & KEY_DDOWN) st.bagScroll = std::min(maxScroll, st.bagScroll + 1);
            if (down & KEY_DUP) st.bagScroll = std::max(0, st.bagScroll - 1);
        }
        if (down & KEY_Y) st.view = st.view == View::Team ? View::PC : st.view == View::PC ? View::Bag : View::Team;
    }

    switch (st.screen) {
    case Screen::Menu:
        if ((touch && B_TRAVEL.hit(t)) || (down & KEY_X)) startTravel(st);
        else if (touch && B_TEAM.hit(t)) st.view = View::Team;
        else if (touch && B_PC.hit(t)) st.view = View::PC;
        else if (touch && B_BAG.hit(t)) st.view = View::Bag;
        else if (touch && B_EXIT.hit(t)) st.quit = true;
        break;
    case Screen::Travel:
        if ((touch && B_BACK.hit(t)) || (down & KEY_B)) { st.screen = Screen::Menu; break; }
        if (touch)
            for (auto& [b, g] : travelButtons(st))
                if (b.hit(t)) { st.target = g; st.screen = Screen::Confirm; }
        break;
    case Screen::Confirm:
        if ((touch && B_NO.hit(t)) || (down & KEY_B)) st.screen = Screen::Travel;
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
