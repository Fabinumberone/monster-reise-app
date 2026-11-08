// DS-Modul-Spielstände über SPI. Der SPI-Teil ist eine gekürzte Fassung von Checkpoint
// 3ds/source/spi.cpp (GPLv3, Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew; darin
// TWLSaveTool, Copyright (C) 2015-2016 TuxSH, GPLv2+, und savegame-manager, Copyright (C) Pokedoc 2010,
// GPLv2+). Geändert: nur Flash-Chips (kein EEPROM, keine 8-MB-Module), Seitenvergleich vor dem
// Schreiben, Kontrolllesen danach.
#include "ndscard.hpp"

#include <cstring>

namespace ndscard {
namespace {

enum CardType {
    NO_CHIP = -1,
    FLASH_256KB_1 = 4,
    FLASH_256KB_2 = 5,
    FLASH_512KB_1 = 6,
    FLASH_512KB_2 = 7,
    FLASH_1MB = 8,
    FLASH_INFRARED_DUMMY = 9,
    FLASH_512KB_INFRARED = 10,
    FLASH_256KB_INFRARED = 11,
    FLASH_STD_DUMMY = 4,
};

constexpr u8 CMD_READ = 3, CMD_RDSR = 5, CMD_WREN = 6, CMD_PW = 10, CMD_RDID = 0x9f;
constexpr u8 FLG_WIP = 1, FLG_WEL = 2;
constexpr u32 SIZE = 0x80000, PAGE = 256;

CardType g_type = NO_CHIP;
bool g_pxi = false;

Result spi(CardType type, void* cmd, u32 cmdSize, void* answer, u32 answerSize, void* data, u32 dataSize) {
    u8 op = pxiDevMakeTransferOption(BAUDRATE_4MHZ, BUSMODE_1BIT), op2 = pxiDevMakeTransferOption(BAUDRATE_1MHZ, BUSMODE_1BIT);
    u64 wait = pxiDevMakeWaitOperation(WAIT_NONE, DEASSERT_NONE, 0LL);
    u64 hf = 0;
    bool ir = type == FLASH_512KB_INFRARED || type == FLASH_256KB_INFRARED;
    PXIDEV_SPIBuffer header = {&hf, ir ? 1U : 0U, ir ? op2 : op, wait};
    PXIDEV_SPIBuffer c = {cmd, cmdSize, op, wait};
    PXIDEV_SPIBuffer a = {answer, answerSize, op, wait};
    PXIDEV_SPIBuffer d = {data, dataSize, op, wait};
    PXIDEV_SPIBuffer n = {nullptr, 0U, op, wait};
    PXIDEV_SPIBuffer footer = {&hf, 0U, op, wait};
    return PXIDEV_SPIMultiWriteRead(&header, &c, &a, &d, &n, &footer);
}

Result waitWriteEnd(CardType type) {
    u8 cmd = CMD_RDSR, sr = 0;
    for (int i = 0; i < 1000; i++) {
        Result r = spi(type, &cmd, 1, &sr, 1, nullptr, 0);
        if (r) return r;
        if (!(sr & FLG_WIP)) return 0;
    }
    return 1;
}

Result enableWriting(CardType type) {
    u8 cmd = CMD_WREN, sr = 0;
    Result r = spi(type, &cmd, 1, nullptr, 0, nullptr, 0);
    if (r) return r;
    cmd = CMD_RDSR;
    for (int i = 0; i < 1000; i++) {
        if ((r = spi(type, &cmd, 1, &sr, 1, nullptr, 0))) return r;
        if (!(sr & ~FLG_WEL)) return 0;
    }
    return 1;
}

Result jedecAndStatus(CardType type, u32& id, u8& sr) {
    u8 cmd = CMD_RDID, buf[3] = {};
    Result r = waitWriteEnd(type);
    if (r) return r;
    if ((r = spi(type, &cmd, 1, buf, 3, nullptr, 0))) return r;
    id = (u32(buf[0]) << 16) | (u32(buf[1]) << 8) | buf[2];
    cmd = CMD_RDSR;
    return spi(type, &cmd, 1, &sr, 1, nullptr, 0);
}

// Nach Checkpoint SPIGetCardType, nur der Flash-Zweig.
Result cardType(CardType& type, bool infrared) {
    static const u32 ordered[] = {0x204012, 0x621600, 0x204013, 0x621100, 0x204014, 0x202017};
    CardType t = infrared ? FLASH_INFRARED_DUMMY : FLASH_STD_DUMMY;
    u32 jedec = 0;
    u8 sr = 0;
    Result r = jedecAndStatus(t, jedec, sr);
    if (r) return r;
    if (jedec == 0x204017 || jedec == 0x202017) { type = NO_CHIP; return 0; } // 8-MB-Modul (kein Pokémon)
    if (infrared) { // wie Checkpoint: Infrarot-Module immer als Infrarot-Flash, Größe nach JEDEC
        type = (jedec == ordered[0] || jedec == ordered[1]) ? FLASH_256KB_INFRARED : FLASH_512KB_INFRARED;
        return 0;
    }
    bool flash = (sr & 0xfd) == 0x00 && jedec != 0x00ffffff;
    if (!flash) { type = NO_CHIP; return 0; } // EEPROM oder kein Chip
    if (jedec == 0x208013) { type = FLASH_512KB_1; return 0; }
    for (int i = 0; i < 6; i++)
        if (jedec == ordered[i]) { type = CardType(FLASH_256KB_1 + i); return 0; }
    type = NO_CHIP;
    return 0;
}

Result readData(CardType type, u32 pos, void* data, u32 size) {
    Result r = waitWriteEnd(type);
    if (r) return r;
    u8 cmd[4] = {CMD_READ, u8(pos >> 16), u8(pos >> 8), u8(pos)};
    return spi(type, cmd, 4, data, size, nullptr, 0);
}

// Eine Seite (256 Byte, an der Seitengrenze) mit „Page Write“ (löscht und schreibt).
Result writePage(CardType type, u32 pos, const void* data) {
    u8 cmd[4] = {CMD_PW, u8(pos >> 16), u8(pos >> 8), u8(pos)};
    Result r;
    if ((r = waitWriteEnd(type))) return r;
    if ((r = enableWriting(type))) return r;
    if ((r = spi(type, cmd, 4, nullptr, 0, const_cast<void*>(data), PAGE))) return r;
    return waitWriteEnd(type);
}

bool pokemonCode(const char* c) {
    static const char* prefixes[] = {"ADA", "APA", "CPU", "IPK", "IPG", "IRA", "IRB", "IRD", "IRE"};
    for (const char* p : prefixes)
        if (!std::strncmp(c, p, 3)) return true;
    return false;
}

std::string hex(Result r) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08lX", static_cast<unsigned long>(r));
    return b;
}

} // namespace

