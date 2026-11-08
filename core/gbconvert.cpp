#include "gbconvert.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

#include "bytes.hpp"

namespace pw3d {

namespace {

// Gen-7-Felder (siehe pk6.hpp und PKHeX PK7)
constexpr int O_SPECIES = 0x08, O_ITEM = 0x0A, O_TID = 0x0C, O_SID = 0x0E, O_EXP = 0x10, O_ABILITY = 0x14,
              O_ABILITYNUM = 0x15, O_PID = 0x18, O_NATURE = 0x1C, O_FLAGS = 0x1D, O_EV = 0x1E, O_NICK = 0x40,
              O_MOVES = 0x5A, O_PP = 0x62, O_PPUP = 0x66, O_IV = 0x74, O_OT = 0xB0, O_OTFRIEND = 0xCA,
              O_METDATE = 0xD4, O_METLOC = 0xDA, O_BALL = 0xDC, O_METLEVEL = 0xDD, O_VERSION = 0xDF,
              O_LANG = 0xE3, O_LEVEL = 0xEC, O_HP = 0xF0, O_STATS = 0xF2;

void wbe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void wbe24(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 16); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v); }

std::string upper(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c >= 'a' && c <= 'z') { o += char(c - 32); continue; }
        if (c == 0xC3 && i + 1 < s.size()) { // ä ö ü é -> Ä Ö Ü É
            unsigned char d = s[i + 1];
            if (d == 0xA4 || d == 0xB6 || d == 0xBC || d == 0xA9) { o += char(c); o += char(d - 0x20); i++; continue; }
        }
        o += char(c);
    }
    return o;
}

std::string defaultName(uint16_t species, uint8_t language, const Names& n) {
    return language == 2 ? n.speciesEn(species) : n.species(species);
}

uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

bool hiddenDisallowed(uint16_t s, int gen) {
    if (s == 92 || s == 93 || s == 94 || s == 109 || s == 110 || s == 151) return true;
    return gen == 2 && (s == 200 || s == 201 || s == 251);
}

uint8_t hpDV(uint16_t dvs) {
    return uint8_t(((dvs >> 12 & 1) << 3) | ((dvs >> 8 & 1) << 2) | ((dvs >> 4 & 1) << 1) | (dvs & 1));
}

uint8_t genderFromDV(uint8_t atkDV, uint8_t ratio) {
    if (ratio == 255) return 2;
    if (ratio == 254) return 1;
    if (ratio == 0) return 0;
    return atkDV > (ratio >> 4) ? 0 : 1;
}

bool isOwn(const PK6& pk, const Trainer6& t) { return pk.tid() == t.tid && pk.sid() == t.sid && pk.otName() == t.name; }

void setEVsFromStatExp(PK6& pk, const std::array<uint16_t, 5>& se) {
    int ev[6];
    const int map[6] = {0, 1, 2, 3, 4, 4}; // KP, Ang, Ver, Init, SpAng=Spez, SpVer=Spez
    int sum = 0;
    for (int i = 0; i < 6; i++) { ev[i] = std::min(252, int(std::sqrt(double(se[map[i]])))); sum += ev[i]; }
    for (int i = 0; i < 6; i++) {
        if (sum > 510) ev[i] = ev[i] * 510 / sum;
        pk.d[O_EV + i] = uint8_t(ev[i]);
    }
}

void setMovesFromGB(PK6& pk, const GBMon& m) {
    for (int i = 0; i < 4; i++) {
        wr16(pk.d.data() + O_MOVES + i * 2, m.moves[i]);
        pk.d[O_PP + i] = uint8_t(m.pp[i] & 0x3F);
        pk.d[O_PPUP + i] = uint8_t(m.pp[i] >> 6);
    }
}

void setNickFromGB(PK6& pk, const GBMon& m, uint8_t language, const Names& n) {
    std::string def = defaultName(m.species, language, n);
    bool nicknamed = !m.nickname.empty() && m.nickname != upper(def);
    utf8ToUtf16(nicknamed ? m.nickname : def, pk.d.data() + O_NICK, 13);
    uint32_t iv = rd32(pk.d.data() + O_IV);
    iv = (iv & 0x7FFFFFFF) | (nicknamed ? 0x80000000u : 0);
    wr32(pk.d.data() + O_IV, iv);
}

} // namespace

