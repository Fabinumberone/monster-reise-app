#include "games.hpp"

#include <cctype>

#include <dirent.h>

#include <cstdio>
#include <cstring>
#include <deque>

#include "gba.hpp"
#include "gbaconvert.hpp"
#include "nds.hpp"
#include "ndsconvert.hpp"
#include "ndscard.hpp"

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

namespace {
constexpr const char* CART_PREFIX = "dscard/"; // saveFile von DS-Modulen (kein echter Pfad)
std::string g_cardInfo;
} // namespace

const std::vector<Game>& fileGames() {
    static std::vector<Game> games;
    static std::deque<std::string> strings; // hält die Texte, auf die Game zeigt
    static bool scanned = false;
    if (scanned) return games;
    scanned = true;
    auto keep = [&](const std::string& s) { strings.push_back(s); return strings.back().c_str(); };
    auto add = [&](const std::string& path, const std::string& fn, const char* name, const char* region, int gen, u8 sub,
                   const char* kind, u8 ver) {
        for (auto& o : games)
            if (std::string(o.name) == name) { name = keep(fn.substr(0, fn.size() - 4)); break; }
        u32 h = 2166136261u;
        for (unsigned char c : path) h = (h ^ c) * 16777619u;
        Game game{keep(name), region, h | 0x80000000u, gen, GameKind::XY, keep(path), 5, ver};
        game.kindName = kind;
        game.sub = sub;
        games.push_back(game);
    };
    struct Dir { const char* path; bool nds; };
    const Dir dirs[] = {{"sdmc:/mGBA", false}, {"sdmc:/roms/nds favoriten/saves", true}, {"sdmc:/roms/nds/saves", true}};
    for (const Dir& dir : dirs) {
        DIR* d = opendir(dir.path);
        if (!d) continue;
        while (dirent* ent = readdir(d)) {
            std::string fn = ent->d_name;
            if (fn.size() < 5) continue;
            std::string ext = fn.substr(fn.size() - 4);
            for (auto& c : ext) c = char(std::tolower(c));
            if (ext != ".sav") continue;
            std::string path = std::string(dir.path) + "/" + fn;
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) continue;
            std::vector<u8> data(0x80100);
            size_t n = std::fread(data.data(), 1, data.size(), f);
            std::fclose(f);
            data.resize(n);
            std::string err;
            if (!dir.nds && n >= 0x20000 && n <= 0x20100) {
                pw3d::SaveGBA s;
                if (!s.load(data, err)) continue;
                u8 ver = pw3d::versionFromFileName(fn);
                if (ver < 1 || ver > 5) ver = pw3d::gen3OriginVersion(s);
                const char* name = ver == 1 ? "Saphir" : ver == 2 ? "Rubin" : ver == 3 ? "Smaragd" : ver == 4 ? "Feuerrot" : "Blattgrün";
                if (s.game() == pw3d::GBAGame::E) name = "Smaragd";
                const char* kind = s.game() == pw3d::GBAGame::RS ? "GBA-RS" : s.game() == pw3d::GBAGame::E ? "GBA-E" : "GBA-FRLG";
                add(path, fn, name, s.game() == pw3d::GBAGame::FRLG ? "Kanto" : "Hoenn", 3, u8(s.game()), kind, ver);
            } else if (dir.nds && n >= 0x80000 && n <= 0x80100) {
                pw3d::SaveNDS s;
                if (!s.load(data, err)) continue;
                u8 ver = pw3d::versionFromFileName(fn);
                if (ver < 7 || ver > 23) ver = pw3d::ndsOriginVersion(s);
                const char* name = "?";
                const char* region = "Sinnoh";
                const char* kind = "NDS-Pt";
                switch (s.game()) {
                case pw3d::NDSGame::DP: name = ver == 11 ? "Perl" : "Diamant"; kind = "NDS-DP"; break;
                case pw3d::NDSGame::Pt: name = "Platin"; break;
                case pw3d::NDSGame::HGSS: name = ver == 8 ? "SoulSilver" : "HeartGold"; region = "Johto"; kind = "NDS-HGSS"; break;
                case pw3d::NDSGame::BW: name = ver == 21 ? "Schwarz" : "Weiß"; region = "Einall"; kind = "NDS-BW"; break;
                case pw3d::NDSGame::B2W2: name = ver == 23 ? "Schwarz 2" : "Weiß 2"; region = "Einall"; kind = "NDS-B2W2"; break;
                }
                add(path, fn, name, region, s.generation(), u8(s.game()), kind, ver);
            }
        }
        closedir(d);
    }
    // DS-Modul im Schacht (Gen 4/5): Spielstand direkt vom Modul (ndscard)
    ndscard::Info card = ndscard::probe();
    g_cardInfo = card.present ? std::string("DS-Modul ") + card.gameCode : "DS-Modul: " + card.error;
    std::vector<u8> data;
    std::string err;
    if (card.present && ndscard::read(data, err)) {
        pw3d::SaveNDS s;
        if (s.load(data, err)) {
            std::string path = std::string(CART_PREFIX) + card.gameCode + ".sav";
            char lang = card.gameCode[3];
            const char* name = "?";
            const char* region = "Sinnoh";
            const char* kind = "NDS-Pt";
            u8 ver = 0;
            std::string c3(card.gameCode, 3);
            if (c3 == "ADA") { name = "Diamant"; kind = "NDS-DP"; ver = 10; }
            else if (c3 == "APA") { name = "Perl"; kind = "NDS-DP"; ver = 11; }
            else if (c3 == "CPU") { name = "Platin"; ver = 12; }
            else if (c3 == "IPK") { name = "HeartGold"; region = "Johto"; kind = "NDS-HGSS"; ver = 7; }
            else if (c3 == "IPG") { name = "SoulSilver"; region = "Johto"; kind = "NDS-HGSS"; ver = 8; }
            else if (c3 == "IRA") { name = "Weiß"; region = "Einall"; kind = "NDS-BW"; ver = 20; }
            else if (c3 == "IRB") { name = "Schwarz"; region = "Einall"; kind = "NDS-BW"; ver = 21; }
            else if (c3 == "IRD") { name = "Weiß 2"; region = "Einall"; kind = "NDS-B2W2"; ver = 22; }
            else if (c3 == "IRE") { name = "Schwarz 2"; region = "Einall"; kind = "NDS-B2W2"; ver = 23; }
            u32 h = 2166136261u;
            for (unsigned char c : path) h = (h ^ c) * 16777619u;
            Game game{keep(lang == 'D' ? std::string(name) : std::string(name) + " (" + lang + ")"), region, h | 0x80000000u,
                      s.generation(), GameKind::XY, keep(path), 5, ver};
            game.kindName = kind;
            game.sub = u8(s.game());
            games.push_back(game);
        } else {
            g_cardInfo += ": " + err;
        }
    } else if (card.present) {
        g_cardInfo += ": " + err;
    }
    return games;
}

