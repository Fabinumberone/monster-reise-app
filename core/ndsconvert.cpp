#include "ndsconvert.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

#include "bytes.hpp"
#include "gbaconvert.hpp"
#include "gbconvert.hpp"

namespace pw3d {

namespace {

// Gen-7-Felder (siehe gbconvert.cpp)
constexpr int O_SPECIES = 0x08, O_ITEM = 0x0A, O_TID = 0x0C, O_SID = 0x0E, O_EXP = 0x10, O_ABILITY = 0x14,
              O_ABILITYNUM = 0x15, O_MARKS = 0x16, O_PID = 0x18, O_NATURE = 0x1C, O_FLAGS = 0x1D, O_EV = 0x1E,
              O_CONTEST = 0x24, O_PKRS = 0x2B, O_NICK = 0x40, O_MOVES = 0x5A, O_PP = 0x62, O_PPUP = 0x66,
              O_IV = 0x74, O_OT = 0xB0, O_OTFRIEND = 0xCA, O_METDATE = 0xD4, O_METLOC = 0xDA, O_BALL = 0xDC,
              O_METLEVEL = 0xDD, O_VERSION = 0xDF, O_LANG = 0xE3;

// PK4/PK5-Felder (entschlüsselt)
constexpr int N_SPECIES = 0x08, N_ITEM = 0x0A, N_TID = 0x0C, N_SID = 0x0E, N_EXP = 0x10, N_FRIEND = 0x14,
              N_ABILITY = 0x15, N_MARKS = 0x16, N_LANG = 0x17, N_EV = 0x18, N_CONTEST = 0x1E, N_MOVES = 0x28,
              N_PP = 0x30, N_PPUP = 0x34, N_IV = 0x38, N_FLAGS = 0x40, N_NATURE5 = 0x41, N_HIDDEN5 = 0x42,
              N_METLOC_PT = 0x46, N_NICK = 0x48, N_VERSION = 0x5F, N_OT = 0x68, N_METDATE = 0x7B, N_METLOC = 0x80,
              N_PKRS = 0x82, N_BALL = 0x83, N_METLEVEL = 0x84, N_BALL_HGSS = 0x86;

struct Limits { uint16_t species, move, item; uint8_t ability, ball, maxVersion; };
Limits limitsOf(NDSGame g) {
    switch (g) {
    case NDSGame::DP: return {493, 467, 464, 123, 16, 15};
    case NDSGame::Pt: return {493, 467, 467, 123, 16, 15};
    case NDSGame::HGSS: return {493, 467, 536, 123, 24, 15};
    case NDSGame::BW: return {649, 559, 632, 164, 25, 23};
    case NDSGame::B2W2: return {649, 559, 638, 164, 25, 23};
    }
    return {0, 0, 0, 0, 0, 0};
}

bool formAllowed(uint16_t s, uint8_t form, NDSGame g) {
    if (form == 0) return true;
    switch (s) {
    case 201: case 386: case 412: case 413: case 422: case 423: case 493: return true;
    case 479: case 487: case 492: return g != NDSGame::DP;  // Rotom, Giratina, Shaymin ab Platin
    case 172: return g == NDSGame::HGSS;                     // Strubbelohr-Pichu
    case 550: case 585: case 586: case 647: case 649: return ndsGeneration(g) == 5;
    case 641: case 642: case 645: case 646: return g == NDSGame::B2W2; // Tiergeistformen, Kyurem
    default: return false;
    }
}

std::string upper(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c >= 'a' && c <= 'z') { o += char(c - 32); continue; }
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = s[i + 1];
            if (d == 0xA4 || d == 0xB6 || d == 0xBC || d == 0xA9) { o += char(c); o += char(d - 0x20); i++; continue; }
        }
        o += char(c);
    }
    return o;
}

// Artname, wie ihn das Spiel ohne Spitznamen zeigt (Gen 4 in Großbuchstaben).
std::string defaultName(uint16_t species, uint8_t language, int gen, const Names& n) {
    std::string s = language == 2 ? n.speciesEn(species) : n.species(species);
    return gen == 4 ? upper(s) : s;
}

uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

uint8_t pidGender(uint32_t pid, uint8_t ratio) {
    if (ratio == 255) return 2;
    if (ratio == 254) return 1;
    if (ratio == 0) return 0;
    return (pid & 0xFF) < ratio ? 1 : 0;
}

bool isOwn45(uint16_t tid, uint16_t sid, const std::string& ot, const NDSContext& ctx) {
    if (tid == ctx.trainer.tid && sid == ctx.trainer.sid && (ot == ctx.trainer.name || ot == ndsTrainerName(ctx.trainer.name)))
        return true;
    return !ctx.gameOt.empty() && tid == ctx.gameTid && sid == ctx.gameSid && ot == ctx.gameOt;
}

void setNick7(PK6& pk, const PK45& n, const NDSContext& ctx) {
    uint8_t lang = n.language() == 2 ? 2 : 5;
    bool nicknamed = n.isNicknamed() && !n.isEgg();
    std::string name = n.isEgg() ? (lang == 2 ? "Egg" : "Ei") : nicknamed ? n.nickname() : (lang == 2 ? ctx.names->speciesEn(n.species()) : ctx.names->species(n.species()));
    utf8ToUtf16(name, pk.d.data() + O_NICK, 13);
    uint32_t iv = rd32(pk.d.data() + O_IV);
    wr32(pk.d.data() + O_IV, (iv & 0x7FFFFFFF) | (nicknamed ? 0x80000000u : 0));
}

void setMoves7(PK6& pk, const PK45& n) {
    for (int i = 0; i < 4; i++) {
        wr16(pk.d.data() + O_MOVES + i * 2, rd16(&n.d[size_t(N_MOVES + i * 2)]));
        pk.d[O_PP + i] = n.d[size_t(N_PP + i)];
        pk.d[O_PPUP + i] = n.d[size_t(N_PPUP + i)];
    }
}

uint8_t abilityNumber(const PK45& n) {
    if (n.gen == 5 && (n.d[N_HIDDEN5] & 1)) return 4;
    uint32_t bit = n.gen == 5 ? (n.pid() >> 16) & 1 : n.pid() & 1;
    return uint8_t(1u << bit);
}

} // namespace

std::string ndsTrainerName(const std::string& s) {
    if (trainerShortNameSet()) return shortTrainerName(s);
    size_t i = 0;
    for (int n = 0; n < 7 && i < s.size(); n++) {
        unsigned char c = s[i];
        i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    }
    return s.substr(0, std::min(i, s.size()));
}

uint8_t ndsOriginVersion(const SaveNDS& s) {
    int count[32] = {};
    auto add = [&](const PK45& m) {
        if (m.present() && m.tid() == s.tid() && m.sid() == s.sid() && m.version() < 32) count[m.version()]++;
    };
    for (const PK45& m : s.party()) add(m);
    for (int b = 0; b < s.boxCount(); b++)
        for (int i = 0; i < SaveNDS::SLOTS; i++) add(s.boxSlot(b, i));
    int best = 0;
    for (int v : {7, 8, 10, 11, 12, 20, 21, 22, 23})
        if (count[v] > count[best]) best = v;
    if (best) return uint8_t(best);
    switch (s.game()) {
    case NDSGame::DP: return 10;
    case NDSGame::Pt: return 12;
    case NDSGame::HGSS: return 7;
    case NDSGame::BW: return 20;
    default: return 22;
    }
}

const char* whyNotGen45(const PK6& pk, NDSGame g) {
    Limits L = limitsOf(g);
    uint16_t s = pk.species();
    if (s == 0 || s > L.species) return "Art gibt es dort nicht";
    if (pk.isEgg() && (pk.version() == 0 || pk.version() > L.maxVersion)) return "Ei aus einer neueren Generation";
    if (!formAllowed(s, pk.form(), g)) return "Form gibt es dort nicht";
    for (uint16_t m : pk.moves())
        if (m > L.move) return "kennt eine neuere Attacke";
    if (pk.heldItem() > L.item) return "trägt ein neueres Item";
    return nullptr;
}