uint32_t gbKey(const GBMon& m) {
    uint32_t h = 2166136261u;
    auto add = [&](uint8_t b) { h = (h ^ b) * 16777619u; };
    add(uint8_t(m.tid >> 8)); add(uint8_t(m.tid));
    add(uint8_t(m.dvs >> 8)); add(uint8_t(m.dvs));
    for (uint8_t b : m.otRaw) { add(b); if (b == 0x50) break; }
    return h;
}

const char* whyNotGB(const PK6& pk, int gen) {
    uint16_t s = pk.species();
    if (pk.isEgg()) return "Ei";
    if (s == 0 || s > (gen == 1 ? 151 : 251)) return "Art gibt es dort nicht";
    if (pk.form() != 0 && s != 201) return "Form gibt es dort nicht"; // Alola-Formen, Kappen-Pikachu …
    uint16_t maxMove = gen == 1 ? 165 : 251;
    for (uint16_t mv : pk.moves())
        if (mv > maxMove) return "kennt eine neuere Attacke";
    if (pk.moves()[0] == 0) return "keine Attacke";
    return nullptr;
}

bool fitsGB(const PK6& pk, int gen) { return whyNotGB(pk, gen) == nullptr; }

void refreshParty7(PK6& pk, const SpeciesData& sp) {
    const SpeciesInfo& si = sp.get(pk.species());
    uint32_t exp = std::min(pk.exp(), expForLevel(si.growth, 100));
    wr32(pk.d.data() + O_EXP, exp);
    uint8_t level = levelForExp(si.growth, exp);
    uint32_t iv = rd32(pk.d.data() + O_IV);
    uint8_t nature = pk.d[O_NATURE];
    wr32(pk.d.data() + 0xE8, 0); // Status
    pk.d[O_LEVEL] = level;
    for (int i = 0; i < 6; i++) {
        uint8_t ivi = uint8_t((iv >> (5 * i)) & 31);
        uint16_t v = stat7(si.base7[i], ivi, pk.d[O_EV + i], level, i, nature);
        wr16(pk.d.data() + O_STATS + i * 2, v);
        if (i == 0) wr16(pk.d.data() + O_HP, v);
    }
    pk.updateChecksum();
}

PK6 gbToPk7(const GBMon& m, const GBContext& ctx) {
    const SpeciesInfo& si = ctx.species->get(m.species);
    PK6 pk{};
    uint8_t* d = pk.d.data();
    uint32_t key = gbKey(m), rng = key ^ 0x9E3779B9u;
    if (!rng) rng = 1;
    bool own = (m.tid == ctx.trainer.tid && m.ot == ctx.trainer.name) ||
               (!ctx.gameOt.empty() && m.tid == ctx.gameTid && m.ot == ctx.gameOt);
    uint16_t tid = own ? ctx.trainer.tid : m.tid, sid = own ? ctx.trainer.sid : 0;

    wr32(d, xorshift(rng)); // EC
    wr16(d + O_SPECIES, m.species);
    wr16(d + O_ITEM, ctx.gen == 2 ? gen2ItemToModern(m.heldItem) : 0);
    wr16(d + O_TID, tid);
    wr16(d + O_SID, sid);
    wr32(d + O_EXP, m.exp);
    int ai = hiddenDisallowed(m.species, ctx.gen) ? 0 : 2;
    d[O_ABILITY] = si.ability[ai];
    d[O_ABILITYNUM] = uint8_t(1 << ai);

    uint32_t pid = xorshift(rng);
    uint16_t tsv = uint16_t(tid ^ sid);
    if (m.shiny()) pid = (pid & 0xFFFF) | (uint32_t(tsv ^ (pid & 0xFFFF)) << 16);
    else if ((tsv ^ (pid >> 16) ^ (pid & 0xFFFF)) < 16) pid ^= 0x10000000;
    wr32(d + O_PID, pid);
    d[O_NATURE] = uint8_t(m.exp % 25);
    uint8_t gender = genderFromDV(m.dv(0), si.gender);
    d[O_FLAGS] = uint8_t(((m.species == 151 || m.species == 251) ? 1 : 0) | (gender << 1));
    setEVsFromStatExp(pk, m.statExp);

    // IVs aus DVs: IV = DV·2+1 (KP-DV aus den unteren Bits der anderen)
    uint8_t dv[6] = {hpDV(m.dvs), m.dv(0), m.dv(1), m.dv(2), m.dv(3), m.dv(3)};
    uint32_t iv = 0;
    for (int i = 0; i < 6; i++) iv |= uint32_t(dv[i] * 2 + 1) << (5 * i);
    wr32(d + O_IV, iv);
    setNickFromGB(pk, m, ctx.language, *ctx.names);
    setMovesFromGB(pk, m);

    utf8ToUtf16(own ? ctx.trainer.name : m.ot, d + O_OT, 13);
    d[O_OTFRIEND] = ctx.gen == 2 ? m.friendship : si.friendship;
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    d[O_METDATE] = uint8_t(t->tm_year - 100);
    d[O_METDATE + 1] = uint8_t(t->tm_mon + 1);
    d[O_METDATE + 2] = uint8_t(t->tm_mday);
    wr16(d + O_METLOC, ctx.gen == 1 ? 30013 : 30017); // Kanto/Johto (Transporter)
    d[O_BALL] = 4;
    d[O_METLEVEL] = uint8_t(levelForExp(si.growth, m.exp) | ((own ? ctx.trainer.gender & 1 : 0) << 7));
    d[O_VERSION] = ctx.version;
    d[O_LANG] = ctx.language;
    refreshParty7(pk, *ctx.species);
    return pk;
}

