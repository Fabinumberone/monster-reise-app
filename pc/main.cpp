// pw3d-reise: PC-Werkzeug zum Prüfen und Übertragen von Spielständen (Gen 6 und 7).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "../core/bank.hpp"
#include "../core/convert.hpp"
#include "../core/items.hpp"
#include "../core/memecrypto.hpp"
#include "../core/save6.hpp"
#include "../core/save7.hpp"
#include "../core/savefile.hpp"

using namespace pw3d;

static const char* USAGE =
    "Aufruf:\n"
    "  pw3d-reise selbsttest\n"
    "  pw3d-reise info <main>                 Spielstand anzeigen\n"
    "  pw3d-reise scan <main>                 gültige Pokémon-Datensätze suchen\n"
    "  pw3d-reise pokedex <main>              alle Pokémon im Pokédex eintragen\n"
    "  pw3d-reise trainer <quelle> <ziel>     Ziel übernimmt Trainer der Quelle\n"
    "  pw3d-reise kopiere <quelle> <teamplatz> <ziel> <box> <platz>\n"
    "  pw3d-reise insteam <quelle> <teamplatz> <ziel> [erwartete Art]\n"
    "  pw3d-reise geld <main> <betrag>        (Gen 7)\n"
    "  pw3d-reise signieren <main> <ausgabe>  (Gen 7, Test)\n"
    "  pw3d-reise speicher-neu <speicher> <startspiel> [weitere …]\n"
    "  pw3d-reise speicher-info <speicher>\n"
    "  pw3d-reise reise <speicher> <von> <nach>\n"
    "  pw3d-reise speicher-items <speicher> <spielstände …>  (Items einmalig dazulegen)\n";

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// Öffnet einen Spielstand; mustBeValid: Prüfsummen/Signatur müssen vorher stimmen.
static std::unique_ptr<SaveFile> open(const std::string& path, bool mustBeValid) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) {
        std::fprintf(stderr, "Kann %s nicht lesen\n", path.c_str());
        return nullptr;
    }
    std::string err;
    auto sav = openSave(std::move(data), err);
    if (!sav) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
        return nullptr;
    }
    if (mustBeValid && !sav->valid()) {
        std::fprintf(stderr, "%s: Prüfsummen/Signatur stimmen schon vor der Änderung nicht, Abbruch\n", path.c_str());
        return nullptr;
    }
    return sav;
}

// Prüfsummen (und Signatur) neu berechnen, Gegenprobe, schreiben.
static bool writeSave(const std::string& path, SaveFile& sav) {
    sav.finalize();
    std::string err;
    auto check = openSave(sav.raw(), err);
    if (!check || !check->valid()) {
        std::fprintf(stderr, "Gegenprobe fehlgeschlagen, nichts geschrieben\n");
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(sav.raw().data()), std::streamsize(sav.raw().size()));
    if (!f) {
        std::fprintf(stderr, "Schreiben fehlgeschlagen\n");
        return false;
    }
    return true;
}

static void printPk(const char* label, const PK6& pk) {
    auto m = pk.moves();
    std::printf("  %-8s Nr. %3u Form %u  \"%s\"  OT \"%s\" %05u/%05u  EP %u  Attacken %u %u %u %u%s%s\n", label,
                pk.species(), pk.form(), pk.nickname().c_str(), pk.otName().c_str(), pk.tid(), pk.sid(),
                unsigned(pk.exp()), m[0], m[1], m[2], m[3], pk.isEgg() ? "  [Ei]" : "",
                pk.checksumValid() ? "" : "  [PRÜFSUMME FALSCH]");
    std::printf("           Spiel %u  Sprache %u  Geschlecht %u%s\n", pk.version(), pk.language(), pk.gender(),
                pk.isShiny() ? "  schillernd" : "");
}

