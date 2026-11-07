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
    dst.updateChecksums();

    // Gegenprobe vor dem Schreiben: neu laden und vergleichen.
    Save6 check;
    std::string err;
    // Verglichen werden nur die 0xE8 Byte, die in der Box gespeichert werden.
    if (!check.load(dst.raw(), err) || !check.checksumsValid() ||
        std::memcmp(check.boxSlot(b, s).d.data(), pk.d.data(), PK6_BOX_SIZE) != 0) {
        std::fprintf(stderr, "Gegenprobe fehlgeschlagen, nichts geschrieben\n");
        return 1;
    }
    std::ofstream f(a[2], std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(dst.raw().data()), std::streamsize(dst.raw().size()));
    if (!f) {
        std::fprintf(stderr, "Schreiben fehlgeschlagen\n");
        return 1;
    }
    std::printf("Kopiert nach Box %d Platz %d:\n", b + 1, s + 1);
    printPk("", pk);
    return 0;
}

int runSelftest();

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "selbsttest") return runSelftest();
    if (argc == 7 && std::string(argv[1]) == "kopiere") return cmdKopiere(argv + 2);
    if (argc < 3) {
        std::fprintf(stderr, "Aufruf: pw3d-reise selbsttest | info <main> | scan <main>\n"
                             "        pw3d-reise kopiere <quelle> <teamplatz> <ziel> <box> <platz>\n");
        return 1;
    }
    std::string cmd = argv[1];
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Kann %s nicht lesen\n", argv[2]);
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
