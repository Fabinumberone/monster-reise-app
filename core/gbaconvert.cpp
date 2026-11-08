#include "gbaconvert.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstring>
#include <ctime>

#include "bytes.hpp"
#include "gbconvert.hpp"
#include "gen3tables.hpp"

namespace pw3d {

namespace {

// Gen-7-Felder (siehe gbconvert.cpp)
constexpr int O_SPECIES = 0x08, O_ITEM = 0x0A, O_TID = 0x0C, O_SID = 0x0E, O_EXP = 0x10, O_ABILITY = 0x14,
              O_ABILITYNUM = 0x15, O_MARKS = 0x16, O_PID = 0x18, O_NATURE = 0x1C, O_FLAGS = 0x1D, O_EV = 0x1E,
              O_CONTEST = 0x24, O_PKRS = 0x2B, O_NICK = 0x40, O_MOVES = 0x5A, O_PP = 0x62, O_PPUP = 0x66,
              O_IV = 0x74, O_OT = 0xB0, O_OTFRIEND = 0xCA, O_METDATE = 0xD4, O_METLOC = 0xDA, O_BALL = 0xDC,
              O_METLEVEL = 0xDD, O_VERSION = 0xDF, O_LANG = 0xE3;

// Gen-3-Felder im entschlüsselten PK3 (siehe gba.hpp)
constexpr int G_NICK = 0x08, G_LANG = 0x12, G_FLAGS = 0x13, G_OT = 0x14, G_MARKS = 0x1B, G_CHK = 0x1C,
              G_SPECIES = 0x20, G_ITEM = 0x22, G_EXP = 0x24, G_PPUP = 0x28, G_FRIEND = 0x29, G_MOVES = 0x2C,
              G_PP = 0x34, G_EV = 0x38, G_CONTEST = 0x3E, G_PKRS = 0x44, G_METLOC = 0x45, G_ORIGINS = 0x46,
              G_IV = 0x48, G_RIBBONS = 0x4C;

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

uint8_t gen3Gender(uint32_t pid, uint8_t ratio) {
    if (ratio == 255) return 2;
    if (ratio == 254) return 1;
    if (ratio == 0) return 0;
    return (pid & 0xFF) < ratio ? 1 : 0;
}

uint8_t unownLetter(uint32_t pid) {
    uint32_t v = ((pid >> 24) & 3) << 6 | ((pid >> 16) & 3) << 4 | ((pid >> 8) & 3) << 2 | (pid & 3);
    return uint8_t(v % 28);
}

bool isGen3Version(uint8_t v) { return (v >= 1 && v <= 5) || v == 15; }

bool isOwnGen3(uint16_t tid, uint16_t sid, const std::string& ot, const GBAContext& ctx) {
    std::string shortName = gen3TrainerName(ctx.trainer.name);
    if (tid == ctx.trainer.tid && sid == ctx.trainer.sid && (ot == ctx.trainer.name || ot == shortName)) return true;
    return !ctx.gameOt.empty() && tid == ctx.gameTid && sid == ctx.gameSid && ot == ctx.gameOt;
}

void setNick7(PK6& pk, const PK3& g, const GBAContext& ctx) {
    uint8_t lang = g.language() == 2 ? 2 : 5;
    std::string def = g.isEgg() ? (lang == 2 ? "Egg" : "Ei") : defaultName(g.species(), lang, *ctx.names);
    std::string nick = g.nickname();
    bool nicknamed = !g.isEgg() && !nick.empty() && nick != upper(def);
    utf8ToUtf16(nicknamed ? nick : def, pk.d.data() + O_NICK, 13);
    uint32_t iv = rd32(pk.d.data() + O_IV);
    wr32(pk.d.data() + O_IV, (iv & 0x7FFFFFFF) | (nicknamed ? 0x80000000u : 0));
}

void setMoves7(PK6& pk, const PK3& g) {
    for (int i = 0; i < 4; i++) {
        wr16(pk.d.data() + O_MOVES + i * 2, rd16(&g.d[size_t(G_MOVES + i * 2)]));
        pk.d[O_PP + i] = g.d[size_t(G_PP + i)];
        pk.d[O_PPUP + i] = uint8_t((g.d[G_PPUP] >> (2 * i)) & 3);
    }
}

uint16_t reverseItem(uint16_t modern) {
    static std::array<uint16_t, 1024> rev{};
    static bool built = false;
    if (!built) {
        for (int i = kGen3ItemCount - 1; i > 0; i--)
            if (kGen3ItemToModern[i] && kGen3ItemToModern[i] < rev.size()) rev[kGen3ItemToModern[i]] = uint16_t(i);
        built = true;
    }
    return modern < rev.size() ? rev[modern] : 0;
}

} // namespace

std::string gen3TrainerName(const std::string& s) {
    size_t i = 0;
    for (int n = 0; n < 7 && i < s.size(); n++) {
        unsigned char c = s[i];
        i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    }
    return s.substr(0, std::min(i, s.size()));
}

uint16_t gen3ItemToModern(uint16_t id) { return id < kGen3ItemCount ? kGen3ItemToModern[id] : 0; }
uint16_t modernItemToGen3(uint16_t id) { return reverseItem(id); }

uint8_t gen3OriginVersion(const SaveGBA& s) {
    int count[16] = {};
    auto add = [&](const PK3& m) {
        if (m.present() && m.tid() == s.tid() && m.sid() == s.sid() && m.version() < 16) count[m.version()]++;
    };
    for (const PK3& m : s.party()) add(m);
    for (int b = 0; b < SaveGBA::BOXES; b++)
        for (int i = 0; i < SaveGBA::SLOTS; i++) add(s.boxSlot(b, i));
    int best = 0;
    for (int v = 1; v <= 5; v++)
        if (count[v] > count[best]) best = v;
    if (best) return uint8_t(best);
    return s.game() == GBAGame::RS ? 2 : s.game() == GBAGame::E ? 3 : 4;
}

uint8_t versionFromFileName(const std::string& fn) {
    std::string s;
    for (char c : fn) s += char(std::tolower(static_cast<unsigned char>(c)));
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    if (has("schwarz 2") || has("black 2")) return 23;
    if (has("weiss 2") || has("weiß 2") || has("white 2")) return 22;
    if (has("schwarz") || has("black")) return 21;
    if (has("weiss") || has("weiß") || has("white")) return 20;
    if (has("platin")) return 12;
    if (has("perl") || has("pearl")) return 11;
    if (has("diamant") || has("diamond")) return 10;
    if (has("soul") ) return 8;
    if (has("heart")) return 7;
    if (has("blattgr") || has("leafgreen") || has("leaf green")) return 5;
    if (has("feuerrot") || has("firered") || has("fire red")) return 4;
    if (has("smaragd") || has("emerald")) return 3;
    if (has("rubin") || has("ruby")) return 2;
    if (has("saphir") || has("sapphire")) return 1;
    return 0;
}

const char* whyNotGen3(const PK6& pk) {
    uint16_t s = pk.species();
    if (s == 0 || s > 386) return "Art gibt es dort nicht";
    if (pk.isEgg() && !isGen3Version(pk.version())) return "Ei aus einer anderen Generation";
    if (pk.form() != 0 && s != 201 && s != 386) return "Form gibt es dort nicht"; // Alola-Formen, Kappen-Pikachu, Proto-Formen …
    for (uint16_t m : pk.moves())
        if (m > 354) return "kennt eine neuere Attacke";
    if (pk.heldItem() && !modernItemToGen3(pk.heldItem())) return "trägt ein neueres Item";
    return nullptr;
}

bool fitsGen3(const PK6& pk) { return whyNotGen3(pk) == nullptr; }

PK6 gen3ToPk7(const PK3& g, const GBAContext& ctx) {
    uint16_t species = g.species();
    const SpeciesInfo& si = ctx.species->get(species);
    PK6 pk{};
    uint8_t* d = pk.d.data();
    uint32_t pid = g.pid();
    std::string ot = g.otName();
    bool own = false; // Umschreiben übernimmt der Speicher (Schillern bleibt erhalten)
    (void)isOwnGen3;

    wr32(d, pid); // EC = PID (wie Poké Transporter)
    wr16(d + O_SPECIES, species);
    wr16(d + O_ITEM, gen3ItemToModern(g.heldItem()));
    wr16(d + O_TID, own ? ctx.trainer.tid : g.tid());
    wr16(d + O_SID, own ? ctx.trainer.sid : g.sid());
    wr32(d + O_EXP, g.exp());
    int slot = int(g.ivWord() >> 31);
    d[O_ABILITY] = si.ability[size_t(slot)];
    d[O_ABILITYNUM] = uint8_t(1 << slot);
    uint8_t marks = 0;
    for (int i = 0; i < 4; i++)
        if (g.d[G_MARKS] >> i & 1) marks |= uint8_t(1 << (2 * i));
    wr16(d + O_MARKS, marks);
    wr32(d + O_PID, pid);
    d[O_NATURE] = uint8_t(pid % 25);
    uint8_t form = species == 201 ? unownLetter(pid) : 0;
    bool fateful = rd32(&g.d[G_RIBBONS]) >> 31;
    d[O_FLAGS] = uint8_t((fateful ? 1 : 0) | (gen3Gender(pid, si.gender) << 1) | (form << 3));
    std::memcpy(d + O_EV, &g.d[G_EV], 6);
    std::memcpy(d + O_CONTEST, &g.d[G_CONTEST], 6);
    d[O_PKRS] = g.d[G_PKRS];
    wr32(d + O_IV, g.ivWord() & 0x7FFFFFFF); // Bit 30 Ei bleibt, Bit 31 = Spitzname (setNick7)
    setNick7(pk, g, ctx);
    setMoves7(pk, g);

    utf8ToUtf16(own ? ctx.trainer.name : ot, d + O_OT, 13);
    d[O_OTFRIEND] = g.friendship();
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    d[O_METDATE] = uint8_t(t->tm_year - 100);
    d[O_METDATE + 1] = uint8_t(t->tm_mon + 1);
    d[O_METDATE + 2] = uint8_t(t->tm_mday);
    wr16(d + O_METLOC, 30001); // Poké Transfer
    uint8_t ball = g.ball();
    d[O_BALL] = ball ? ball : 4;
    uint16_t origins = rd16(&g.d[G_ORIGINS]);
    d[O_METLEVEL] = uint8_t(levelForExp(si.growth, g.exp()) | ((origins >> 15) << 7));
    d[O_VERSION] = g.version();
    d[O_LANG] = g.language() >= 1 && g.language() <= 7 ? g.language() : 2;
    refreshParty7(pk, *ctx.species);
    return pk;
}

void mergeGen3(PK6& pk, const PK3& now, const PK3& before, const GBAContext& ctx) {
    uint8_t* d = pk.d.data();
    if (now.isEgg() != before.isEgg()) { // geschlüpft: alles aus Gen 3 neu
        PK6 fresh = gen3ToPk7(now, ctx);
        std::memcpy(fresh.d.data(), d, 4); // EC behalten
        pk = fresh;
        return;
    }
    if (now.species() != before.species()) { // Entwicklung
        wr16(d + O_SPECIES, now.species());
        uint8_t num = d[O_ABILITYNUM], idx = num == 4 ? 2 : num == 2 ? 1 : 0;
        d[O_ABILITY] = ctx.species->get(now.species()).ability[idx];
        if (!pk.isNicknamed()) utf8ToUtf16(defaultName(now.species(), d[O_LANG] == 2 ? 2 : 5, *ctx.names), d + O_NICK, 13);
    }
    if (now.exp() != before.exp()) wr32(d + O_EXP, now.exp());
    if (now.moves() != before.moves() || std::memcmp(&now.d[G_PP], &before.d[G_PP], 4) != 0 || now.d[G_PPUP] != before.d[G_PPUP])
        setMoves7(pk, now);
    if (now.heldItem() != before.heldItem()) wr16(d + O_ITEM, gen3ItemToModern(now.heldItem()));
    if (now.friendship() != before.friendship()) d[O_OTFRIEND] = now.friendship();
    if (std::memcmp(&now.d[G_NICK], &before.d[G_NICK], 10) != 0) setNick7(pk, now, ctx);
    if (std::memcmp(&now.d[G_EV], &before.d[G_EV], 6) != 0) std::memcpy(d + O_EV, &now.d[G_EV], 6);
    if (std::memcmp(&now.d[G_CONTEST], &before.d[G_CONTEST], 6) != 0) std::memcpy(d + O_CONTEST, &now.d[G_CONTEST], 6);
    if (now.d[G_PKRS] != before.d[G_PKRS]) d[O_PKRS] = now.d[G_PKRS];
    refreshParty7(pk, *ctx.species);
}

PK3 pk7ToGen3(const PK6& pk, const PK3* snap, const GBAContext& ctx) {
    uint16_t species = pk.species();
    const SpeciesInfo& si = ctx.species->get(species);
    PK3 g;
    if (snap) g = *snap;
    g.party = true;
    uint8_t* d = g.d.data();
    const uint8_t* s = pk.d.data();
    bool own = pk.tid() == ctx.trainer.tid && pk.sid() == ctx.trainer.sid && pk.otName() == ctx.trainer.name;
    uint16_t tid = pk.tid(), sid = pk.sid();

    // PID: bisherige Gen-3-PID, eigene aus Gen 3, sonst passend gesucht (fest je EC)
    uint32_t pid;
    if (snap) {
        pid = snap->pid();
    } else if (isGen3Version(pk.version()) && pk.ec() == pk.pid()) {
        pid = pk.pid();
    } else {
        pid = pk.pid();
        uint32_t r = pk.ec() ^ 0x5BD1E995u;
        if (!r) r = 1;
        bool shiny = pk.isShiny();
        uint16_t tsv = uint16_t(tid ^ sid);
        for (int i = 0; i < (1 << 22); i++) {
            uint32_t a = xorshift(r), b = xorshift(r);
            uint16_t lo = uint16_t(a), hi = shiny ? uint16_t(tsv ^ lo ^ (b & 7)) : uint16_t(b >> 16);
            uint32_t c = uint32_t(hi) << 16 | lo;
            if (!shiny && uint16_t(tsv ^ hi ^ lo) < 8) continue;
            if (c % 25 != s[O_NATURE] % 25) continue;
            if (si.gender != 0 && si.gender < 254 && gen3Gender(c, si.gender) != pk.gender()) continue;
            if (species == 201 && unownLetter(c) != pk.form()) continue;
            pid = c;
            break;
        }
    }
    wr32(d, pid);
    wr16(d + 4, tid);
    wr16(d + 6, sid);

    uint8_t lang = s[O_LANG] >= 1 && s[O_LANG] <= 7 ? s[O_LANG] : 2;
    std::string nick = pk.isEgg() ? std::string(lang == 2 ? "EGG" : "EI")
                                  : pk.isNicknamed() ? pk.nickname() : upper(defaultName(species, lang == 2 ? 2 : 5, *ctx.names));
    gen3Encode(nick, d + G_NICK, 10);
    d[G_LANG] = lang;
    d[G_FLAGS] = uint8_t(0x02 | (pk.isEgg() ? 0x04 : 0));
    gen3Encode(own ? gen3TrainerName(ctx.trainer.name) : pk.otName(), d + G_OT, 7);
    if (!snap) {
        uint16_t m7 = rd16(s + O_MARKS);
        uint8_t m = 0;
        for (int i = 0; i < 4; i++)
            if ((m7 >> (2 * i)) & 3) m |= uint8_t(1 << i);
        d[G_MARKS] = m;
    }
    wr16(d + G_SPECIES, kGen3NationalToInternal[species]);
    wr16(d + G_ITEM, modernItemToGen3(pk.heldItem()));
    const uint32_t exp = std::min(pk.exp(), expForLevel(si.growth, 100));
    wr32(d + G_EXP, exp);
    uint8_t ppup = 0;
    for (int i = 0; i < 4; i++) {
        ppup |= uint8_t((s[O_PPUP + i] & 3) << (2 * i));
        wr16(d + G_MOVES + i * 2, rd16(s + O_MOVES + i * 2));
        d[G_PP + i] = s[O_PP + i];
    }
    d[G_PPUP] = ppup;
    d[G_FRIEND] = s[O_OTFRIEND];
    std::memcpy(d + G_EV, s + O_EV, 6);
    std::memcpy(d + G_CONTEST, s + O_CONTEST, 6);
    d[G_PKRS] = s[O_PKRS];
    if (!snap) {
        d[G_METLOC] = 0xFF; // Gen 3 kennt den Fundort nicht
        uint8_t ver = isGen3Version(pk.version()) ? pk.version() : ctx.version;
        uint8_t ball = s[O_BALL] >= 1 && s[O_BALL] <= 12 ? s[O_BALL] : 4;
        uint16_t origins = uint16_t((s[O_METLEVEL] & 0x7F) | (ver << 7) | (ball << 11) | ((s[O_METLEVEL] >> 7) << 15));
        wr16(d + G_ORIGINS, origins);
        wr32(d + G_RIBBONS, (s[O_FLAGS] & 1) ? 0x80000000u : 0); // Schicksalhafte Begegnung (Mew, Deoxys …)
    }
    uint32_t iv = pk.iv32() & 0x3FFFFFFF;
    if (pk.isEgg()) iv |= 1u << 30;
    if (s[O_ABILITYNUM] == 2) iv |= 1u << 31;
    wr32(d + G_IV, iv);

    // Teamwerte (Gen-3-Wesen = PID % 25)
    uint8_t level = levelForExp(si.growth, exp);
    std::memset(d + 0x50, 0, 4);
    d[0x54] = level;
    d[0x55] = 0xFF; // keine Post
    for (int i = 0; i < 6; i++) {
        uint16_t v = stat7(si.base7[i], uint8_t((iv >> (5 * i)) & 31), s[O_EV + i], level, i, uint8_t(pid % 25));
        // Reihenfolge im Team: KP, max. KP, Ang, Ver, Init, SpAng, SpVer
        if (i == 0) { wr16(d + 0x56, v); wr16(d + 0x58, v); }
        else wr16(d + 0x58 + i * 2, v);
    }
    wr16(d + G_CHK, g.calcChecksum());
    return g;
}

} // namespace pw3d