static int cmdInfo(const SaveFile& sav, bool showBlocks) {
    std::printf("Spiel: %s (Gen %d), %zu Byte\n", sav.gameTitle(), sav.generation(), sav.raw().size());
    int bad = 0;
    for (auto& b : sav.blocks()) {
        if (!b.ok()) bad++;
        if (showBlocks || !b.ok())
            std::printf("  Block %04X  @0x%05X  Länge 0x%05X  CRC %04X/%04X %s\n", b.id, unsigned(b.offset),
                        unsigned(b.length), b.storedCrc, b.computedCrc, b.ok() ? "ok" : "FALSCH");
    }
    std::printf("%zu Blöcke, %d mit falscher Prüfsumme\n", sav.blocks().size(), bad);
    if (sav.generation() == 7) {
        bool sig = static_cast<const Save7&>(sav).signatureValid();
        std::printf("Signatur: %s\n", sig ? "gültig" : "UNGÜLTIG");
        if (!sig) bad++;
    }
    auto t = sav.trainer();
    std::printf("\nTrainer: \"%s\"  TID %05u  SID %05u  Geschlecht %u", t.name.c_str(), t.tid, t.sid, t.gender);
    if (sav.generation() == 7) {
        uint32_t id7 = (uint32_t(t.sid) << 16 | t.tid) % 1000000;
        std::printf("  (Anzeige-ID %06u)", unsigned(id7));
    }
    std::printf("  Geld %u\nReisende Items:", unsigned(sav.money()));
    for (const ItemStack& s : readTravelItems(sav))
        std::printf(" %u×%u(%s)", s.item, s.count, pouchName(pouchOf(sav.kind(), s.item)));
    std::printf("\n\nTeam (%d):\n", sav.partyCount());
    for (int i = 0; i < sav.partyCount() && i < 6; i++) {
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "#%d", i + 1);
        PK6 pk = sav.partySlot(i);
        printPk(lbl, pk);
        std::printf("           Level %u\n", pk.partyLevel());
    }
    std::printf("\nBoxen:\n");
    int boxed = 0;
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < SaveFile::BOX_SLOTS; s++) {
            if (sav.boxSlotEmpty(b, s)) continue;
            boxed++;
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "B%02d/%02d", b + 1, s + 1);
            printPk(lbl, sav.boxSlot(b, s));
        }
    std::printf("  %d Pokémon in %d Boxen\n", boxed, sav.boxCount());
    return bad ? 2 : 0;
}

static int cmdScan(const SaveFile& sav) {
    const auto& d = sav.raw();
    std::printf("Suche gültige Datensätze (Prüfsumme + Art 1..%u):\n", sav.maxSpecies());
    int found = 0;
    for (size_t p = 0; p + PK6_BOX_SIZE <= d.size(); p += 4) {
        if (isEmptySlot(&d[p], PK6_BOX_SIZE)) continue;
        PK6 pk = PK6::decrypt(&d[p], PK6_BOX_SIZE);
        if (pk.species() == 0 || pk.species() > sav.maxSpecies() || !pk.checksumValid()) continue;
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "@%05zX", p);
        printPk(lbl, pk);
        found++;
    }
    std::printf("%d gefunden\n", found);
    return 0;
}

// Holt ein Team-Pokémon aus der Quelle und passt es an die Generation des Ziels an.
static bool takeFromParty(const SaveFile& src, int t, const SaveFile& dst, PK6& out) {
    if (t < 0 || t >= src.partyCount()) {
        std::fprintf(stderr, "Ungültiger Teamplatz\n");
        return false;
    }
    PK6 pk = src.partySlot(t);
    if (pk.species() == 0 || !pk.checksumValid()) {
        std::fprintf(stderr, "Quell-Pokémon ungültig, Abbruch\n");
        return false;
    }
    if (src.generation() == 6 && dst.generation() == 7) {
        pk = toGen7(pk);
    } else if (src.generation() == 7 && dst.generation() == 6) {
        std::fprintf(stderr, "Gen 7 -> Gen 6 kommt mit dem gemeinsamen Speicher, noch nicht möglich\n");
        return false;
    }
    if (pk.species() > dst.maxSpecies()) {
        std::fprintf(stderr, "Art %u gibt es im Zielspiel nicht\n", pk.species());
        return false;
    }
    out = pk;
    return true;
}

