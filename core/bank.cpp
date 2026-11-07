#include "bank.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "bytes.hpp"
#include "items.hpp"

namespace pw3d {

namespace {

bool sameMon(const PK6& a, const PK6& b) { return a.ec() == b.ec() && a.pid() == b.pid(); }

bool present(const PK6& pk) { return pk.species() != 0 && pk.checksumValid(); }

const BankEntry* findIn(const std::array<BankEntry, Bank::PARTY>& party, const std::vector<BankEntry>& boxes,
                        const PK6& pk) {
    for (auto& e : party)
        if (e.used && sameMon(e.pk, pk)) return &e;
    for (auto& e : boxes)
        if (e.used && sameMon(e.pk, pk)) return &e;
    return nullptr;
}

// Pokémon aus einem Spielstand in die Speicherform bringen; prev = bisheriger Eintrag.
BankEntry importMon(const SaveFile& sav, const PK6& game, const BankEntry* prev) {
    BankEntry e;
    e.used = true;
    if (sav.generation() == 6) {
        e.pk = toGen7(game);
        if (prev) restoreGen7Only(e.pk, prev->pk, game.d[0x2A]);
        e.gen6 = game;
        e.hasGen6 = true;
    } else {
        e.pk = game;
        if (prev && prev->hasGen6) { e.gen6 = prev->gen6; e.hasGen6 = true; }
    }
    if (prev && prev->hasGb) { e.hasGb = true; e.gb = prev->gb; e.gbKey = prev->gbKey; } // GB-Stand mitnehmen
    return e;
}

PK6 exportMon(const BankEntry& e, GameKind k) {
    if (kindGeneration(k) == 7) return e.pk;
    PK6 p = toGen6(e.pk);
    if (e.hasGen6 && sameMon(e.gen6, e.pk)) restoreGen6Only(p, e.gen6);
    return p;
}

void reownIfFrom(PK6& pk, const Trainer6& from, const Trainer6& to) {
    if (pk.tid() == from.tid && pk.sid() == from.sid && pk.otName() == from.name) {
        pk.setTid(to.tid);
        pk.setSid(to.sid);
        pk.setOtName(to.name);
        pk.updateChecksum();
    }
}

} // namespace

BankEntry* Bank::find(const PK6& pk) {
    return const_cast<BankEntry*>(findIn(party, boxes, pk));
}

BankEntry* Bank::freeBoxSlot(std::vector<BankEntry>& bx) {
    for (auto& e : bx)
        if (!e.used) return &e;
    return nullptr;
}

int Bank::count() const {
    int n = 0;
    for (auto& e : party) n += e.used;
    for (auto& e : boxes) n += e.used;
    return n;
}

void Bank::create(const SaveFile& start, const std::vector<const SaveFile*>& others) {
    trainer = start.trainer();
    current = kindName(start.kind());
    party = {};
    boxes.assign(BOXES * SLOTS, BankEntry{});

    for (int i = 0; i < start.partyCount() && i < PARTY; i++) {
        PK6 pk = start.partySlot(i);
        if (!present(pk)) continue;
        party[i] = importMon(start, pk, nullptr);
        party[i].inGame = true;
    }
    for (int b = 0; b < start.boxCount() && b < BOXES; b++)
        for (int s = 0; s < SLOTS; s++) {
            if (start.boxSlotEmpty(b, s)) continue;
            auto& e = boxes[b * SLOTS + s];
            e = importMon(start, start.boxSlot(b, s), nullptr);
            e.inGame = true;
        }

    items.clear();
    for (const ItemStack& s : readTravelItems(start)) {
        BankItem* e = nullptr;
        for (auto& b : items)
            if (b.item == s.item) e = &b;
        if (!e) { items.push_back({s.item, 0, 0}); e = &items.back(); }
        e->count += s.count;
        e->written = uint16_t(e->written + s.count); // steht im Startspiel
    }
    money = start.money();
    moneyKnown = true;

    for (const SaveFile* o : others) {
        addItemsFrom(*o);
        Trainer6 ot = o->trainer();
        auto add = [&](PK6 pk, bool fromParty, int box, int slot) {
            if (!present(pk) || find(pk)) return;
            reownIfFrom(pk, ot, trainer);
            BankEntry e = importMon(*o, pk, nullptr);
            e.inGame = false; // steht (noch) nicht im Startspiel
            BankEntry* dst = nullptr;
            if (fromParty) {
                for (auto& p : party)
                    if (!p.used) { dst = &p; break; }
            } else if (box < BOXES && !boxes[box * SLOTS + slot].used) {
                dst = &boxes[box * SLOTS + slot];
            }
            if (!dst) dst = freeBoxSlot(boxes);
            if (dst) *dst = e;
        };
        for (int i = 0; i < o->partyCount() && i < PARTY; i++) add(o->partySlot(i), true, 0, 0);
        for (int b = 0; b < o->boxCount(); b++)
            for (int s = 0; s < SLOTS; s++)
                if (!o->boxSlotEmpty(b, s)) add(o->boxSlot(b, s), false, b, s);
    }
}

void Bank::addItemsFrom(const SaveFile& sav) {
    for (const ItemStack& s : readTravelItems(sav)) {
        BankItem* e = nullptr;
        for (auto& b : items)
            if (b.item == s.item) e = &b;
        if (!e) { items.push_back({s.item, 0, 0}); e = &items.back(); }
        e->count += s.count; // written bleibt: im aktuellen Spiel nicht sichtbar
    }
}

bool Bank::depart(const SaveFile& sav, std::string& err) {
    if (current != kindName(sav.kind())) {
        err = std::string("Der Speicher steht in ") + (current.empty() ? "keinem Spiel" : current) + ", nicht in " +
              kindName(sav.kind());
        return false;
    }
    std::array<BankEntry, PARTY> np{};
    std::vector<BankEntry> nb(BOXES * SLOTS);

    std::vector<BankEntry> team;
    for (int i = 0; i < sav.partyCount() && i < PARTY; i++) {
        PK6 pk = sav.partySlot(i);
        if (!present(pk)) continue;
        team.push_back(importMon(sav, pk, findIn(party, boxes, pk)));
    }
    // Nicht angezeigte Team-Pokémon an ihren alten Platz zurück (solange das Team nicht voll ist).
    std::vector<const BankEntry*> overflow;
    for (int i = 0; i < PARTY; i++) {
        const auto& e = party[i];
        if (!e.used || e.inGame) continue;
        bool dup = false;
        for (auto& t : team) dup |= sameMon(t.pk, e.pk);
        if (dup) continue;
        if (int(team.size()) < PARTY) team.insert(team.begin() + std::min<size_t>(size_t(i), team.size()), e);
        else overflow.push_back(&e);
    }
    for (size_t i = 0; i < team.size(); i++) np[i] = team[i];
    for (int b = 0; b < sav.boxCount() && b < BOXES; b++)
        for (int s = 0; s < SLOTS; s++) {
            if (sav.boxSlotEmpty(b, s)) continue;
            PK6 pk = sav.boxSlot(b, s);
            nb[b * SLOTS + s] = importMon(sav, pk, findIn(party, boxes, pk));
        }

    // Was das Spiel nicht angezeigt hat, bleibt erhalten.
    auto keep = [&](const BankEntry& e, BankEntry* preferred) -> bool {
        if (findIn(np, nb, e.pk)) return true; // schon da (z. B. Kopie)
        BankEntry* dst = (preferred && !preferred->used) ? preferred : nullptr;
        if (!dst) dst = freeBoxSlot(nb);
        if (!dst) return false;
        *dst = e;
        return true;
    };
    for (const BankEntry* e : overflow)
        if (!keep(*e, nullptr)) { err = "Kein Platz mehr für nicht angezeigte Pokémon"; return false; }
    for (int i = 0; i < BOXES * SLOTS; i++) {
        const auto& e = boxes[i];
        if (!e.used || e.inGame) continue;
        if (!keep(e, &nb[i])) { err = "Kein Platz mehr für nicht angezeigte Pokémon"; return false; }
    }

    // Items: Was im Spiel liegt, ist der neue Stand; was dort nicht sichtbar war, bleibt dazu.
    std::vector<BankItem> ni;
    auto addTo = [&](uint16_t item, uint32_t n) {
        if (!n) return;
        for (auto& b : ni)
            if (b.item == item) { b.count += n; return; }
        ni.push_back({item, 0, n});
    };
    for (const ItemStack& s : readTravelItems(sav)) addTo(s.item, s.count);               // Spielinhalt zuerst
    for (auto& b : items) addTo(b.item, b.count - std::min<uint32_t>(b.written, b.count)); // unsichtbarer Rest

    for (auto& e : np) e.inGame = false;
    for (auto& e : nb) e.inGame = false;
    party = np;
    boxes = nb;
    items = ni;
    money = sav.money();
    moneyKnown = true;
    current.clear();
    return true;
}

bool Bank::arrive(SaveFile& sav, int& hidden, std::string& err) {
    if (!current.empty()) {
        err = "Erst aus " + current + " abreisen";
        return false;
    }
    GameKind k = sav.kind();
    hidden = 0;

    // Leerer Platz, wie ihn das Spiel selbst verwendet.
    PK6 blank;
    bool haveBlank = false;
    for (int b = 0; b < sav.boxCount() && !haveBlank; b++)
        for (int s = 0; s < SLOTS && !haveBlank; s++)
            if (sav.boxSlot(b, s).species() == 0 && sav.boxSlot(b, s).checksumValid()) {
                blank = sav.boxSlot(b, s);
                haveBlank = true;
            }

    std::vector<BankEntry*> visible;
    for (auto& e : party)
        if (e.used && fitsIn(e.pk, k)) visible.push_back(&e);
    if (visible.empty()) {
        err = "Kein Pokémon im Team kann in diesem Spiel existieren";
        return false;
    }

    for (auto& e : party) e.inGame = false;
    for (auto& e : boxes) e.inGame = false;

    int n = 0;
    for (BankEntry* e : visible) {
        PK6 out = exportMon(*e, k);
        sav.setPartySlot(n++, out);
        sav.registerDex(out);
        e->inGame = true;
    }
    for (int i = n; i < PARTY; i++) sav.setPartySlot(i, blank);
    sav.setPartyCount(n);
    for (auto& e : party)
        if (e.used && !e.inGame) hidden++;

    for (int b = 0; b < BOXES; b++)
        for (int s = 0; s < SLOTS; s++) {
            auto& e = boxes[b * SLOTS + s];
            bool fits = e.used && b < sav.boxCount() && fitsIn(e.pk, k);
            if (b < sav.boxCount()) {
                if (fits) {
                    PK6 out = exportMon(e, k);
                    sav.setBoxSlot(b, s, out);
                    sav.registerDex(out);
                    e.inGame = true;
                } else {
                    sav.setBoxSlot(b, s, blank);
                }
            }
            if (e.used && !fits) hidden++;
        }

    // Items und Geld
    std::vector<ItemStack> stacks;
    for (auto& b : items) stacks.push_back({b.item, uint16_t(std::min<uint32_t>(b.count, 0xFFFF))});
    std::vector<uint16_t> written;
    writeTravelItems(sav, stacks, written);
    for (size_t i = 0; i < items.size(); i++) items[i].written = written[i];
    if (moneyKnown) sav.setMoney(std::min<uint32_t>(money, 9999999));

    Trainer6 t = sav.trainer();
    t.tid = trainer.tid;
    t.sid = trainer.sid;
    t.name = trainer.name;
    t.gender = trainer.gender;
    sav.setTrainer(t);
    current = kindName(k);
    return true;
}

// ---------- Gen 1/2 ----------

namespace {

const char* gbKind(int gen) { return gen == 1 ? "RBY" : "GSC"; }

bool isBall(uint16_t m) { return (m >= 1 && m <= 16) || (m >= 492 && m <= 500); }
bool isRepel(uint16_t m) { return m == 79 || m == 76 || m == 77; }

// Reist dieses Item (heutige Nummer) in einem GB-Spiel mit? Gen 1: alles mit Zuordnung (die
// Tabelle enthält nur reisende Items). Gen 2: was es in den reisenden Taschen von USUM gibt.
uint16_t gbTravelModern(int gen, uint8_t id) {
    if (gen == 1) return gen1ItemToModern(id);
    uint16_t m = gen2ItemToModern(id);
    return (m && pouchOf(GameKind::USUM, m) != Pouch::None) ? m : 0;
}

std::vector<ItemStack> gbTravelItems(const SaveGB& s) {
    std::vector<ItemStack> out;
    auto add = [&](const std::vector<GBItem>& v) {
        for (const GBItem& i : v) {
            uint16_t m = gbTravelModern(s.generation(), i.id);
            if (!m || !i.count) continue;
            bool merged = false;
            for (auto& o : out)
                if (o.item == m) { o.count = uint16_t(o.count + i.count); merged = true; }
            if (!merged) out.push_back({m, i.count});
        }
    };
    add(s.bag());
    add(s.balls());
    add(s.pcItems());
    return out;
}

int itemPriority(uint16_t m) {
    if (isBall(m)) return 0;
    if (pouchOf(GameKind::USUM, m) == Pouch::Medicine) return 1;
    if (isRepel(m)) return 2;
    return 3;
}

} // namespace

bool Bank::departGB(const SaveGB& sav, const GBContext& ctxIn, std::string& err) {
    const char* kind = gbKind(sav.generation());
    if (current != kind) {
        err = std::string("Der Speicher steht in ") + (current.empty() ? "keinem Spiel" : current) + ", nicht in " + kind;
        return false;
    }
    GBContext ctx = ctxIn;
    ctx.gameTid = sav.tid();
    ctx.gameOt = sav.trainerName();
    std::vector<const BankEntry*> matched;
    auto findGb = [&](uint32_t key) -> const BankEntry* {
        auto ok = [&](const BankEntry& e) {
            return e.used && e.hasGb && e.gbKey == key && std::find(matched.begin(), matched.end(), &e) == matched.end();
        };
        for (auto& e : party) if (ok(e)) return &e;
        for (auto& e : boxes) if (ok(e)) return &e;
        return nullptr;
    };
    auto importGB = [&](const GBMon& m) {
        uint32_t key = gbKey(m);
        const BankEntry* prev = findGb(key);
        BankEntry e;
        if (prev) {
            matched.push_back(prev);
            e = *prev;
            mergeGb(e.pk, m, prev->gb, ctx);
        } else {
            e.pk = gbToPk7(m, ctx);
        }
        e.used = true;
        e.hasGb = true;
        e.gb = m;
        e.gbKey = key;
        e.inGame = false;
        return e;
    };

    std::array<BankEntry, PARTY> np{};
    std::vector<BankEntry> nb(BOXES * SLOTS);
    std::vector<BankEntry> team;
    for (const GBMon& m : sav.party()) team.push_back(importGB(m));
    std::vector<const BankEntry*> overflow;
    for (int i = 0; i < PARTY; i++) { // nicht angezeigte Team-Pokémon an ihren alten Platz
        const auto& e = party[size_t(i)];
        if (!e.used || e.inGame) continue;
        if (int(team.size()) < PARTY) team.insert(team.begin() + std::min<size_t>(size_t(i), team.size()), e);
        else overflow.push_back(&e);
    }
    for (size_t i = 0; i < team.size(); i++) np[i] = team[i];
    for (int b = 0; b < sav.boxCount(); b++) {
        auto list = sav.box(b);
        for (size_t i = 0; i < list.size(); i++) {
            size_t idx = size_t(b) * 20 + i;
            if (idx < nb.size()) nb[idx] = importGB(list[i]);
        }
    }
    auto freeSlot = [&]() -> BankEntry* { for (auto& e : nb) if (!e.used) return &e; return nullptr; };
    auto keep = [&](const BankEntry& e, BankEntry* pref) {
        BankEntry* dst = (pref && !pref->used) ? pref : freeSlot();
        if (!dst) return false;
        *dst = e;
        return true;
    };
    for (const BankEntry* e : overflow)
        if (!keep(*e, nullptr)) { err = "Kein Platz mehr für nicht angezeigte Pokémon"; return false; }
    for (size_t i = 0; i < boxes.size(); i++)
        if (boxes[i].used && !boxes[i].inGame && !keep(boxes[i], &nb[i])) { err = "Kein Platz mehr für nicht angezeigte Pokémon"; return false; }

    // Items: Spielinhalt + vorher unsichtbarer Rest
    std::vector<BankItem> ni;
    auto addTo = [&](uint16_t item, uint32_t n) {
        if (!n) return;
        for (auto& b : ni) if (b.item == item) { b.count += n; return; }
        ni.push_back({item, 0, n});
    };
    for (const ItemStack& s : gbTravelItems(sav)) addTo(s.item, s.count);
    for (auto& b : items) addTo(b.item, b.count - std::min<uint32_t>(b.written, b.count));

    for (auto& e : np) e.inGame = false;
    for (auto& e : nb) e.inGame = false;
    party = np;
    boxes = nb;
    items = ni;
    money = sav.money();
    moneyKnown = true;
    current.clear();
    return true;
}

int Bank::addFromGB(const SaveGB& sav, const GBContext& ctxIn) {
    GBContext ctx = ctxIn;
    ctx.gameTid = sav.tid();
    ctx.gameOt = sav.trainerName();
    int added = 0;
    auto known = [&](uint32_t key) {
        for (auto& e : party) if (e.used && e.hasGb && e.gbKey == key) return true;
        for (auto& e : boxes) if (e.used && e.hasGb && e.gbKey == key) return true;
        return false;
    };
    auto add = [&](const GBMon& m, bool fromParty) {
        uint32_t key = gbKey(m);
        if (known(key)) return;
        BankEntry e;
        e.used = true;
        e.pk = gbToPk7(m, ctx);
        e.hasGb = true;
        e.gb = m;
        e.gbKey = key;
        BankEntry* dst = nullptr;
        if (fromParty) for (auto& p : party) if (!p.used) { dst = &p; break; }
        if (!dst) dst = freeBoxSlot(boxes);
        if (dst) { *dst = e; added++; }
    };
    for (const GBMon& m : sav.party()) add(m, true);
    for (int b = 0; b < sav.boxCount(); b++)
        for (const GBMon& m : sav.box(b)) add(m, false);
    for (const ItemStack& s : gbTravelItems(sav)) {
        BankItem* e = nullptr;
        for (auto& b : items) if (b.item == s.item) e = &b;
        if (!e) { items.push_back({s.item, 0, 0}); e = &items.back(); }
        e->count += s.count;
    }
    return added;
}

bool Bank::arriveGB(SaveGB& sav, const GBContext& ctx, int& hidden, std::string& err) {
    if (!current.empty()) { err = "Erst aus " + current + " abreisen"; return false; }
    const int gen = sav.generation();
    hidden = 0;
    for (auto& e : party) e.inGame = false;
    for (auto& e : boxes) e.inGame = false;

    auto exportGB = [&](BankEntry& e) {
        GBMon m = pk7ToGb(e.pk, e.hasGb ? &e.gb : nullptr, ctx);
        e.gb = m;
        e.hasGb = true;
        e.gbKey = gbKey(m);
        e.inGame = true;
        sav.registerDex(m.species);
        return m;
    };

    std::vector<GBMon> team;
    for (auto& e : party)
        if (e.used && fitsGB(e.pk, gen)) team.push_back(exportGB(e));
    if (team.empty()) { err = "Kein Pokémon im Team kann in diesem Spiel existieren"; return false; }
    sav.setParty(team);
    for (int b = 0; b < sav.boxCount(); b++) {
        std::vector<GBMon> list;
        for (int i = 0; i < 20; i++) {
            size_t idx = size_t(b) * 20 + size_t(i);
            if (idx < boxes.size() && boxes[idx].used && fitsGB(boxes[idx].pk, gen)) list.push_back(exportGB(boxes[idx]));
        }
        sav.setBox(b, list);
    }
    for (auto& e : party) hidden += e.used && !e.inGame;
    for (auto& e : boxes) hidden += e.used && !e.inGame;

    // Items: vorhandene nicht reisende Items bleiben, dann reisende nach Rangfolge
    auto keepOnly = [&](const std::vector<GBItem>& v) {
        std::vector<GBItem> out;
        for (const GBItem& i : v) if (!gbTravelModern(gen, i.id)) out.push_back(i);
        return out;
    };
    std::vector<GBItem> bag = keepOnly(sav.bag()), balls = keepOnly(sav.balls()), pc = keepOnly(sav.pcItems());
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return itemPriority(items[a].item) < itemPriority(items[b].item); });
    for (auto& b : items) b.written = 0;
    for (size_t idx : order) {
        BankItem& it = items[idx];
        uint8_t g = gen == 1 ? modernItemToGen1(it.item) : modernItemToGen2(it.item);
        if (!g || !it.count || !gbTravelModern(gen, g)) continue;
        uint8_t n = uint8_t(std::min<uint32_t>(it.count, 99));
        std::vector<GBItem>* dst = nullptr;
        if (gen == 2 && isBall(it.item) && balls.size() < 12) dst = &balls;
        else if (!(gen == 2 && isBall(it.item)) && bag.size() < 20) dst = &bag;
        else if (pc.size() < 50) dst = &pc;
        if (!dst) continue;
        dst->push_back({g, n});
        it.written = n;
    }
    sav.setBag(bag);
    sav.setBalls(balls);
    sav.setPcItems(pc);
    if (moneyKnown) sav.setMoney(std::min<uint32_t>(money, 999999));
    sav.setTrainer(trainer.name, trainer.tid);
    current = gbKind(gen);
    return true;
}