bool fitsGen45(const PK6& pk, NDSGame g) { return whyNotGen45(pk, g) == nullptr; }

PK6 gen45ToPk7(const PK45& n, const NDSContext& ctx) {
    uint16_t species = n.species();
    const SpeciesInfo& si = ctx.species->get(species);
    PK6 pk{};
    uint8_t* d = pk.d.data();
    uint32_t pid = n.pid();
    std::string ot = n.otName();
    bool own = false; // Umschreiben übernimmt der Speicher (Schillern bleibt erhalten)
    (void)isOwn45;

    wr32(d, pid); // EC = PID (Poké Transporter)
    wr16(d + O_SPECIES, species);
    wr16(d + O_ITEM, n.heldItem());
    wr16(d + O_TID, own ? ctx.trainer.tid : n.tid());
    wr16(d + O_SID, own ? ctx.trainer.sid : n.sid());
    wr32(d + O_EXP, n.exp());
    d[O_ABILITY] = n.ability();
    d[O_ABILITYNUM] = abilityNumber(n);
    uint16_t marks = 0;
    for (int i = 0; i < 6; i++)
        if (n.d[N_MARKS] >> i & 1) marks |= uint16_t(1 << (2 * i));
    wr16(d + O_MARKS, marks);
    wr32(d + O_PID, pid);
    d[O_NATURE] = n.nature();
    d[O_FLAGS] = n.d[N_FLAGS]; // Schicksal, Geschlecht, Form
    std::memcpy(d + O_EV, &n.d[N_EV], 6);
    std::memcpy(d + O_CONTEST, &n.d[N_CONTEST], 6);
    d[O_PKRS] = n.d[N_PKRS];
    wr32(d + O_IV, n.iv32() & 0x7FFFFFFF);
    setNick7(pk, n, ctx);
    setMoves7(pk, n);

    utf8ToUtf16(own ? ctx.trainer.name : ot, d + O_OT, 13);
    d[O_OTFRIEND] = n.friendship();
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    d[O_METDATE] = uint8_t(t->tm_year - 100);
    d[O_METDATE + 1] = uint8_t(t->tm_mon + 1);
    d[O_METDATE + 2] = uint8_t(t->tm_mday);
    wr16(d + O_METLOC, 30001); // Poké Transfer
    d[O_BALL] = n.ball() ? n.ball() : 4;
    d[O_METLEVEL] = uint8_t(levelForExp(si.growth, n.exp()) | (n.d[N_METLEVEL] & 0x80));
    d[O_VERSION] = n.version();
    d[O_LANG] = n.language() >= 1 && n.language() <= 8 ? n.language() : 2;
    refreshParty7(pk, *ctx.species);
    return pk;
}

void mergeGen45(PK6& pk, const PK45& now, const PK45& before, const NDSContext& ctx) {
    uint8_t* d = pk.d.data();
    if (now.isEgg() != before.isEgg()) { // geschlüpft
        PK6 fresh = gen45ToPk7(now, ctx);
        std::memcpy(fresh.d.data(), d, 4);
        pk = fresh;
        return;
    }
    if (now.species() != before.species() || now.form() != before.form()) { // Entwicklung, Formwechsel
        wr16(d + O_SPECIES, now.species());
        d[O_FLAGS] = uint8_t((d[O_FLAGS] & 0x07) | (now.d[N_FLAGS] & 0xF8));
        d[O_ABILITY] = now.ability();
        if (!pk.isNicknamed()) utf8ToUtf16(defaultName(now.species(), d[O_LANG] == 2 ? 2 : 5, 5, *ctx.names), d + O_NICK, 13);
    }
    if (now.exp() != before.exp()) wr32(d + O_EXP, now.exp());
    if (now.moves() != before.moves() || std::memcmp(&now.d[N_PP], &before.d[N_PP], 8) != 0) setMoves7(pk, now);
    if (now.heldItem() != before.heldItem()) wr16(d + O_ITEM, now.heldItem());
    if (now.friendship() != before.friendship()) d[O_OTFRIEND] = now.friendship();
    if (std::memcmp(&now.d[N_NICK], &before.d[N_NICK], 22) != 0) setNick7(pk, now, ctx);
    if (std::memcmp(&now.d[N_EV], &before.d[N_EV], 6) != 0) std::memcpy(d + O_EV, &now.d[N_EV], 6);
    if (std::memcmp(&now.d[N_CONTEST], &before.d[N_CONTEST], 6) != 0) std::memcpy(d + O_CONTEST, &now.d[N_CONTEST], 6);
    if (now.d[N_PKRS] != before.d[N_PKRS]) d[O_PKRS] = now.d[N_PKRS];
    refreshParty7(pk, *ctx.species);
}

