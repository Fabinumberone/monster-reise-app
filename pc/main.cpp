// pw3d-reise: PC-Werkzeug zum Prüfen und (später) Übertragen von Spielständen.
//   pw3d-reise info <main>   Spiel, Blöcke/Prüfsummen, Trainer, Team, Boxen
//   pw3d-reise scan <main>   sucht gültige Pokémon-Datensätze im ganzen Spielstand
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../core/save6.hpp"
#include "../core/memecrypto.hpp"
#include "../core/save7.hpp"

using namespace pw3d;

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

static void printPk(const char* label, const PK6& pk) {
    auto m = pk.moves();
    std::printf("  %-8s Nr. %3u Form %u  \"%s\"  OT \"%s\" %05u/%05u  EP %u  Attacken %u %u %u %u%s%s\n", label,
                pk.species(), pk.form(), pk.nickname().c_str(), pk.otName().c_str(), pk.tid(), pk.sid(), unsigned(pk.exp()),
                m[0], m[1], m[2], m[3], pk.isEgg() ? "  [Ei]" : "", pk.checksumValid() ? "" : "  [PRÜFSUMME FALSCH]");
    std::printf("           Spiel %u  Sprache %u  Geschlecht %u%s\n", pk.version(), pk.language(), pk.gender(),
                pk.isShiny() ? "  schillernd" : "");
}

static int cmdInfo(Save6& sav) {
    std::printf("Spiel: %s, %zu Byte\n", gameName(sav.game()), sav.raw().size());
    int bad = 0;
    for (auto& b : sav.blocks()) {
        if (!b.ok()) bad++;
        std::printf("  Block %04X  @0x%05X  Länge 0x%05X  CRC %04X/%04X %s\n", b.id, unsigned(b.offset), unsigned(b.length), b.storedCrc,
                    b.computedCrc, b.ok() ? "ok" : "FALSCH");
    }
    std::printf("%zu Blöcke, %d mit falscher Prüfsumme\n\n", sav.blocks().size(), bad);

    auto t = sav.trainer();
    std::printf("Trainer: \"%s\"  TID %05u  SID %05u  Geschlecht %u\n\n", t.name.c_str(), t.tid, t.sid, t.gender);

    int n = sav.partyCount();
    std::printf("Team (%d):\n", n);
    for (int i = 0; i < n && i < 6; i++) {
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "#%d", i + 1);
        PK6 pk = sav.partySlot(i);
        printPk(lbl, pk);
        std::printf("           Level %u\n", pk.partyLevel());
    }

    std::printf("\nBoxen:\n");
    int boxed = 0;
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < Save6::BOX_SLOTS; s++) {
            if (sav.boxSlotEmpty(b, s)) continue;
            boxed++;
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "B%02d/%02d", b + 1, s + 1);
            printPk(lbl, sav.boxSlot(b, s));
        }
    std::printf("  %d Pokémon in %d Boxen\n", boxed, sav.boxCount());
    return bad ? 2 : 0;
}

static int cmdInfo7(const Save7& sav) {
    std::printf("Spiel: %s, %zu Byte\n", gameName(sav.game()), sav.raw().size());
    int bad = 0;
    for (auto& b : sav.blocks()) {
        if (!b.ok()) bad++;
        std::printf("  Block %04X  @0x%05X  Länge 0x%05X  CRC %04X/%04X %s\n", b.id, unsigned(b.offset),
                    unsigned(b.length), b.storedCrc, b.computedCrc, b.ok() ? "ok" : "FALSCH");
    }
    std::printf("%zu Blöcke, %d mit falscher Prüfsumme\n", sav.blocks().size(), bad);
    bool sigOk = verifySave7(sav.raw());
    std::printf("Signatur: %s\n\n", sigOk ? "gültig" : "UNGÜLTIG");
    if (!sigOk) bad++;
    auto t = sav.trainer();
    std::printf("Trainer: \"%s\"  TID %05u  SID %05u  Geschlecht %u  Geld %u\n\n", t.name.c_str(), t.tid, t.sid,
                t.gender, unsigned(sav.money()));
    std::printf("Team (%d):\n", sav.partyCount());
    for (int i = 0; i < sav.partyCount() && i < 6; i++) {
        PK6 pk = sav.partySlot(i);
        printPk("", pk);
        std::printf("           Level %u\n", pk.partyLevel());
    }
    int boxed = 0;
    std::printf("\nBoxen:\n");
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < Save7::BOX_SLOTS; s++)
            if (!sav.boxSlotEmpty(b, s)) { boxed++; printPk("", sav.boxSlot(b, s)); }
    std::printf("  %d Pokémon in %d Boxen\n", boxed, sav.boxCount());
    return bad ? 2 : 0;
}