bool isDsCart(const Game& g) { return isFileGame(g) && !std::strncmp(g.saveFile, CART_PREFIX, std::strlen(CART_PREFIX)); }
const std::string& cartInfo() { return g_cardInfo; }

std::string saveName(const Game& g) {
    std::string s = g.saveFile;
    size_t p = s.find_last_of('/');
    return p == std::string::npos ? s : s.substr(p + 1);
}

const char* bankKind(const Game& g) {
    if (isFileGame(g)) return g.kindName;
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
    for (auto& g : fileGames()) out.push_back({&g, isDsCart(g) ? MEDIATYPE_GAME_CARD : MEDIATYPE_SD});
    return out;
}

bool readSave(const SaveSlot& s, std::vector<u8>& out, std::string& err) {
    const Game& g = *s.game;
    if (isDsCart(g)) return ndscard::read(out, err);
    if (isFileGame(g) || (!s.cart() && emuExists(g))) {
        FILE* f = std::fopen(isFileGame(g) ? g.saveFile : emuPath(g).c_str(), "rb");
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
    if (isDsCart(g)) return ndscard::write(data, err);
    if (isFileGame(g) || (!s.cart() && emuExists(g))) {
        FILE* f = std::fopen(isFileGame(g) ? g.saveFile : emuPath(g).c_str(), "r+b"); // vorhandene Datei überschreiben
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

namespace {

// Ist der Titel installiert? (am:u; false, wenn der Dienst nicht verfügbar ist)
bool titleInstalled(u64 tid, FS_MediaType m) {
    if (R_FAILED(amInit())) return false;
    AM_TitleEntry info;
    bool ok = R_SUCCEEDED(AM_GetTitleInfo(m, 1, &tid, &info));
    amExit();
    return ok;
}

bool launchTitle(u64 tid, FS_MediaType media) {
    // Läuft das Home-Menü (echte Konsole), springt libctru beim Beenden über den Chainloader.
    // Ohne Home-Menü (Azahar startet die App direkt) nähme libctru einen Luma3DS-Umweg, der im
    // Emulator nicht existiert. Dann lösen wir den Sprung selbst aus; Azahar startet das Spiel
    // in diesem Fall per „RebootToTitle“.
    bool homeMenu = false;
    if (R_SUCCEEDED(APT_IsRegistered(aptGetMenuAppID(), &homeMenu)) && homeMenu) {
        aptSetChainloader(tid, media);
        return true;
    }
    static u8 param[0x300] = {};
    static u8 hmac[0x20] = {};
    if (R_FAILED(APT_PrepareToDoApplicationJump(0, tid, media))) return false;
    return R_SUCCEEDED(APT_DoApplicationJump(param, sizeof param, hmac));
}

constexpr u64 MGBA = 0x00040000001A1E00ULL;               // mGBA (CIA), Nutzer 2026-11-07
constexpr u64 TWILIGHT = 0x0004800453524C41ULL;           // TWiLight Menu++ (CIA „TWLMENUPP“, NAND), Nutzer 2026-11-07

// DS-Forwarder (nds-bootstrap) haben die Title-ID 00048004 + Spielcode in ASCII,
// z. B. HeartGold deutsch „IPKD“ = 49504B44 (vom Nutzer bestätigt: HG, Platin).
const char* dsGameCode(u8 version) {
    switch (version) {
    case 10: return "ADAD"; // Diamant
    case 11: return "APAD"; // Perl
    case 12: return "CPUD"; // Platin
    case 7: return "IPKD";  // HeartGold
    case 8: return "IPGD";  // SoulSilver
    case 20: return "IRAD"; // Weiß
    case 21: return "IRBD"; // Schwarz
    case 22: return "IRDD"; // Weiß 2
    case 23: return "IRED"; // Schwarz 2
    default: return nullptr;
    }
}

} // namespace

const char* launcherName(const SaveSlot& s) {
    if (isDsCart(*s.game)) return "das HOME-Menü (Modul)";
    if (s.game->gen == 3) return "mGBA";
    if (s.game->gen == 4 || s.game->gen == 5) return "TWiLight Menu++";
    return nullptr;
}

bool launchGame(const SaveSlot& s) {
    // DS-Module nicht aus der App starten: NS_RebootToTitle(Modul, 0) bringt den Systemdienst ns zum
    // Absturz (2026-11-08, New 2DS XL). Gestartet wird über das HOME-Menü.
    if (isDsCart(*s.game)) return false;
    if (s.game->gen == 3) return titleInstalled(MGBA, MEDIATYPE_SD) && launchTitle(MGBA, MEDIATYPE_SD);
    if (s.game->gen == 4 || s.game->gen == 5) {
        if (const char* c = dsGameCode(s.game->gbVersion)) {
            u64 tid = (0x00048004ULL << 32) | (u64(u8(c[0])) << 24) | (u64(u8(c[1])) << 16) | (u64(u8(c[2])) << 8) | u8(c[3]);
            if (titleInstalled(tid, MEDIATYPE_NAND)) return launchTitle(tid, MEDIATYPE_NAND);
        }
        if (titleInstalled(TWILIGHT, MEDIATYPE_NAND)) return launchTitle(TWILIGHT, MEDIATYPE_NAND);
        if (titleInstalled(TWILIGHT, MEDIATYPE_SD)) return launchTitle(TWILIGHT, MEDIATYPE_SD);
        return false;
    }
    return launchTitle((u64(HIGH_ID) << 32) | s.game->lowId, s.media);
}