PK45 pk7ToGen45(const PK6& pk, const PK45* snapIn, const NDSContext& ctx) {
    const int gen = ndsGeneration(ctx.game);
    const PK45* snap = (snapIn && snapIn->gen == gen) ? snapIn : nullptr;
    Limits L = limitsOf(ctx.game);
    uint16_t species = pk.species();
    const SpeciesInfo& si = ctx.species->get(species);
    PK45 n;
    if (snap) n = *snap;
    n.gen = gen;
    n.party = true;
    uint8_t* d = n.d.data();
    const uint8_t* s = pk.d.data();
    bool own = pk.tid() == ctx.trainer.tid && pk.sid() == ctx.trainer.sid && pk.otName() == ctx.trainer.name;
    uint16_t tid = pk.tid(), sid = pk.sid();
    uint8_t abilNum = s[O_ABILITYNUM];
    bool hidden = abilNum == 4;

    uint32_t pid;
    if (snapIn) pid = snapIn->pid();
    else if (pk.ec() == pk.pid()) pid = pk.pid();
    else {
        pid = pk.pid();
        uint32_t r = pk.ec() ^ 0x5BD1E995u;
        if (!r) r = 1;
        bool shiny = pk.isShiny();
        uint16_t tsv = uint16_t(tid ^ sid);
        int slot = abilNum == 2 ? 1 : 0;
        bool twoAbilities = si.ability[0] != si.ability[1];
        for (int i = 0; i < (1 << 22); i++) {
            uint32_t a = xorshift(r), b = xorshift(r);
            uint16_t lo = uint16_t(a), hi = shiny ? uint16_t(tsv ^ lo ^ (b & 7)) : uint16_t(b >> 16);
            uint32_t c = uint32_t(hi) << 16 | lo;
            if (!shiny && uint16_t(tsv ^ hi ^ lo) < 8) continue;
            if (gen == 4 && c % 25 != s[O_NATURE] % 25) continue;
            if (si.gender != 0 && si.gender < 254 && pidGender(c, si.gender) != pk.gender()) continue;
            if (twoAbilities && !hidden && int(gen == 5 ? (c >> 16) & 1 : c & 1) != slot) continue;
            pid = c;
            break;
        }
    }
    wr32(d, pid);
    wr16(d + 4, 0);
    wr16(d + N_SPECIES, species);
    wr16(d + N_ITEM, pk.heldItem() <= L.item ? pk.heldItem() : 0);
    wr16(d + N_TID, tid);
    wr16(d + N_SID, sid);
    wr32(d + N_EXP, std::min(pk.exp(), expForLevel(si.growth, 100)));
    d[N_FRIEND] = s[O_OTFRIEND];
    uint8_t ability = s[O_ABILITY];
    if (ability > L.ability) {
        ability = si.ability[0] <= L.ability ? si.ability[0] : si.ability[1] <= L.ability ? si.ability[1] : 1;
    }
    d[N_ABILITY] = ability;
    if (!snap) {
        uint16_t m7 = rd16(s + O_MARKS);
        uint8_t m = 0;
        for (int i = 0; i < 6; i++)
            if ((m7 >> (2 * i)) & 3) m |= uint8_t(1 << i);
        d[N_MARKS] = m;
    }
    uint8_t lang = s[O_LANG] >= 1 && s[O_LANG] <= 8 ? s[O_LANG] : 2;
    d[N_LANG] = lang;
    std::memcpy(d + N_EV, s + O_EV, 6);
    std::memcpy(d + N_CONTEST, s + O_CONTEST, 6);
    for (int i = 0; i < 4; i++) {
        wr16(d + N_MOVES + i * 2, rd16(s + O_MOVES + i * 2));
        d[N_PP + i] = s[O_PP + i];
        d[N_PPUP + i] = s[O_PPUP + i];
    }
    uint32_t iv = pk.iv32() & 0x3FFFFFFF;
    if (pk.isEgg()) iv |= 1u << 30;
    if (pk.isNicknamed()) iv |= 1u << 31;
    wr32(d + N_IV, iv);
    d[N_FLAGS] = uint8_t((s[O_FLAGS] & 0xF9) | (pk.gender() << 1)); // Schicksal, Form, Geschlecht
    if (gen == 5) {
        d[N_NATURE5] = s[O_NATURE];
        d[N_HIDDEN5] = uint8_t((d[N_HIDDEN5] & ~1) | (hidden ? 1 : 0));
    }
    std::string nick = pk.isEgg() ? std::string(lang == 2 ? "EGG" : "EI")
                     : pk.isNicknamed() ? pk.nickname() : defaultName(species, lang == 2 ? 2 : 5, gen, *ctx.names);
    ndsEncodeString(nick, d + N_NICK, 11, gen);
    ndsEncodeString(own ? ndsTrainerName(ctx.trainer.name) : pk.otName(), d + N_OT, 8, gen);
    d[N_PKRS] = s[O_PKRS];
    if (!snap) {
        uint8_t ver = pk.version() && pk.version() <= L.maxVersion ? pk.version() : ctx.version;
        d[N_VERSION] = ver;
        bool fromGen3 = (ver >= 1 && ver <= 5) || ver == 15;
        uint16_t loc = gen == 5 ? 30001 : fromGen3 ? 0x37 : 3002; // Poké Transfer / Pal Park / Ferner Ort
        wr16(d + N_METLOC, loc);
        if (gen == 4 && ctx.game != NDSGame::DP) wr16(d + N_METLOC_PT, loc);
        d[N_METDATE] = s[O_METDATE];
        d[N_METDATE + 1] = s[O_METDATE + 1];
        d[N_METDATE + 2] = s[O_METDATE + 2];
        uint8_t ball = s[O_BALL] >= 1 && s[O_BALL] <= L.ball ? s[O_BALL] : 4;
        d[N_BALL] = gen == 4 && ball > 16 ? 4 : ball;
        if (gen == 4) d[N_BALL_HGSS] = ctx.game == NDSGame::HGSS ? ball : 0;
        d[N_METLEVEL] = s[O_METLEVEL]; // Level + Trainer-Geschlecht
    }

    // Eigene Pokémon: OT-Geschlecht = Spielfigur dieses Spiels (sonst „getauscht“: Bonus-EP)
    if (own && ctx.gameGender >= 0) d[N_METLEVEL] = uint8_t((d[N_METLEVEL] & 0x7F) | ((ctx.gameGender & 1) << 7));

    // Teamwerte
    uint8_t level = levelForExp(si.growth, rd32(d + N_EXP));
    std::memset(d + 0x88, 0, n.d.size() - 0x88);
    d[0x8C] = level;
    uint8_t nature = gen == 5 ? d[N_NATURE5] : uint8_t(pid % 25);
    for (int i = 0; i < 6; i++) {
        uint16_t v = stat7(si.base7[i], uint8_t((iv >> (5 * i)) & 31), d[N_EV + i], level, i, nature);
        if (i == 0) { wr16(d + 0x8E, v); wr16(d + 0x90, v); }
        else wr16(d + 0x90 + i * 2, v);
    }
    wr16(d + 6, n.calcChecksum());
    return n;
}

} // namespace pw3d