void mergeGb(PK6& pk, const GBMon& now, const GBMon& before, const GBContext& ctx) {
    uint8_t* d = pk.d.data();
    if (now.species != before.species) { // Entwicklung
        wr16(d + O_SPECIES, now.species);
        uint8_t num = d[O_ABILITYNUM], idx = num == 4 ? 2 : num == 2 ? 1 : 0;
        d[O_ABILITY] = ctx.species->get(now.species).ability[idx];
        if (!pk.isNicknamed()) utf8ToUtf16(defaultName(now.species, d[O_LANG] == 2 ? 2 : 5, *ctx.names), d + O_NICK, 13);
    }
    if (now.exp != before.exp) wr32(d + O_EXP, now.exp);
    if (now.moves != before.moves || now.pp != before.pp) setMovesFromGB(pk, now);
    if (now.nickRaw != before.nickRaw) setNickFromGB(pk, now, ctx.language, *ctx.names);
    if (ctx.gen == 2) {
        if (now.heldItem != before.heldItem) wr16(d + O_ITEM, gen2ItemToModern(now.heldItem));
        if (now.friendship != before.friendship) d[O_OTFRIEND] = now.friendship;
    }
    if (now.statExp != before.statExp) setEVsFromStatExp(pk, now.statExp);
    refreshParty7(pk, *ctx.species);
}