static int cmdKopiere(char** a) {
    auto src = open(a[0], true), dst = open(a[2], true);
    if (!src || !dst) return 1;
    int b = std::atoi(a[3]) - 1, s = std::atoi(a[4]) - 1;
    if (b < 0 || b >= dst->boxCount() || s < 0 || s >= SaveFile::BOX_SLOTS) {
        std::fprintf(stderr, "Ungültiger Boxplatz\n");
        return 1;
    }
    if (!dst->boxSlotEmpty(b, s)) {
        std::fprintf(stderr, "Box %d Platz %d ist belegt, Abbruch\n", b + 1, s + 1);
        return 1;
    }
    PK6 pk;
    if (!takeFromParty(*src, std::atoi(a[1]) - 1, *dst, pk)) return 1;
    dst->setBoxSlot(b, s, pk);
    dst->registerDex(pk);
    if (std::memcmp(dst->boxSlot(b, s).d.data(), pk.d.data(), PK6_BOX_SIZE) != 0) {
        std::fprintf(stderr, "Gegenprobe fehlgeschlagen, nichts geschrieben\n");
        return 1;
    }
    if (!writeSave(a[2], *dst)) return 1;
    std::printf("Kopiert nach Box %d Platz %d, im Pokédex eingetragen:\n", b + 1, s + 1);
    printPk("", pk);
    return 0;
}

// insteam <quelle> <teamplatz> <ziel> [art]: optional Abbruch, wenn auf dem Platz eine andere Art steht.
static int cmdInsTeam(char** a, const char* expectSpecies) {
    auto src = open(a[0], true), dst = open(a[2], true);
    if (!src || !dst) return 1;
    PK6 pk;
    if (!takeFromParty(*src, std::atoi(a[1]) - 1, *dst, pk)) return 1;
    if (expectSpecies && pk.species() != std::atoi(expectSpecies)) {
        std::fprintf(stderr, "Auf Teamplatz %s steht Art %u, erwartet %s. Abbruch\n", a[1], pk.species(), expectSpecies);
        return 1;
    }
    if (!dst->addToParty(pk)) {
        std::fprintf(stderr, "Team im Ziel ist voll\n");
        return 1;
    }
    dst->registerDex(pk);
    if (!writeSave(a[2], *dst)) return 1;
    std::printf("Ins Team aufgenommen (Platz %d), im Pokédex eingetragen:\n", dst->partyCount());
    printPk("", pk);
    return 0;
}

static int cmdPokedex(const char* path) {
    auto sav = open(path, true);
    if (!sav) return 1;
    int n = 0;
    for (int i = 0; i < sav->partyCount() && i < 6; i++) { sav->registerDex(sav->partySlot(i)); n++; }
    for (int b = 0; b < sav->boxCount(); b++)
        for (int s = 0; s < SaveFile::BOX_SLOTS; s++)
            if (!sav->boxSlotEmpty(b, s)) { sav->registerDex(sav->boxSlot(b, s)); n++; }
    if (!writeSave(path, *sav)) return 1;
    std::printf("%d Pokémon im Pokédex eingetragen\n", n);
    return 0;
}

static int cmdTrainer(const char* srcPath, const char* dstPath) {
    auto src = open(srcPath, true), dst = open(dstPath, true);
    if (!src || !dst) return 1;
    Trainer6 t = src->trainer(), old = dst->trainer();
    int n = dst->adoptTrainer(t);
    if (!writeSave(dstPath, *dst)) return 1;
    std::printf("Trainer \"%s\" %05u/%05u -> \"%s\" %05u/%05u, %d eigene Pokémon umgeschrieben\n", old.name.c_str(),
                old.tid, old.sid, t.name.c_str(), t.tid, t.sid, n);
    return 0;
}

