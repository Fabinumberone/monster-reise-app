// Pokémon World 3DS – Reise-App
// Oben: Trainer, Region und die gewählte Ansicht (Team, PC, Beutel). Unten: Menü.
// Steuerung: Steuerkreuz wählen, A bestätigen, B zurück, L/R Box wechseln, START beenden.
#include <3ds.h>

#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "bank.hpp"
#include "games.hpp"
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

PrintConsole topCon, botCon;
Names g_names;

// ---------- Text: UTF-8 -> Zeichensatz der Konsole (CP437) ----------
std::string cp437(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        if (c < 0x80) { o += char(c); i++; continue; }
        unsigned cp = 0;
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 1;
        if (len == 2 && i + 1 < s.size()) cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        else if (len == 3 && i + 2 < s.size()) cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        i += len;
        switch (cp) {
        case 0xE4: o += char(0x84); break; // ä
        case 0xF6: o += char(0x94); break; // ö
        case 0xFC: o += char(0x81); break; // ü
        case 0xC4: o += char(0x8E); break; // Ä
        case 0xD6: o += char(0x99); break; // Ö
        case 0xDC: o += char(0x9A); break; // Ü
        case 0xDF: o += char(0xE1); break; // ß
        case 0xE9: o += char(0x82); break; // é
        case 0xD7: o += 'x'; break;        // ×
        case 0x2192: o += char(0x1A); break; // →
        case 0x2642: o += char(0x0B); break; // ♂
        case 0x2640: o += char(0x0C); break; // ♀
        default: o += '?';
        }
    }
    return o;
}

void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(cp437(buf).c_str(), stdout);
}

const char* HL = "\x1b[33m";  // gelb
const char* DIM = "\x1b[90m"; // grau
const char* OK = "\x1b[32m";  // grün
const char* ERR = "\x1b[31m"; // rot
const char* RST = "\x1b[0m";

// ---------- Zustand ----------
enum class Screen { Menu, Travel, Confirm, Done, Error };
enum class View { Team, PC, Bag };

struct State {
    Bank bank;
    bool bankOk = false;
    std::string bankErr;
    std::vector<const Game*> installed; // Spiele mit Spielstand
    const Game* current = nullptr;      // Spiel, in dem man sich befindet
    Screen screen = Screen::Menu;
    View view = View::Team;
    int menuSel = 0, travelSel = 0, box = 0;
    const Game* target = nullptr;
    std::string message;
    bool quit = false, dirty = true;
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

void loadState(State& st) {
    g_names.load(NAMES);
    st.installed.clear();
    for (auto& g : allGames())
        if (hasSave(g)) st.installed.push_back(&g);
    st.bankOk = st.bank.load(BANK, st.bankErr);
    st.current = nullptr;
    if (!st.bankOk) return;
    const Game* last = gameByLowId(readLast());
    if (last && st.bank.current == kindName(last->kind)) st.current = last;
    if (!st.current)
        for (const Game* g : st.installed)
            if (st.bank.current == kindName(g->kind)) { st.current = g; break; }
}

// ---------- Ansichten (oberer Bildschirm) ----------
// Mit Leerzeichen auf width Zeichen auffüllen (Zeichen, nicht UTF-8-Bytes).
std::string pad(const std::string& s, size_t width) {
    size_t chars = 0;
    for (unsigned char c : s) chars += (c & 0xC0) != 0x80;
    return chars >= width ? s : s + std::string(width - chars, ' ');
}

// Auf höchstens width Zeichen kürzen.
std::string cut(const std::string& s, size_t width) {
    size_t chars = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && chars++ == width) return s.substr(0, i);
    }
    return s;
}

std::string monName(const PK6& pk) {
    if (pk.isEgg()) return "Ei";
    std::string art = g_names.species(pk.species());
    std::string nick = pk.nickname();
    if (pk.isNicknamed() && !nick.empty() && nick != art) return nick + " (" + art + ")";
    return art;
}