static int cmdScan(const Save6& sav) {
    const auto& d = sav.raw();
    std::printf("Suche gültige Datensätze (Prüfsumme + Art 1..%u):\n", GEN6_MAX_SPECIES);
    int found = 0;
    for (size_t p = 0; p + PK6_BOX_SIZE <= d.size(); p += 4) {
        if (isEmptySlot(&d[p], PK6_BOX_SIZE)) continue;
        PK6 pk = PK6::decrypt(&d[p], PK6_BOX_SIZE);
        if (!pk.looksValid()) continue;
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "@%05zX", p);
        printPk(lbl, pk);
        found++;
    }
    std::printf("%d gefunden\n", found);
    return 0;
}

static bool loadSave(const std::string& path, Save6& sav) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) {
        std::fprintf(stderr, "Kann %s nicht lesen\n", path.c_str());
        return false;
    }
    std::string err;
    if (!sav.load(std::move(data), err)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
        return false;
    }
    if (!sav.checksumsValid()) {
        std::fprintf(stderr, "%s: Prüfsummen stimmen schon vor der Änderung nicht, Abbruch\n", path.c_str());
        return false;
    }
    return true;
}

// Prüfsummen neu berechnen, Gegenprobe (neu laden, alle Prüfsummen ok) und schreiben.
static bool writeSave(const std::string& path, Save6& sav) {
    sav.updateChecksums();
    Save6 check;
    std::string err;
    if (!check.load(sav.raw(), err) || !check.checksumsValid()) {
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

// kopiere <quelle> <teamplatz 1-6> <ziel> <box 1-31> <platz 1-30>
// Kopiert ein Team-Pokémon unverändert in einen leeren Boxplatz des Ziels. Ziel wird überschrieben.
static int cmdKopiere(char** a) {
    Save6 src, dst;
    if (!loadSave(a[0], src) || !loadSave(a[2], dst)) return 1;
    int t = std::atoi(a[1]) - 1, b = std::atoi(a[3]) - 1, s = std::atoi(a[4]) - 1;
    if (t < 0 || t >= src.partyCount() || b < 0 || b >= dst.boxCount() || s < 0 || s >= Save6::BOX_SLOTS) {
        std::fprintf(stderr, "Ungültiger Platz\n");
        return 1;
    }
    if (!dst.boxSlotEmpty(b, s)) {
        std::fprintf(stderr, "Box %d Platz %d ist belegt, Abbruch\n", b + 1, s + 1);
        return 1;
    }
    PK6 pk = src.partySlot(t);
    if (!pk.looksValid()) {
        std::fprintf(stderr, "Quell-Pokémon ungültig, Abbruch\n");
        return 1;
    }
    dst.setBoxSlot(b, s, pk);
    dst.registerDex(pk);
    // Verglichen werden nur die 0xE8 Byte, die in der Box gespeichert werden.
    if (std::memcmp(dst.boxSlot(b, s).d.data(), pk.d.data(), PK6_BOX_SIZE) != 0) {
        std::fprintf(stderr, "Gegenprobe fehlgeschlagen, nichts geschrieben\n");
        return 1;
    }
    if (!writeSave(a[2], dst)) return 1;
    std::printf("Kopiert nach Box %d Platz %d, im Pokédex eingetragen:\n", b + 1, s + 1);
    printPk("", pk);
    return 0;
}

// pokedex <main>: trägt alle Pokémon aus Team und Boxen im Pokédex ein.
static int cmdPokedex(const char* path) {
    Save6 sav;
    if (!loadSave(path, sav)) return 1;
    int n = 0;
    for (int i = 0; i < sav.partyCount() && i < 6; i++) { sav.registerDex(sav.partySlot(i)); n++; }
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < Save6::BOX_SLOTS; s++)
            if (!sav.boxSlotEmpty(b, s)) { sav.registerDex(sav.boxSlot(b, s)); n++; }
    if (!writeSave(path, sav)) return 1;
    std::printf("%d Pokémon im Pokédex eingetragen\n", n);
    return 0;
}

// trainer <quelle> <ziel>: Ziel übernimmt Trainer-ID, Geheim-ID, Name und Geschlecht der Quelle.
static int cmdTrainer(const char* srcPath, const char* dstPath) {
    Save6 src, dst;
    if (!loadSave(srcPath, src) || !loadSave(dstPath, dst)) return 1;
    Trainer6 t = src.trainer(), old = dst.trainer();
    int n = dst.adoptTrainer(t);
    if (!writeSave(dstPath, dst)) return 1;
    std::printf("Trainer \"%s\" %05u/%05u -> \"%s\" %05u/%05u, %d eigene Pokémon umgeschrieben\n", old.name.c_str(),
                old.tid, old.sid, t.name.c_str(), t.tid, t.sid, n);
    return 0;
}

int runSelftest();

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "selbsttest") return runSelftest();
    if (argc == 7 && std::string(argv[1]) == "kopiere") return cmdKopiere(argv + 2);
    if (argc == 3 && std::string(argv[1]) == "pokedex") return cmdPokedex(argv[2]);
    if (argc == 4 && std::string(argv[1]) == "trainer") return cmdTrainer(argv[2], argv[3]);
    if (argc < 3) {
        std::fprintf(stderr, "Aufruf: pw3d-reise selbsttest | info <main> | scan <main> | pokedex <main>\n"
                             "        pw3d-reise kopiere <quelle> <teamplatz> <ziel> <box> <platz>\n"
                             "        pw3d-reise trainer <quelle> <ziel>\n");
        return 1;
    }
    std::string cmd = argv[1];
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Kann %s nicht lesen\n", argv[2]);
        return 1;
    }
    if (data.size() != 0x65600 && data.size() != 0x76000) {
        Save7 s7;
        std::string err7;
        if (!s7.load(std::move(data), err7)) {
            std::fprintf(stderr, "Fehler: %s\n", err7.c_str());
            return 1;
        }
        if (cmd == "info") return cmdInfo7(s7);
        if (cmd == "signieren" && argc == 4) { // Test: neu signieren und unter anderem Namen speichern
            signSave7(s7.raw());
            std::ofstream f(argv[3], std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(s7.raw().data()), std::streamsize(s7.raw().size()));
            std::printf("Signiert nach %s, Signatur %s\n", argv[3], verifySave7(s7.raw()) ? "gültig" : "UNGÜLTIG");
            return f ? 0 : 1;
        }
        if (cmd == "geld" && argc == 4) {
            if (!s7.checksumsValid()) { std::fprintf(stderr, "Prüfsummen stimmen schon vorher nicht, Abbruch\n"); return 1; }
            uint32_t old = s7.money();
            s7.setMoney(uint32_t(std::strtoul(argv[3], nullptr, 10)));
            s7.updateChecksums();
            signSave7(s7.raw()); // nach den Prüfsummen, denn die Signatur deckt die Blocktabelle ab
            Save7 check;
            if (!check.load(s7.raw(), err7) || !check.checksumsValid() || !verifySave7(check.raw())) {
                std::fprintf(stderr, "Gegenprobe fehlgeschlagen\n");
                return 1;
            }
            std::ofstream f(argv[2], std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(s7.raw().data()), std::streamsize(s7.raw().size()));
            if (!f) { std::fprintf(stderr, "Schreiben fehlgeschlagen\n"); return 1; }
            std::printf("Geld %u -> %u\n", unsigned(old), unsigned(s7.money()));
            return 0;
        }
        std::fprintf(stderr, "Befehl für Gen 7 nicht verfügbar: %s\n", cmd.c_str());
        return 1;
    }
    Save6 sav;
    std::string err;
    if (!sav.load(std::move(data), err)) {
        std::fprintf(stderr, "Fehler: %s\n", err.c_str());
        return 1;
    }
    if (cmd == "info") return cmdInfo(sav);
    if (cmd == "scan") return cmdScan(sav);
    std::fprintf(stderr, "Unbekannter Befehl: %s\n", cmd.c_str());
    return 1;
}