// ---------- Datei ----------
// "PW3DBANK", u32 Version 1, Trainer (TID, SID, Geschlecht, 3 frei, Name 26 Byte UTF-16),
// aktuelles Spiel (16 Byte), dann 6 + 960 Einträge: Flags (1 belegt, 2 im Spiel, 4 Gen-6-Stand),
// 3 frei, 0x104 Gen-7-Form, 0x104 Gen-6-Stand. Unverschlüsselt.
// Version 2 hängt an: u32 Geld, u8 Geld bekannt, 3 frei, u32 Anzahl Items, je Item
// u16 Item, u16 geschrieben, u32 Anzahl. Version 1 wird weiter gelesen (ohne Items/Geld).
// Version 3 hängt zusätzlich an: u32 Anzahl Gen-1/2-Stände, je Stand u16 Eintrag (0–5 Team,
// danach Boxplätze), u32 Schlüssel, u8 Generation, u8 Länge, 48 Byte Datensatz, 11 Byte OT-Name,
// 11 Byte Spitzname (Spiel-Zeichensatz).

namespace {
constexpr char MAGIC[8] = {'P', 'W', '3', 'D', 'B', 'A', 'N', 'K'};
constexpr size_t ENTRY = 4 + 2 * PK6_PARTY_SIZE;
constexpr size_t HEADER = 8 + 4 + 8 + 26 + 16;
constexpr size_t GBREC = 2 + 4 + 1 + 1 + 48 + 11 + 11;
} // namespace