std::string monLine(const BankEntry& e, GameKind here) {
    const PK6& pk = e.pk;
    std::string s = pad(monName(pk), 26);
    char buf[32];
    if (pk.partyLevel()) { std::snprintf(buf, sizeof buf, "Lv.%3u", pk.partyLevel()); s += buf; } // nur im Team
    if (!pk.isEgg()) s += pk.gender() == 0 ? " ♂" : pk.gender() == 1 ? " ♀" : "  ";
    if (pk.isShiny()) s += " *";
    if (!fitsIn(pk, here)) s += std::string("\n     ") + DIM + "(hier nicht sichtbar)" + RST;
    return s;
}

std::string moveLine(const PK6& pk) {
    std::string s;
    for (uint16_t m : pk.moves())
        if (m) s += (s.empty() ? "" : ", ") + g_names.move(m);
    return s;
}

void drawTop(const State& st) {
    consoleSelect(&topCon);
    consoleClear();
    say("%sPokémon World 3DS%s\n", HL, RST);
    if (!st.bankOk) {
        say("\n%sKein gemeinsamer Speicher gefunden.%s\n%s\n\nEinrichten am PC:\npw3d-reise speicher-neu …\n", ERR, RST,
            st.bankErr.c_str());
        return;
    }
    const Trainer6& t = st.bank.trainer;
    say("Trainer %s  ID %05u  Geld %lu P\n", t.name.c_str(), t.tid, (unsigned long)st.bank.money);
    if (st.current) say("Aktuell: %s (%s)\n", st.current->region, st.current->name);
    else say("Aktuell: %s\n", st.bank.current.empty() ? "unterwegs" : st.bank.current.c_str());
    say("--------------------------------------------------\n");

    GameKind here = st.current ? st.current->kind : GameKind::USUM;
    switch (st.view) {
    case View::Team:
        say("%sTeam%s\n\n", HL, RST);
        for (int i = 0; i < Bank::PARTY; i++)
            if (st.bank.party[i].used) {
                say(" %d. %s\n", i + 1, monLine(st.bank.party[i], here).c_str());
                say("    %s%s%s\n", DIM, moveLine(st.bank.party[i].pk).c_str(), RST);
            }
        if (!g_names.loaded()) say("\n%sNamen fehlen: namen.txt mit tools/gen-namen.mjs\nerzeugen.%s\n", DIM, RST);
        break;
    case View::PC: {
        say("%sPC  Box %d/%d%s   (L/R wechseln, grau = hier nicht sichtbar)\n\n", HL, st.box + 1, Bank::BOXES, RST);
        // Zwei Spalten à 15 Plätze, damit eine volle Box auf den Bildschirm passt.
        auto cell = [&](int s) {
            const BankEntry& e = st.bank.boxes[st.box * Bank::SLOTS + s];
            char num[8];
            std::snprintf(num, sizeof num, "%2d ", s + 1);
            if (!e.used) return std::string(num) + DIM + pad("-", 21) + RST;
            std::string name = pad(cut(monName(e.pk), 20), 21);
            return std::string(num) + (fitsIn(e.pk, here) ? "" : DIM) + name + RST;
        };
        for (int r = 0; r < Bank::SLOTS / 2; r++) say("%s %s\n", cell(r).c_str(), cell(r + Bank::SLOTS / 2).c_str());
        if (st.box == 31 && kindGeneration(here) == 6) say("\n %sBox 32 gibt es nur in Gen 7.%s\n", DIM, RST);
        break;
    }
    case View::Bag: {
        say("%sBeutel%s  (nur Taschen, die mitreisen)\n\n", HL, RST);
        if (st.bank.items.empty()) say(" %s(leer)%s\n", DIM, RST);
        for (const BankItem& b : st.bank.items) {
            Pouch p = pouchOf(here, b.item);
            char cnt[16];
            std::snprintf(cnt, sizeof cnt, "x%-4lu ", (unsigned long)b.count);
            say(" %s%s%s%s%s\n", pad(g_names.item(b.item), 22).c_str(), cnt, DIM,
                p == Pouch::None ? "(hier nicht sichtbar)" : pouchName(p), RST);
        }
        break;
    }
    }
}

