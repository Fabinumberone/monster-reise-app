#include "games.hpp"

#include <cctype>

#include <dirent.h>

#include <cstdio>
#include <deque>

#include "gba.hpp"
#include "gbaconvert.hpp"

using pw3d::GameKind;

const std::vector<Game>& allGames() {
    // Gen 1/2: Virtual-Console-Titel (EU), Spiel und Sprache am ROM-Namen im RomFS bestätigt.
    static const std::vector<Game> games = {
        {"Blau (EN)", "Kanto", 0x00171100, 1, GameKind::XY, "sav.dat", 2, 37},
        {"Gelb (EN)", "Kanto", 0x00171200, 1, GameKind::XY, "sav.dat", 2, 38},
        {"Gelb", "Kanto", 0x00171500, 1, GameKind::XY, "sav.dat", 5, 38},
        {"Kristall (EN)", "Johto", 0x00172800, 2, GameKind::XY, "sav.dat", 2, 41},
        {"Silber", "Johto", 0x00172A00, 2, GameKind::XY, "sav.dat", 5, 40},
        {"Kristall", "Johto", 0x00172B00, 2, GameKind::XY, "sav.dat", 5, 41},
        {"Omega Rubin", "Hoenn", 0x0011C400, 6, GameKind::ORAS, "main"},
        {"Alpha Saphir", "Hoenn", 0x0011C500, 6, GameKind::ORAS, "main"},
        {"X", "Kalos", 0x00055D00, 6, GameKind::XY, "main"},
        {"Y", "Kalos", 0x00055E00, 6, GameKind::XY, "main"},
        {"Sonne", "Alola", 0x00164800, 7, GameKind::SM, "main"},
        {"Mond", "Alola", 0x00175E00, 7, GameKind::SM, "main"},
        {"Ultrasonne", "Alola", 0x001B5000, 7, GameKind::USUM, "main"},
        {"Ultramond", "Alola", 0x001B5100, 7, GameKind::USUM, "main"},
    };
    return games;
}

const std::vector<Game>& fileGames() {
    static std::vector<Game> games;
    static std::deque<std::string> strings; // hält die Texte, auf die Game zeigt
    static bool scanned = false;
    if (scanned) return games;
    scanned = true;
    const char* dirs[] = {"sdmc:/mGBA"};
    for (const char* dir : dirs) {
        DIR* d = opendir(dir);
        if (!d) continue;
        while (dirent* ent = readdir(d)) {
            std::string fn = ent->d_name;
            if (fn.size() < 5) continue;
            std::string ext = fn.substr(fn.size() - 4);
            for (auto& c : ext) c = char(std::tolower(c));
            if (ext != ".sav") continue;
            std::string path = std::string(dir) + "/" + fn;
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) continue;
            std::vector<u8> data(0x20100);
            size_t n = std::fread(data.data(), 1, data.size(), f);
            std::fclose(f);
            if (n < 0x20000 || n > 0x20100) continue;
            data.resize(n);
            pw3d::SaveGBA s;
            std::string err;
            if (!s.load(data, err)) continue;
            u8 ver = pw3d::gen3OriginVersion(s);
            const char* name = ver == 1 ? "Saphir" : ver == 2 ? "Rubin" : ver == 3 ? "Smaragd" : ver == 4 ? "Feuerrot" : "Blattgrün";
            if (s.game() == pw3d::GBAGame::E) name = "Smaragd";
            for (auto& g : games)
                if (std::string(g.name) == name) { strings.push_back(fn.substr(0, fn.size() - 4)); name = strings.back().c_str(); break; }
            u32 h = 2166136261u;
            for (unsigned char c : fn) h = (h ^ c) * 16777619u;
            strings.push_back(name);
            const char* nm = strings.back().c_str();
            strings.push_back(path);
            const char* p = strings.back().c_str();
            const char* kind = s.game() == pw3d::GBAGame::RS ? "GBA-RS" : s.game() == pw3d::GBAGame::E ? "GBA-E" : "GBA-FRLG";
            Game g{nm, s.game() == pw3d::GBAGame::FRLG ? "Kanto" : "Hoenn", h | 0x80000000u, 3, GameKind::XY, p, 5, ver};
            g.kindName = kind;
            games.push_back(g);
        }
        closedir(d);
    }
    return games;
}

std::string saveName(const Game& g) {
    std::string s = g.saveFile;
    size_t p = s.find_last_of('/');
    return p == std::string::npos ? s : s.substr(p + 1);
}

const char* bankKind(const Game& g) {
    if (g.gen == 3) return g.kindName;
    if (g.gen == 1) return "RBY";
    if (g.gen == 2) return "GSC";
    return pw3d::kindName(g.kind);
}

const Game* gameByLowId(u32 lowId) {
    for (auto& g : allGames())
        if (g.lowId == lowId) return &g;
    for (auto& g : fileGames())
        if (g.lowId == lowId) return &g;
    return nullptr;
}