Info probe() {
    Info info;
    bool inserted = false;
    if (R_FAILED(FSUSER_CardSlotIsInserted(&inserted)) || !inserted) { info.error = "kein Modul"; return info; }
    FS_CardType ct;
    if (R_FAILED(FSUSER_GetCardType(&ct)) || ct != CARD_TWL) { info.error = "kein DS-Modul"; return info; }
    static u8 header[0x3B4];
    Result r = FSUSER_GetLegacyRomHeader(MEDIATYPE_GAME_CARD, 0LL, header);
    if (R_FAILED(r)) { info.error = "Modulkopf nicht lesbar " + hex(r); return info; }
    std::memcpy(info.gameCode, header + 12, 4);
    if (!pokemonCode(info.gameCode)) { info.error = std::string("kein Pokémon-Spiel (") + info.gameCode + ")"; return info; }
    if (!g_pxi) {
        if (R_FAILED(r = pxiDevInit())) { info.error = "pxi:dev nicht verfügbar " + hex(r); return info; }
        g_pxi = true;
    }
    CardType t = NO_CHIP;
    if (R_FAILED(r = cardType(t, info.gameCode[0] == 'I'))) { info.error = "Speicherchip nicht erkannt " + hex(r); return info; }
    if (t != FLASH_512KB_1 && t != FLASH_512KB_2 && t != FLASH_512KB_INFRARED) {
        info.error = "unerwarteter Speicherchip (Typ " + std::to_string(int(t)) + ")";
        return info;
    }
    g_type = t;
    info.present = true;
    return info;
}

bool read(std::vector<u8>& out, std::string& err) {
    if (g_type == NO_CHIP) { err = "DS-Modul nicht erkannt"; return false; }
    out.assign(SIZE, 0);
    for (u32 p = 0; p < SIZE; p += 0x10000) { // in Blöcken, damit der Bus nicht zu lange belegt ist
        Result r = readData(g_type, p, out.data() + p, 0x10000);
        if (r) { err = "Modul nicht lesbar " + hex(r); return false; }
    }
    return true;
}

bool write(const std::vector<u8>& data, std::string& err) {
    if (g_type == NO_CHIP) { err = "DS-Modul nicht erkannt"; return false; }
    if (data.size() < SIZE) { err = "Spielstand zu klein"; return false; }
    std::vector<u8> cur;
    if (!read(cur, err)) return false;
    for (u32 p = 0; p < SIZE; p += PAGE) {
        if (!std::memcmp(cur.data() + p, data.data() + p, PAGE)) continue;
        Result r = writePage(g_type, p, data.data() + p);
        if (r) { err = "Schreiben aufs Modul fehlgeschlagen " + hex(r); return false; }
    }
    std::vector<u8> check;
    if (!read(check, err)) return false;
    if (std::memcmp(check.data(), data.data(), SIZE)) { err = "Kontrolle nach dem Schreiben fehlgeschlagen"; return false; }
    return true;
}

bool launch() {
    // Mit Titel-ID 0 und Medium „Modul“ startet das System das eingelegte Modul (3dbrew NSS:RebootSystem).
    if (R_FAILED(nsInit())) return false;
    Result r = NS_RebootToTitle(MEDIATYPE_GAME_CARD, 0, 0);
    nsExit();
    return R_SUCCEEDED(r);
}

} // namespace ndscard