// ---------- Menü (unterer Bildschirm) ----------
const char* MENU[] = {"Reisen", "Team ansehen", "PC ansehen", "Beutel ansehen", "Beenden"};
constexpr int MENU_N = 5;

void drawBottom(const State& st) {
    consoleSelect(&botCon);
    consoleClear();
    switch (st.screen) {
    case Screen::Menu:
        say("%sMenü%s\n\n", HL, RST);
        for (int i = 0; i < MENU_N; i++) say(" %s %s\n", i == st.menuSel ? "\x10" : " ", MENU[i]);
        say("\n%sA: wählen   START: beenden%s\n", DIM, RST);
        break;
    case Screen::Travel:
        say("%sWohin möchtest du reisen?%s\n\n", HL, RST);
        for (size_t i = 0; i < st.installed.size(); i++) {
            const Game* g = st.installed[i];
            say(" %s %-7s %s%s\n", int(i) == st.travelSel ? "\x10" : " ", g->region, g->name,
                g == st.current ? "  (hier)" : "");
        }
        say("\n%sA: wählen   B: zurück%s\n", DIM, RST);
        break;
    case Screen::Confirm:
        say("%sReise bestätigen%s\n\n", HL, RST);
        say("Von:  %s (%s)\n", st.current ? st.current->name : "?", st.current ? st.current->region : "?");
        say("Nach: %s (%s)\n\n", st.target->name, st.target->region);
        say("Team, PC, Beutel und Geld werden\nübertragen, danach startet das Spiel.\nVorher wird alles gesichert.\n\n");
        say("Hast du im aktuellen Spiel gespeichert?\n\n%sA: reisen   B: abbrechen%s\n", DIM, RST);
        break;
    case Screen::Done:
    case Screen::Error:
        say("%s", st.message.c_str());
        say("\n\n%s%s%s\n", DIM, st.screen == Screen::Done ? "A: Spiel starten" : "B: zurück", RST);
        break;
    }
}

