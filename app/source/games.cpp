#include "games.hpp"

#include <cstdio>

using pw3d::GameKind;

const std::vector<Game>& allGames() {
    static const std::vector<Game> games = {
        {"Omega Rubin", "Hoenn", 0x0011C400, GameKind::ORAS}, {"Alpha Saphir", "Hoenn", 0x0011C500, GameKind::ORAS},
        {"X", "Kalos", 0x00055D00, GameKind::XY},              {"Y", "Kalos", 0x00055E00, GameKind::XY},
        {"Sonne", "Alola", 0x00164800, GameKind::SM},          {"Mond", "Alola", 0x00175E00, GameKind::SM},
        {"Ultrasonne", "Alola", 0x001B5000, GameKind::USUM},   {"Ultramond", "Alola", 0x001B5100, GameKind::USUM},
    };
    return games;
}

const Game* gameByLowId(u32 lowId) {
    for (auto& g : allGames())
        if (g.lowId == lowId) return &g;
    return nullptr;
}

namespace {

constexpr u32 HIGH_ID = 0x00040000;

std::string emuPath(const Game& g) {
    char p[160];
    std::snprintf(p, sizeof p,
                  "sdmc:/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/"
                  "title/%08lx/%08lx/data/00000001/main",
                  (unsigned long)HIGH_ID, (unsigned long)g.lowId);
    return p;
}

bool emuExists(const Game& g) {
    FILE* f = std::fopen(emuPath(g).c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Spielstand-Archiv auf der Konsole öffnen (SD, dann Modul).
bool openArchive(const Game& g, FS_Archive& arch, FS_MediaType& usedMedia) {
    for (FS_MediaType m : {MEDIATYPE_SD, MEDIATYPE_GAME_CARD}) {
        u32 path[3] = {u32(m), g.lowId, HIGH_ID};
        FS_Path fp = {PATH_BINARY, sizeof path, path};
        if (R_SUCCEEDED(FSUSER_OpenArchive(&arch, ARCHIVE_USER_SAVEDATA, fp))) {
            usedMedia = m;
            return true;
        }
    }
    return false;
}

} // namespace

bool hasSave(const Game& g) {
    if (emuExists(g)) return true;
    FS_Archive a;
    FS_MediaType m;
    if (!openArchive(g, a, m)) return false;
    Handle h;
    bool ok = R_SUCCEEDED(FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, "/main"), FS_OPEN_READ, 0));
    if (ok) FSFILE_Close(h);
    FSUSER_CloseArchive(a);
    return ok;
}

bool readSave(const Game& g, std::vector<u8>& out, std::string& err) {
    if (emuExists(g)) {
        FILE* f = std::fopen(emuPath(g).c_str(), "rb");
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
    FS_MediaType m;
    if (!openArchive(g, a, m)) { err = "Spielstand-Archiv nicht zugänglich"; return false; }
    Handle h;
    Result r = FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, "/main"), FS_OPEN_READ, 0);
    if (R_FAILED(r)) { FSUSER_CloseArchive(a); err = "Datei main fehlt"; return false; }
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

bool writeSave(const Game& g, const std::vector<u8>& data, std::string& err) {
    if (emuExists(g)) {
        FILE* f = std::fopen(emuPath(g).c_str(), "r+b"); // vorhandene Datei überschreiben
        if (!f) { err = "Kann Spielstand nicht öffnen"; return false; }
        bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        ok = (std::fclose(f) == 0) && ok;
        if (!ok) err = "Schreibfehler";
        return ok;
    }
    FS_Archive a;
    FS_MediaType m;
    if (!openArchive(g, a, m)) { err = "Spielstand-Archiv nicht zugänglich"; return false; }
    Handle h;
    Result r = FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, "/main"), FS_OPEN_WRITE, 0);
    if (R_FAILED(r)) { FSUSER_CloseArchive(a); err = "Kann Spielstand nicht öffnen"; return false; }
    u32 written = 0;
    r = FSFILE_Write(h, &written, 0, data.data(), u32(data.size()), FS_WRITE_FLUSH);
    FSFILE_Close(h);
    if (R_SUCCEEDED(r)) r = FSUSER_ControlArchive(a, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
    FSUSER_CloseArchive(a);
    if (R_FAILED(r) || written != data.size()) { err = "Schreibfehler"; return false; }
    return true;
}

bool launchGame(const Game& g) {
    u64 tid = (u64(HIGH_ID) << 32) | g.lowId;
    // Läuft das Home-Menü (echte Konsole), springt libctru beim Beenden über den Chainloader.
    // Ohne Home-Menü (Azahar startet die App direkt) nähme libctru einen Luma3DS-Umweg, der im
    // Emulator nicht existiert. Dann lösen wir den Sprung selbst aus; Azahar startet das Spiel
    // in diesem Fall per „RebootToTitle“.
    bool homeMenu = false;
    if (R_SUCCEEDED(APT_IsRegistered(aptGetMenuAppID(), &homeMenu)) && homeMenu) {
        aptSetChainloader(tid, MEDIATYPE_SD);
        return true;
    }
    static u8 param[0x300] = {};
    static u8 hmac[0x20] = {};
    if (R_FAILED(APT_PrepareToDoApplicationJump(0, tid, MEDIATYPE_SD))) return false;
    return R_SUCCEEDED(APT_DoApplicationJump(param, sizeof param, hmac));
}