bool Bank::save(const std::string& path, std::string& err) const {
    std::vector<uint8_t> out(HEADER + (PARTY + BOXES * SLOTS) * ENTRY, 0);
    std::memcpy(out.data(), MAGIC, 8);
    wr32(&out[8], 3);
    wr16(&out[12], trainer.tid);
    wr16(&out[14], trainer.sid);
    out[16] = trainer.gender;
    utf8ToUtf16(trainer.name, &out[20], 13);
    std::memcpy(&out[46], current.c_str(), current.size() < 15 ? current.size() : 15);
    size_t p = HEADER;
    auto put = [&](const BankEntry& e) {
        out[p] = uint8_t((e.used ? 1 : 0) | (e.inGame ? 2 : 0) | (e.hasGen6 ? 4 : 0));
        if (e.used) {
            std::memcpy(&out[p + 4], e.pk.d.data(), PK6_PARTY_SIZE);
            if (e.hasGen6) std::memcpy(&out[p + 4 + PK6_PARTY_SIZE], e.gen6.d.data(), PK6_PARTY_SIZE);
        }
        p += ENTRY;
    };
    for (auto& e : party) put(e);
    for (auto& e : boxes) put(e);

    // Version 2: Geld und Items
    size_t tail = out.size();
    out.resize(tail + 12 + items.size() * 8, 0);
    wr32(&out[tail], money);
    out[tail + 4] = moneyKnown ? 1 : 0;
    wr32(&out[tail + 8], uint32_t(items.size()));
    for (size_t i = 0; i < items.size(); i++) {
        uint8_t* q = &out[tail + 12 + i * 8];
        wr16(q, items[i].item);
        wr16(q + 2, items[i].written);
        wr32(q + 4, items[i].count);
    }

    // Version 3: Gen-1/2-Stände
    std::vector<std::pair<uint16_t, const BankEntry*>> gbs;
    for (int i = 0; i < PARTY; i++)
        if (party[size_t(i)].used && party[size_t(i)].hasGb) gbs.push_back({uint16_t(i), &party[size_t(i)]});
    for (size_t i = 0; i < boxes.size(); i++)
        if (boxes[i].used && boxes[i].hasGb) gbs.push_back({uint16_t(PARTY + i), &boxes[i]});
    tail = out.size();
    out.resize(tail + 4 + gbs.size() * GBREC, 0);
    wr32(&out[tail], uint32_t(gbs.size()));
    for (size_t i = 0; i < gbs.size(); i++) {
        uint8_t* q = &out[tail + 4 + i * GBREC];
        const BankEntry& e = *gbs[i].second;
        wr16(q, gbs[i].first);
        wr32(q + 2, e.gbKey);
        q[6] = uint8_t(e.gb.gen);
        q[7] = uint8_t(std::min<size_t>(e.gb.raw.size(), 48));
        std::memcpy(q + 8, e.gb.raw.data(), q[7]);
        std::memcpy(q + 56, e.gb.otRaw.data(), 11);
        std::memcpy(q + 67, e.gb.nickRaw.data(), 11);
    }

    std::string tmp = path + ".neu";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        if (!f) { err = "Kann " + tmp + " nicht schreiben"; return false; }
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { err = "Kann " + path + " nicht ersetzen"; return false; }
    return true;
}