namespace {

constexpr u32 HIGH_ID = 0x00040000;

std::string emuPath(const Game& g) {
    char p[160];
    std::snprintf(p, sizeof p,
                  "sdmc:/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/"
                  "title/%08lx/%08lx/data/00000001/%s",
                  (unsigned long)HIGH_ID, (unsigned long)g.lowId, g.saveFile);
    return p;
}

bool emuExists(const Game& g) {
    FILE* f = std::fopen(emuPath(g).c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Spielstand-Archiv auf der Konsole öffnen.
bool openArchive(const Game& g, FS_MediaType m, FS_Archive& arch) {
    u32 path[3] = {u32(m), g.lowId, HIGH_ID};
    FS_Path fp = {PATH_BINARY, sizeof path, path};
    return R_SUCCEEDED(FSUSER_OpenArchive(&arch, ARCHIVE_USER_SAVEDATA, fp));
}

bool archiveHasFile(const Game& g, FS_MediaType m) {
    FS_Archive a;
    if (!openArchive(g, m, a)) return false;
    Handle h;
    bool ok = R_SUCCEEDED(FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, (std::string("/") + g.saveFile).c_str()), FS_OPEN_READ, 0));
    if (ok) FSFILE_Close(h);
    FSUSER_CloseArchive(a);
    return ok;
}

} // namespace

std::vector<SaveSlot> findSaves() {
    std::vector<SaveSlot> out;
    for (auto& g : allGames()) {
        if (emuExists(g)) { out.push_back({&g, MEDIATYPE_SD}); continue; } // Azahar: nur die Emulator-SD
        if (archiveHasFile(g, MEDIATYPE_SD)) out.push_back({&g, MEDIATYPE_SD});
        if (g.gen >= 6 && archiveHasFile(g, MEDIATYPE_GAME_CARD)) out.push_back({&g, MEDIATYPE_GAME_CARD});
    }
    for (auto& g : fileGames()) out.push_back({&g, MEDIATYPE_SD});
    return out;
}

bool readSave(const SaveSlot& s, std::vector<u8>& out, std::string& err) {
    const Game& g = *s.game;
    if (g.gen == 3 || (!s.cart() && emuExists(g))) {
        FILE* f = std::fopen(g.gen == 3 ? g.saveFile : emuPath(g).c_str(), "rb");
        if (!f) { err = s.label() + ": Spielstand nicht lesbar"; return false; }
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.resize(size_t(n));
        bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        std::fclose(f);
        if (!ok) err = "Lesefehler";
        return ok;
    }
    FS_Archive a;
    if (!openArchive(g, s.media, a)) { err = s.label() + ": Spielstand-Archiv nicht zugänglich"; return false; }
    Handle h;
    Result r = FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, (std::string("/") + g.saveFile).c_str()), FS_OPEN_READ, 0);
    if (R_FAILED(r)) { FSUSER_CloseArchive(a); err = s.label() + ": Spielstand fehlt"; return false; }
    u64 size = 0;
    FSFILE_GetSize(h, &size);
    out.resize(size_t(size));
    u32 read = 0;
    r = FSFILE_Read(h, &read, 0, out.data(), u32(size));
    FSFILE_Close(h);
    FSUSER_CloseArchive(a);
    if (R_FAILED(r) || read != size) { err = "Lesefehler"; return false; }
    return true;
}

bool writeSave(const SaveSlot& s, const std::vector<u8>& data, std::string& err) {
    const Game& g = *s.game;
    if (g.gen == 3 || (!s.cart() && emuExists(g))) {
        FILE* f = std::fopen(g.gen == 3 ? g.saveFile : emuPath(g).c_str(), "r+b"); // vorhandene Datei überschreiben
        if (!f) { err = "Kann Spielstand nicht öffnen"; return false; }
        bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        ok = (std::fclose(f) == 0) && ok;
        if (!ok) err = "Schreibfehler";
        return ok;
    }
    FS_Archive a;
    if (!openArchive(g, s.media, a)) { err = "Spielstand-Archiv nicht zugänglich"; return false; }
    Handle h;
    Result r = FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, (std::string("/") + g.saveFile).c_str()), FS_OPEN_WRITE, 0);
    if (R_FAILED(r)) { FSUSER_CloseArchive(a); err = "Kann Spielstand nicht öffnen"; return false; }
    u32 written = 0;
    r = FSFILE_Write(h, &written, 0, data.data(), u32(data.size()), FS_WRITE_FLUSH);
    FSFILE_Close(h);
    if (R_SUCCEEDED(r)) r = FSUSER_ControlArchive(a, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
    FSUSER_CloseArchive(a);
    if (R_FAILED(r) || written != data.size()) { err = "Schreibfehler"; return false; }
    return true;
}

bool launchGame(const SaveSlot& s) {
    if (s.game->gen == 3) return false; // mGBA startet man selbst
    u64 tid = (u64(HIGH_ID) << 32) | s.game->lowId;
    // Läuft das Home-Menü (echte Konsole), springt libctru beim Beenden über den Chainloader.
    // Ohne Home-Menü (Azahar startet die App direkt) nähme libctru einen Luma3DS-Umweg, der im
    // Emulator nicht existiert. Dann lösen wir den Sprung selbst aus; Azahar startet das Spiel
    // in diesem Fall per „RebootToTitle“.
    bool homeMenu = false;
    if (R_SUCCEEDED(APT_IsRegistered(aptGetMenuAppID(), &homeMenu)) && homeMenu) {
        aptSetChainloader(tid, s.media);
        return true;
    }
    static u8 param[0x300] = {};
    static u8 hmac[0x20] = {};
    if (R_FAILED(APT_PrepareToDoApplicationJump(0, tid, s.media))) return false;
    return R_SUCCEEDED(APT_DoApplicationJump(param, sizeof param, hmac));
}