static int cmdGeld(const char* path, const char* amount) {
    auto sav = open(path, true);
    if (!sav) return 1;
    if (sav->generation() != 7) {
        std::fprintf(stderr, "Geld bisher nur für Gen 7\n");
        return 1;
    }
    auto& s7 = static_cast<Save7&>(*sav);
    uint32_t old = s7.money();
    s7.setMoney(uint32_t(std::strtoul(amount, nullptr, 10)));
    if (!writeSave(path, s7)) return 1;
    std::printf("Geld %u -> %u\n", unsigned(old), unsigned(s7.money()));
    return 0;
}

static int cmdSignieren(const char* path, const char* out) {
    auto sav = open(path, false);
    if (!sav || sav->generation() != 7) return 1;
    signSave7(sav->raw());
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(sav->raw().data()), std::streamsize(sav->raw().size()));
    std::printf("Signiert nach %s, Signatur %s\n", out, verifySave7(sav->raw()) ? "gültig" : "UNGÜLTIG");
    return f ? 0 : 1;
}

// ---------- Gemeinsamer Speicher ----------

static void printBank(const Bank& bank) {
    std::printf("Gemeinsamer Speicher: Trainer \"%s\" %05u/%05u, %d Pokémon, aktuelles Spiel: %s\n",
                bank.trainer.name.c_str(), bank.trainer.tid, bank.trainer.sid, bank.count(),
                bank.current.empty() ? "keins" : bank.current.c_str());
    if (bank.moneyKnown) std::printf("Geld: %u\n", unsigned(bank.money));
    else std::printf("Geld: noch nicht übernommen (wird bei der nächsten Abreise gelesen)\n");
    std::printf("Items:");
    for (const BankItem& b : bank.items) {
        std::printf(" %u×%u", b.item, unsigned(b.count));
        if (b.written < b.count) std::printf("(%u sichtbar)", b.written);
    }
    std::printf("\n");
    auto line =[](const char* lbl, const BankEntry& e) {
        printPk(lbl, e.pk);
        std::printf("           %s%s  passt in: %s%s%s%s\n", e.inGame ? "im Spiel" : "nur im Speicher",
                    e.hasGen6 ? ", Gen-6-Stand vorhanden" : "", fitsIn(e.pk, GameKind::XY) ? "XY " : "",
                    fitsIn(e.pk, GameKind::ORAS) ? "ORAS " : "", fitsIn(e.pk, GameKind::SM) ? "SM " : "",
                    fitsIn(e.pk, GameKind::USUM) ? "USUM" : "");
    };
    std::printf("Team:\n");
    for (int i = 0; i < Bank::PARTY; i++)
        if (bank.party[i].used) {
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "#%d", i + 1);
            line(lbl, bank.party[i]);
        }
    std::printf("Boxen:\n");
    for (int i = 0; i < Bank::BOXES * Bank::SLOTS; i++)
        if (bank.boxes[i].used) {
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "B%02d/%02d", i / Bank::SLOTS + 1, i % Bank::SLOTS + 1);
            line(lbl, bank.boxes[i]);
        }
}