// ---------- Reisen ----------
void step(const char* text) {
    consoleSelect(&botCon);
    say("%s\n", text);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

bool backup(const char* stamp, const char* what, const std::vector<u8>& data) {
    std::string p = std::string(BACKUPS) + "/" + stamp + "-" + what;
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

bool travel(State& st, std::string& err) {
    const Game& from = *st.current;
    const Game& to = *st.target;
    consoleSelect(&botCon);
    consoleClear();
    say("%sReise nach %s%s\n\n", HL, to.region, RST);

    step("Lese Spielstände …");
    std::vector<u8> fromData, toData;
    if (!readSave(from, fromData, err) || !readSave(to, toData, err)) return false;
    std::string e;
    auto fromSav = openSave(fromData, e);
    auto toSav = openSave(toData, e);
    if (!fromSav || !toSav) { err = "Spielstand nicht erkannt: " + e; return false; }
    if (!fromSav->valid()) { err = std::string(from.name) + ": Prüfsummen stimmen nicht"; return false; }
    if (!toSav->valid()) { err = std::string(to.name) + ": Prüfsummen stimmen nicht"; return false; }

    step("Lege Sicherungen an …");
    mkdir(DIR, 0777);
    mkdir(BACKUPS, 0777);
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    FILE* bf = std::fopen(BANK, "rb");
    std::vector<u8> bankData;
    if (bf) {
        std::fseek(bf, 0, SEEK_END);
        bankData.resize(size_t(std::ftell(bf)));
        std::fseek(bf, 0, SEEK_SET);
        std::fread(bankData.data(), 1, bankData.size(), bf);
        std::fclose(bf);
    }
    if (!backup(stamp, (std::string(from.name) + ".main").c_str(), fromData) ||
        !backup(stamp, (std::string(to.name) + ".main").c_str(), toData) || !backup(stamp, "speicher.bin", bankData)) {
        err = "Sicherung fehlgeschlagen";
        return false;
    }

    step("Übertrage Pokémon, Items und Geld …");
    Bank bank = st.bank;
    if (!bank.depart(*fromSav, err)) return false;
    int hidden = 0;
    if (!bank.arrive(*toSav, hidden, err)) return false;

    step("Berechne Prüfsummen und Signatur …");
    toSav->finalize();
    auto check = openSave(toSav->raw(), e);
    if (!check || !check->valid()) { err = "Gegenprobe fehlgeschlagen, nichts geschrieben"; return false; }

    step("Schreibe Spielstand …");
    if (!writeSave(to, toSav->raw(), err)) return false;
    if (!bank.save(BANK, err)) { err = "Spielstand geschrieben, aber Speicher nicht: " + err; return false; }
    writeLast(to.lowId);
    st.bank = bank;
    st.current = &to;

    char msg[256];
    std::snprintf(msg, sizeof msg, "%sGute Reise!%s\n\n%d Pokémon im Speicher,\n%d davon in %s nicht sichtbar.", OK,
                  RST, bank.count(), hidden, to.name);
    st.message = msg;
    return true;
}

// ---------- Eingabe ----------
void input(State& st, u32 down) {
    if (down & KEY_START) { st.quit = true; return; }
    switch (st.screen) {
    case Screen::Menu:
        if (down & KEY_DOWN) st.menuSel = (st.menuSel + 1) % MENU_N;
        if (down & KEY_UP) st.menuSel = (st.menuSel + MENU_N - 1) % MENU_N;
        if (down & KEY_A) {
            switch (st.menuSel) {
            case 0:
                if (!st.bankOk || !st.current) {
                    st.message = std::string(ERR) + "Reisen nicht möglich:" + RST + "\n\n" +
                                 (st.bankOk ? "Das aktuelle Spiel (" + st.bank.current + ") hat keinen Spielstand."
                                            : "Kein gemeinsamer Speicher.");
                    st.screen = Screen::Error;
                } else {
                    st.screen = Screen::Travel;
                    st.travelSel = 0;
                }
                break;
            case 1: st.view = View::Team; break;
            case 2: st.view = View::PC; break;
            case 3: st.view = View::Bag; break;
            case 4: st.quit = true; break;
            }
        }
        if (st.view == View::PC) {
            if (down & KEY_R) st.box = (st.box + 1) % Bank::BOXES;
            if (down & KEY_L) st.box = (st.box + Bank::BOXES - 1) % Bank::BOXES;
        }
        break;
    case Screen::Travel: {
        int n = int(st.installed.size());
        if (n && (down & KEY_DOWN)) st.travelSel = (st.travelSel + 1) % n;
        if (n && (down & KEY_UP)) st.travelSel = (st.travelSel + n - 1) % n;
        if (down & KEY_B) st.screen = Screen::Menu;
        if (n && (down & KEY_A)) {
            st.target = st.installed[st.travelSel];
            st.screen = Screen::Confirm;
        }
        break;
    }
    case Screen::Confirm:
        if (down & KEY_B) st.screen = Screen::Travel;
        if (down & KEY_A) {
            std::string err;
            if (travel(st, err)) {
                st.screen = Screen::Done;
            } else {
                st.message = std::string(ERR) + "Reise abgebrochen:" + RST + "\n\n" + err +
                             "\n\nDeine Spielstände sind unverändert.";
                st.screen = Screen::Error;
            }
        }
        break;
    case Screen::Done:
        if (down & KEY_A) {
            if (launchGame(*st.target)) {
                st.quit = true;
            } else {
                st.message = std::string(ERR) + "Spiel konnte nicht gestartet werden." + RST +
                             "\n\nDie Reise ist trotzdem erledigt.\nBitte starte " + st.target->name + " selbst.";
                st.screen = Screen::Error;
            }
        }
        break;
    case Screen::Error:
        if (down & KEY_B) st.screen = Screen::Menu;
        break;
    }
    st.dirty = true;
}

} // namespace

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, &topCon);
    consoleInit(GFX_BOTTOM, &botCon);

    State st;
    consoleSelect(&botCon);
    say("Lade …\n");
    loadState(st);

    while (aptMainLoop() && !st.quit) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down) input(st, down);
        if (st.dirty) {
            drawTop(st);
            drawBottom(st);
            st.dirty = false;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    return 0;
}