GBMon pk7ToGb(const PK6& pk, const GBMon* snap, const GBContext& ctx) {
    const SpeciesInfo& si = ctx.species->get(pk.species());
    GBMon m;
    m.gen = ctx.gen;
    m.species = pk.species();
    m.exp = std::min(pk.exp(), expForLevel(si.growth, 100));
    m.level = levelForExp(si.growth, m.exp);
    m.moves = pk.moves();
    for (int i = 0; i < 4; i++) m.pp[i] = uint8_t(std::min<int>(pk.d[O_PP + i], 63) | (pk.d[O_PPUP + i] << 6));
    m.tid = pk.tid();

    if (snap) {
        m.dvs = snap->dvs;
        m.statExp = snap->statExp;
    } else {
        uint32_t iv = rd32(pk.d.data() + O_IV);
        auto ivAt = [&](int i) { return uint8_t((iv >> (5 * i)) & 31) >> 1; };
        uint8_t atk = ivAt(1), def = ivAt(2), spe = ivAt(3), spc = ivAt(4);
        if (pk.isShiny()) { def = spe = spc = 10; atk |= 2; }
        else if (def == 10 && spe == 10 && spc == 10 && (atk & 2)) spc = 11;
        if (ctx.gen == 2 && si.gender != 0 && si.gender < 254) { // Geschlecht passend zum Angriffs-DV
            uint8_t t = si.gender >> 4;
            auto ok = [&](uint8_t a) { return (pk.gender() == 1) == (a <= t) && (!pk.isShiny() || (a & 2)); };
            for (int k = 0; k < 16 && !ok(atk); k++) atk = uint8_t(pk.gender() == 1 ? (atk + 15) % 16 : (atk + 1) % 16);
        }
        m.dvs = uint16_t(atk << 12 | def << 8 | spe << 4 | spc);
        const int src[5] = {0, 1, 2, 3, 4};
        for (int i = 0; i < 5; i++) {
            int ev = pk.d[O_EV + src[i]];
            if (i == 4) ev = std::max<int>(pk.d[O_EV + 4], pk.d[O_EV + 5]);
            m.statExp[i] = uint16_t(std::min(65535, ev * ev));
        }
    }

    // Namen
    bool own = isOwn(pk, ctx.trainer);
    if (snap && snap->tid == m.tid) m.otRaw = snap->otRaw;
    else utf8ToGb(own ? ctx.trainer.name : pk.otName(), m.otRaw.data(), 11);
    std::string nick = pk.isNicknamed() ? pk.nickname() : upper(defaultName(m.species, ctx.language, *ctx.names));
    utf8ToGb(nick, m.nickRaw.data(), 11);
    m.ot = gbToUtf8(m.otRaw.data(), 11);
    m.nickname = gbToUtf8(m.nickRaw.data(), 11);

    // Werte
    uint8_t dvHp = hpDV(m.dvs);
    if (ctx.gen == 1) {
        m.raw.assign(44, 0);
        uint8_t* r = m.raw.data();
        uint16_t hp = statGB(si.base1[0], dvHp, m.statExp[0], m.level, true);
        r[0] = gen1NationalToInternal(m.species);
        wbe16(r + 1, hp);
        r[3] = m.level;
        r[5] = si.type1;
        r[6] = si.type2;
        r[7] = (snap && snap->gen == 1 && snap->raw.size() > 7) ? snap->raw[7] : si.catchRate;
        for (int i = 0; i < 4; i++) { r[8 + i] = uint8_t(m.moves[i]); r[29 + i] = m.pp[i]; }
        wbe16(r + 12, m.tid);
        wbe24(r + 14, m.exp);
        for (int i = 0; i < 5; i++) wbe16(r + 17 + i * 2, m.statExp[i]);
        wbe16(r + 27, m.dvs);
        r[33] = m.level;
        wbe16(r + 34, hp);
        for (int i = 1; i < 5; i++) wbe16(r + 34 + i * 2, statGB(si.base1[i], m.dv(i - 1), m.statExp[i], m.level, false));
        m.hp = m.maxHp = hp;
        m.heldItem = r[7];
    } else {
        m.raw.assign(48, 0);
        uint8_t* r = m.raw.data();
        uint16_t hp = statGB(si.base2[0], dvHp, m.statExp[0], m.level, true);
        m.heldItem = modernItemToGen2(pk.heldItem());
        m.friendship = pk.d[O_OTFRIEND];
        r[0] = uint8_t(m.species);
        r[1] = m.heldItem;
        for (int i = 0; i < 4; i++) { r[2 + i] = uint8_t(m.moves[i]); r[23 + i] = m.pp[i]; }
        wbe16(r + 6, m.tid);
        wbe24(r + 8, m.exp);
        for (int i = 0; i < 5; i++) wbe16(r + 11 + i * 2, m.statExp[i]);
        wbe16(r + 21, m.dvs);
        r[27] = m.friendship;
        if (snap && snap->gen == 2 && snap->raw.size() > 30) { r[28] = snap->raw[28]; r[29] = snap->raw[29]; r[30] = snap->raw[30]; }
        r[31] = m.level;
        wbe16(r + 34, hp);
        wbe16(r + 36, hp);
        // Ang, Ver, Init, SpAng, SpVer (Spezial-DV und -Stat-EXP für beide Spezialwerte)
        const uint8_t dvs[5] = {m.dv(0), m.dv(1), m.dv(2), m.dv(3), m.dv(3)};
        const uint16_t se[5] = {m.statExp[1], m.statExp[2], m.statExp[3], m.statExp[4], m.statExp[4]};
        for (int i = 0; i < 5; i++) wbe16(r + 38 + i * 2, statGB(si.base2[i + 1], dvs[i], se[i], m.level, false));
        m.hp = m.maxHp = hp;
    }
    return m;
}

} // namespace pw3d