bool Bank::load(const std::string& path, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "Kann " + path + " nicht lesen"; return false; }
    std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), {});
    const size_t base = HEADER + (PARTY + BOXES * SLOTS) * ENTRY;
    uint32_t version = in.size() >= 12 ? rd32(&in[8]) : 0;
    if (in.size() < base || std::memcmp(in.data(), MAGIC, 8) != 0 || version < 1 || version > 3 ||
        (version == 1 && in.size() != base) || (version >= 2 && in.size() < base + 12)) {
        err = path + " ist kein gemeinsamer Speicher (Version 1 bis 3)";
        return false;
    }
    items.clear();
    money = 0;
    moneyKnown = false;
    size_t gbAt = 0;
    if (version >= 2) {
        money = rd32(&in[base]);
        moneyKnown = in[base + 4] != 0;
        uint32_t n = rd32(&in[base + 8]);
        size_t end = base + 12 + size_t(n) * 8;
        if (in.size() < end || (version == 2 && in.size() != end)) { err = path + ": Itemliste beschädigt"; return false; }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t* q = &in[base + 12 + i * 8];
            items.push_back({rd16(q), rd16(q + 2), rd32(q + 4)});
        }
        gbAt = end;
    }
    trainer.tid = rd16(&in[12]);
    trainer.sid = rd16(&in[14]);
    trainer.gender = in[16];
    trainer.name = utf16ToUtf8(&in[20], 13);
    current.assign(reinterpret_cast<const char*>(&in[46]), strnlen(reinterpret_cast<const char*>(&in[46]), 16));
    size_t p = HEADER;
    auto get = [&](BankEntry& e) {
        e = BankEntry{};
        uint8_t fl = in[p];
        e.used = fl & 1;
        e.inGame = fl & 2;
        e.hasGen6 = fl & 4;
        std::memcpy(e.pk.d.data(), &in[p + 4], PK6_PARTY_SIZE);
        std::memcpy(e.gen6.d.data(), &in[p + 4 + PK6_PARTY_SIZE], PK6_PARTY_SIZE);
        p += ENTRY;
    };
    for (auto& e : party) get(e);
    boxes.assign(BOXES * SLOTS, BankEntry{});
    for (auto& e : boxes) get(e);

    if (version == 3) {
        if (in.size() < gbAt + 4) { err = path + ": Gen-1/2-Teil fehlt"; return false; }
        uint32_t n = rd32(&in[gbAt]);
        if (in.size() != gbAt + 4 + size_t(n) * GBREC) { err = path + ": Gen-1/2-Teil beschädigt"; return false; }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t* q = &in[gbAt + 4 + i * GBREC];
            uint16_t idx = rd16(q);
            BankEntry* e = idx < PARTY ? &party[idx] : (size_t(idx - PARTY) < boxes.size() ? &boxes[idx - PARTY] : nullptr);
            if (!e || q[6] < 1 || q[6] > 2 || q[7] > 48) continue;
            e->hasGb = true;
            e->gbKey = rd32(q + 2);
            e->gb = gbFromRaw(q[6], q + 8, q[7], q + 56, q + 67);
        }
    }
    return true;
}

} // namespace pw3d