// speicher-neu <speicher> <startspiel> [weitere spielstände …]
static int cmdSpeicherNeu(int argc, char** argv) {
    auto start = open(argv[3], true);
    if (!start) return 1;
    std::vector<std::unique_ptr<SaveFile>> others;
    std::vector<const SaveFile*> ptrs;
    for (int i = 4; i < argc; i++) {
        auto o = open(argv[i], true);
        if (!o) return 1;
        ptrs.push_back(o.get());
        others.push_back(std::move(o));
    }
    Bank bank;
    bank.create(*start, ptrs);
    std::string err;
    if (!bank.save(argv[2], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    printBank(bank);
    return 0;
}

// reise <speicher> <von> <nach>: Stand aus „von“ übernehmen und in „nach“ schreiben.
static int cmdReise(const char* bankPath, const char* fromPath, const char* toPath) {
    Bank bank;
    std::string err;
    if (!bank.load(bankPath, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    auto from = open(fromPath, true), to = open(toPath, true);
    if (!from || !to) return 1;
    if (!bank.depart(*from, err)) { std::fprintf(stderr, "Abreise: %s\n", err.c_str()); return 1; }
    int hidden = 0;
    if (!bank.arrive(*to, hidden, err)) { std::fprintf(stderr, "Ankunft: %s\n", err.c_str()); return 1; }
    if (!writeSave(toPath, *to)) return 1;
    if (!bank.save(bankPath, err)) {
        std::fprintf(stderr, "ACHTUNG: Zielspiel geschrieben, aber Speicher nicht: %s\n", err.c_str());
        return 1;
    }
    std::printf("Gereist von %s nach %s: %d Pokémon, davon %d dort nicht sichtbar\n", from->gameTitle(),
                to->gameTitle(), bank.count(), hidden);
    return 0;
}

int runSelftest();

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "selbsttest" && argc == 2) return runSelftest();
    if (cmd == "info" && argc == 3) { auto s = open(argv[2], false); return s ? cmdInfo(*s, false) : 1; }
    if (cmd == "bloecke" && argc == 3) { auto s = open(argv[2], false); return s ? cmdInfo(*s, true) : 1; }
    if (cmd == "scan" && argc == 3) { auto s = open(argv[2], false); return s ? cmdScan(*s) : 1; }
    if (cmd == "pokedex" && argc == 3) return cmdPokedex(argv[2]);
    if (cmd == "trainer" && argc == 4) return cmdTrainer(argv[2], argv[3]);
    if (cmd == "kopiere" && argc == 7) return cmdKopiere(argv + 2);
    if (cmd == "insteam" && (argc == 5 || argc == 6)) return cmdInsTeam(argv + 2, argc == 6 ? argv[5] : nullptr);
    if (cmd == "geld" && argc == 4) return cmdGeld(argv[2], argv[3]);
    if (cmd == "signieren" && argc == 4) return cmdSignieren(argv[2], argv[3]);
    if (cmd == "speicher-neu" && argc >= 4) return cmdSpeicherNeu(argc, argv);
    if (cmd == "speicher-info" && argc == 3) {
        Bank bank;
        std::string err;
        if (!bank.load(argv[2], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        printBank(bank);
        return 0;
    }
    if (cmd == "reise" && argc == 5) return cmdReise(argv[2], argv[3], argv[4]);
    if (cmd == "item" && argc == 5) { // Test: Item in die passende reisende Tasche legen
        auto s = open(argv[2], true);
        if (!s) return 1;
        auto list = readTravelItems(*s);
        list.push_back({uint16_t(std::atoi(argv[3])), uint16_t(std::atoi(argv[4]))});
        std::vector<uint16_t> written;
        writeTravelItems(*s, list, written);
        if (!written.back()) { std::fprintf(stderr, "Item passt in diesem Spiel in keine reisende Tasche\n"); return 1; }
        if (!writeSave(argv[2], *s)) return 1;
        std::printf("Item %s × %s hinzugefügt\n", argv[3], argv[4]);
        return 0;
    }
    if (cmd == "speicher-items" && argc >= 4) { // Items weiterer Spielstände einmalig dazulegen
        Bank bank;
        std::string err;
        if (!bank.load(argv[2], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        for (int i = 3; i < argc; i++) {
            auto s = open(argv[i], true);
            if (!s) return 1;
            bank.addItemsFrom(*s);
        }
        if (!bank.save(argv[2], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        printBank(bank);
        return 0;
    }
    std::fputs(USAGE, stderr);
    return 1;
}
