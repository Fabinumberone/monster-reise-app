#include "bank.hpp"

#include <algorithm>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "bytes.hpp"
#include "items.hpp"
#include "sync.hpp"
#include "world.hpp"

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
    if (prev) e.uid = prev->uid;
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
    if (prev && prev->hasGba) { e.hasGba = true; e.gba = prev->gba; }                    // Gen-3-Stand mitnehmen
    if (prev && prev->hasNds) { e.hasNds = true; e.nds = prev->nds; }                    // Gen-4/5-Stand mitnehmen
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

uint64_t monId(const PK6& pk) { return (uint64_t(pk.ec()) << 32) | pk.pid(); }
uint64_t gbId(uint32_t key, uint16_t family) { return (uint64_t(key) << 16) | family; }

bool contains(const std::vector<uint64_t>& v, uint64_t x) { return std::binary_search(v.begin(), v.end(), x); }
void insertSorted(std::vector<uint64_t>& v, uint64_t x) {
    auto it = std::lower_bound(v.begin(), v.end(), x);
    if (it == v.end() || *it != x) v.insert(it, x);
}

bool sameTrainer(const Trainer6& a, uint16_t tid, uint16_t sid, const std::string& name) {
    return a.tid == tid && a.sid == sid && a.name == name;
}

// Auf den Speicher-Trainer umschreiben und dabei das Schillern erhalten: Schillern hängt an
// TID ^ SID ^ PID-Oberteil ^ PID-Unterteil; das Oberteil wird so angepasst, dass der Wert gleich
// bleibt. Gibt true zurück, wenn sich die PID geändert hat.
bool reownKeepShiny(PK6& pk, const Trainer6& t) {
    if (sameTrainer(t, pk.tid(), pk.sid(), pk.otName())) return false;
    uint32_t pid = pk.pid();
    uint16_t oldTsv = uint16_t(pk.tid() ^ pk.sid()), newTsv = uint16_t(t.tid ^ t.sid);
    uint32_t np = (pid & 0xFFFF) | (uint32_t(uint16_t((pid >> 16) ^ oldTsv ^ newTsv)) << 16);
    wr32(pk.d.data() + 0x18, np);
    pk.setTid(t.tid);
    pk.setSid(t.sid);
    pk.setOtName(t.name);
    pk.updateChecksum();
    return np != pid;
}

} // namespace

BankSlot* Bank::slot(uint32_t lowId, uint8_t media) {
    for (auto& s : slots)
        if (s.lowId == lowId && s.media == media) return &s;
    return nullptr;
}

// Gehört ein Pokémon mit diesem Originaltrainer dem Spieler? (Speicher-Trainer oder Trainer
// eines bekannten Spielstands; bei Gen 1/2 ohne SID.)
// Wunsch des Nutzers (2026-11-07): alle Pokémon gehören dem gemeinsamen Trainer, auch getauschte
// und Event-Pokémon. Die Prüfung bleibt für ältere Aufrufe, liefert aber immer „eigen“.
bool Bank::ownTrainer(uint16_t tid, uint16_t sid, const std::string& name, bool gb) const {
    (void)tid; (void)sid; (void)name; (void)gb;
    return true;
    auto match = [&](const Trainer6& t) { return t.tid == tid && t.name == name && (gb || t.sid == sid); };
    if (match(trainer)) return true;
    for (auto& s : slots)
        if ((s.gen <= 2) == gb && match(s.trainer)) return true;
    return false;
}

void Bank::record(uint32_t lowId, uint8_t media, int gen, const Trainer6& t) {
    BankSlot* s = slot(lowId, media);
    if (!s) { slots.push_back({}); s = &slots.back(); }
    s->lowId = lowId;
    s->media = media;
    s->gen = uint8_t(gen);
    s->trainer = t;
}

void Bank::remember() {
    auto add = [&](const BankEntry& e) {
        if (!e.used) return;
        insertSorted(seen, monId(e.pk));
        if (e.hasGb) insertSorted(seenGb, gbId(e.gbKey, familyOf(e.gb.species)));
        if (e.hasGba) insertSorted(seen, (uint64_t(e.gba.pid()) << 32) | e.gba.pid()); // in Gen 3 geschriebene PID
        if (e.hasNds) insertSorted(seen, (uint64_t(e.nds.pid()) << 32) | e.nds.pid()); // in Gen 4/5 geschriebene PID
    };
    for (auto& e : party) add(e);
    for (auto& e : boxes) add(e);
}

BankEntry* Bank::find(const PK6& pk) {
    return const_cast<BankEntry*>(findIn(party, boxes, pk));
}

BankEntry* Bank::freeBoxSlot(std::vector<BankEntry>& bx) {
    for (auto& e : bx)
        if (!e.used) return &e;
    return nullptr;
}

void Bank::leave() {
    for (auto& e : party) e.inGame = false;
    for (auto& e : boxes) e.inGame = false;
    for (auto& b : items) b.written = 0;
    moneyWritten = 0;
    current.clear();
}

void Bank::sortBoxes() {
    std::vector<BankEntry> all;
    for (auto& e : boxes)
        if (e.used) all.push_back(std::move(e));
    std::stable_sort(all.begin(), all.end(), [](const BankEntry& x, const BankEntry& y) {
        uint16_t sx = x.pk.isEgg() ? 0xFFFF : x.pk.species(), sy = y.pk.isEgg() ? 0xFFFF : y.pk.species();
        if (sx != sy) return sx < sy;
        if (x.pk.form() != y.pk.form()) return x.pk.form() < y.pk.form();
        return x.uid < y.uid;
    });
    boxes.assign(BOXES * SLOTS, BankEntry{});
    for (size_t i = 0; i < all.size() && i < boxes.size(); i++) boxes[i] = std::move(all[i]);
}

void Bank::ensureUids() {
    for (auto& e : party) if (e.used && !e.uid) e.uid = nextUid++;
    for (auto& e : boxes) if (e.used && !e.uid) e.uid = nextUid++;
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
    moneyWritten = money;
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

bool Bank::depart(const SaveFile& sav, std::string& err, bool stay) {
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
        team.back().inGame = stay;
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
            nb[b * SLOTS + s].inGame = stay;
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
    if (stay) for (auto& b : ni) b.written = uint16_t(std::min<uint32_t>(b.count, 0xFFFF));
    for (auto& b : items) addTo(b.item, b.count - std::min<uint32_t>(b.written, b.count)); // unsichtbarer Rest

    remember(); // vorher: wer jetzt fehlt, wurde freigelassen und soll nicht zurückkommen
    party = np;
    boxes = nb;
    items = ni;
    { // Geld: Spielinhalt + was dort nicht hineinpasste bzw. aus anderen Spielständen dazukam
        uint64_t extra = money > moneyWritten ? money - moneyWritten : 0;
        money = uint32_t(std::min<uint64_t>(uint64_t(sav.money()) + extra, 9999999));
        moneyWritten = stay ? sav.money() : 0;
    }
    moneyKnown = true;
    if (!stay) current.clear();
    return true;
}

IntegrateResult Bank::integrate(const SaveFile& sav, uint32_t lowId, uint8_t media, bool forceNew) {
    IntegrateResult r;
    const Trainer6 st = sav.trainer();
    BankSlot* sl = slot(lowId, media);
    // Neu, wenn der Trainer weder der des Speichers (dann hat der Speicher ihn geschrieben oder er
    // stammt von dort) noch der zuletzt gesehene dieses Spielstands ist.
    r.newSave = forceNew || (!sameTrainer(trainer, st.tid, st.sid, st.name) && (!sl || !sameTrainer(sl->trainer, st.tid, st.sid, st.name)));
    if (!sl) { record(lowId, media, sav.generation(), st); sl = slot(lowId, media); }
    if (r.newSave) {
        if (!sl) { slots.push_back({}); sl = &slots.back(); }
        sl->lowId = lowId;
        sl->media = media;
        sl->gen = uint8_t(sav.generation());
        sl->trainer = st;
        addItemsFrom(sav);
        money = std::min<uint32_t>(money + sav.money(), 9999999);
        moneyKnown = true;
    }
    auto add = [&](PK6 pk, bool fromParty, int box, int s) {
        if (!present(pk)) return;
        uint64_t origId = monId(pk);
        reownKeepShiny(pk, trainer); // alle Pokémon gehören dem Speicher-Trainer
        if (BankEntry* e = find(pk)) {
            if (!e->inGame && pk.exp() > e->pk.exp()) { // dort weitergespielt
                BankEntry n = importMon(sav, pk, e);
                *e = n;
                r.updated++;
            }
            return;
        }
        if (contains(seen, monId(pk))) return; // freigelassen oder alte Kopie
        BankEntry e = importMon(sav, pk, nullptr);
        insertSorted(seen, origId);
        BankEntry* dst = nullptr;
        if (fromParty) {
            for (auto& p : party)
                if (!p.used) { dst = &p; break; }
        } else if (box < BOXES && !boxes[size_t(box * SLOTS + s)].used) {
            dst = &boxes[size_t(box * SLOTS + s)];
        }
        if (!dst) dst = freeBoxSlot(boxes);
        if (!dst) { r.noRoom++; return; }
        *dst = e;
        insertSorted(seen, monId(pk));
        r.added++;
    };
    for (int i = 0; i < sav.partyCount() && i < PARTY; i++) add(sav.partySlot(i), true, 0, 0);
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < SLOTS; s++)
            if (!sav.boxSlotEmpty(b, s)) add(sav.boxSlot(b, s), false, b, s);
    return r;
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

    writeMeta(sav);
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

bool Bank::departGB(const SaveGB& sav, const GBContext& ctxIn, std::string& err, bool stay) {
    const char* kind = gbKind(sav.generation());
    if (current != kind) {
        err = std::string("Der Speicher steht in ") + (current.empty() ? "keinem Spiel" : current) + ", nicht in " + kind;
        return false;
    }
    GBContext ctx = ctxIn;
    ctx.gameTid = sav.tid();
    ctx.gameOt = sav.trainerName();
    std::vector<const BankEntry*> matched;
    auto findGb = [&](uint32_t key, uint16_t fam) -> const BankEntry* {
        auto ok = [&](const BankEntry& e) {
            return e.used && e.hasGb && e.gbKey == key && familyOf(e.gb.species) == fam &&
                   std::find(matched.begin(), matched.end(), &e) == matched.end();
        };
        for (auto& e : party) if (ok(e)) return &e;
        for (auto& e : boxes) if (ok(e)) return &e;
        return nullptr;
    };
    auto importGB = [&](const GBMon& m) {
        uint32_t key = gbKey(m);
        const BankEntry* prev = findGb(key, familyOf(m.species));
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
        e.inGame = stay;
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
    if (stay) for (auto& b : ni) b.written = uint16_t(std::min<uint32_t>(b.count, 0xFFFF));
    for (auto& b : items) addTo(b.item, b.count - std::min<uint32_t>(b.written, b.count));

    remember();
    party = np;
    boxes = nb;
    items = ni;
    { // Geld: Spielinhalt + was dort nicht hineinpasste bzw. aus anderen Spielständen dazukam
        uint64_t extra = money > moneyWritten ? money - moneyWritten : 0;
        money = uint32_t(std::min<uint64_t>(uint64_t(sav.money()) + extra, 9999999));
        moneyWritten = stay ? sav.money() : 0;
    }
    moneyKnown = true;
    if (!stay) current.clear();
    return true;
}

IntegrateResult Bank::integrateGB(const SaveGB& sav, const GBContext& ctxIn, uint32_t lowId, uint8_t media, bool forceNew) {
    IntegrateResult r;
    GBContext ctx = ctxIn;
    ctx.gameTid = sav.tid();
    ctx.gameOt = sav.trainerName();
    BankSlot* sl = slot(lowId, media);
    auto same = [&](const Trainer6& t) { return t.tid == ctx.gameTid && t.name == ctx.gameOt; };
    r.newSave = forceNew || (!same(trainer) && (!sl || !same(sl->trainer)));
    if (!sl) { record(lowId, media, sav.generation(), Trainer6{ctx.gameTid, 0, 0, ctx.gameOt}); sl = slot(lowId, media); }
    if (r.newSave) {
        if (!sl) { slots.push_back({}); sl = &slots.back(); }
        sl->lowId = lowId;
        sl->media = media;
        sl->gen = uint8_t(sav.generation());
        sl->trainer = Trainer6{ctx.gameTid, 0, 0, ctx.gameOt};
        for (const ItemStack& s : gbTravelItems(sav)) {
            BankItem* e = nullptr;
            for (auto& b : items) if (b.item == s.item) e = &b;
            if (!e) { items.push_back({s.item, 0, 0}); e = &items.back(); }
            e->count += s.count;
        }
        money = std::min<uint32_t>(money + sav.money(), 9999999);
        moneyKnown = true;
    }
    auto add = [&](const GBMon& m, bool fromParty) {
        uint32_t key = gbKey(m);
        uint16_t fam = familyOf(m.species);
        // Wiedererkennung: über den GB-Schlüssel (so zuletzt in ein GB-Spiel geschrieben) oder über
        // EC/PID, die beim ersten Umwandeln aus dem ursprünglichen Schlüssel entstanden sind.
        PK6 pk = gbToPk7(m, ctx);
        BankEntry* known = find(pk);
        auto match = [&](BankEntry& e) { return e.used && e.hasGb && e.gbKey == key && familyOf(e.gb.species) == fam; };
        for (auto& e : party) if (!known && match(e)) known = &e;
        for (auto& e : boxes) if (!known && match(e)) known = &e;
        if (known) {
            if (!known->inGame && known->hasGb && m.exp > known->gb.exp) { // dort weitergespielt
                mergeGb(known->pk, m, known->gb, ctx);
                known->gb = m;
                known->gbKey = key;
                r.updated++;
            }
            return;
        }
        if (contains(seenGb, gbId(key, fam)) || contains(seen, monId(pk))) return;
        insertSorted(seen, monId(pk));
        reownKeepShiny(pk, trainer);
        BankEntry e;
        e.used = true;
        e.pk = pk;
        e.hasGb = true;
        e.gb = m;
        e.gbKey = key;
        BankEntry* dst = nullptr;
        if (fromParty) for (auto& p : party) if (!p.used) { dst = &p; break; }
        if (!dst) dst = freeBoxSlot(boxes);
        if (!dst) { r.noRoom++; return; }
        *dst = e;
        insertSorted(seenGb, gbId(key, fam));
        insertSorted(seen, monId(e.pk));
        r.added++;
    };
    for (const GBMon& m : sav.party()) add(m, true);
    for (int b = 0; b < sav.boxCount(); b++)
        for (const GBMon& m : sav.box(b)) add(m, false);
    return r;
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

    writeMetaGB(sav);
    current = gbKind(gen);
    return true;
}

// ---------- Gen 3 ----------

namespace {

std::string gbaKind(GBAGame g) { return g == GBAGame::RS ? "GBA-RS" : g == GBAGame::E ? "GBA-E" : "GBA-FRLG"; }

uint16_t gbaTravelModern(uint16_t id) {
    uint16_t m = gen3ItemToModern(id);
    return (m && pouchOf(GameKind::USUM, m) != Pouch::None) ? m : 0;
}

std::vector<ItemStack> gbaTravelItems(const SaveGBA& s) {
    std::vector<ItemStack> out;
    for (auto p : {SaveGBA::Pouch::Items, SaveGBA::Pouch::Balls, SaveGBA::Pouch::Berries, SaveGBA::Pouch::PC})
        for (const GBAItem& i : s.pouch(p)) {
            uint16_t m = gbaTravelModern(i.id);
            if (!m || !i.count) continue;
            bool merged = false;
            for (auto& o : out)
                if (o.item == m) { o.count = uint16_t(o.count + i.count); merged = true; }
            if (!merged) out.push_back({m, i.count});
        }
    return out;
}

bool samePk3(const PK3& a, const PK3& b) { return a.pid() == b.pid() && a.tid() == b.tid() && a.sid() == b.sid(); }

} // namespace

bool Bank::departGBA(const SaveGBA& sav, const GBAContext& ctxIn, std::string& err, bool stay) {
    std::string kind = gbaKind(sav.game());
    if (current != kind) {
        err = "Der Speicher steht in " + (current.empty() ? std::string("keinem Spiel") : current) + ", nicht in " + kind;
        return false;
    }
    GBAContext ctx = ctxIn;
    ctx.gameTid = sav.tid();
    ctx.gameSid = sav.sid();
    ctx.gameOt = sav.trainerName();
    std::vector<const BankEntry*> matched;
    auto findFor = [&](const PK3& m) -> const BankEntry* {
        auto free = [&](const BankEntry& e) { return std::find(matched.begin(), matched.end(), &e) == matched.end(); };
        for (auto& e : party) if (e.used && e.hasGba && samePk3(e.gba, m) && free(e)) return &e;
        for (auto& e : boxes) if (e.used && e.hasGba && samePk3(e.gba, m) && free(e)) return &e;
        for (auto& e : party) if (e.used && e.pk.ec() == m.pid() && e.pk.pid() == m.pid() && free(e)) return &e;
        for (auto& e : boxes) if (e.used && e.pk.ec() == m.pid() && e.pk.pid() == m.pid() && free(e)) return &e;
        return nullptr;
    };
    auto importG = [&](const PK3& m) {
        const BankEntry* prev = findFor(m);
        BankEntry e;
        if (prev) {
            matched.push_back(prev);
            e = *prev;
            if (prev->hasGba) mergeGen3(e.pk, m, prev->gba, ctx);
            else e.pk = gen3ToPk7(m, ctx);
        } else {
            e.pk = gen3ToPk7(m, ctx);
        }
        e.used = true;
        e.hasGba = true;
        e.gba = m;
        e.inGame = stay;
        return e;
    };

    std::array<BankEntry, PARTY> np{};
    std::vector<BankEntry> nb(BOXES * SLOTS);
    std::vector<BankEntry> team;
    for (const PK3& m : sav.party())
        if (m.present()) team.push_back(importG(m));
    std::vector<const BankEntry*> overflow;
    for (int i = 0; i < PARTY; i++) { // nicht angezeigte Team-Pokémon an ihren alten Platz
        const auto& e = party[size_t(i)];
        if (!e.used || e.inGame) continue;
        if (int(team.size()) < PARTY) team.insert(team.begin() + std::min<size_t>(size_t(i), team.size()), e);
        else overflow.push_back(&e);
    }
    for (size_t i = 0; i < team.size(); i++) np[i] = team[i];
    for (int b = 0; b < SaveGBA::BOXES; b++)
        for (int s = 0; s < SLOTS; s++) {
            PK3 m = sav.boxSlot(b, s);
            if (m.present()) nb[size_t(b * SLOTS + s)] = importG(m);
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

    std::vector<BankItem> ni;
    auto addTo = [&](uint16_t item, uint32_t n) {
        if (!n) return;
        for (auto& b : ni) if (b.item == item) { b.count += n; return; }
        ni.push_back({item, 0, n});
    };
    for (const ItemStack& s : gbaTravelItems(sav)) addTo(s.item, s.count);
    if (stay) for (auto& b : ni) b.written = uint16_t(std::min<uint32_t>(b.count, 0xFFFF));
    for (auto& b : items) addTo(b.item, b.count - std::min<uint32_t>(b.written, b.count));

    remember();
    party = np;
    boxes = nb;
    items = ni;
    {
        uint64_t extra = money > moneyWritten ? money - moneyWritten : 0;
        money = uint32_t(std::min<uint64_t>(uint64_t(sav.money()) + extra, 9999999));
        moneyWritten = stay ? sav.money() : 0;
    }
    moneyKnown = true;
    if (!stay) current.clear();
    return true;
}

bool Bank::arriveGBA(SaveGBA& sav, const GBAContext& ctx, int& hidden, std::string& err) {
    if (!current.empty()) { err = "Erst aus " + current + " abreisen"; return false; }
    hidden = 0;
    for (auto& e : party) e.inGame = false;
    for (auto& e : boxes) e.inGame = false;
    auto exportG = [&](BankEntry& e) {
        PK3 m = pk7ToGen3(e.pk, e.hasGba ? &e.gba : nullptr, ctx);
        e.gba = m;
        e.hasGba = true;
        e.inGame = true;
        if (!m.isEgg()) sav.registerDex(m.species());
        return m;
    };
    std::vector<PK3> team;
    for (auto& e : party)
        if (e.used && fitsGen3(e.pk)) team.push_back(exportG(e));
    if (team.empty()) { err = "Kein Pokémon im Team kann in diesem Spiel existieren"; return false; }
    sav.setParty(team);
    for (int b = 0; b < SaveGBA::BOXES; b++)
        for (int s = 0; s < SLOTS; s++) {
            BankEntry& e = boxes[size_t(b * SLOTS + s)];
            if (e.used && fitsGen3(e.pk)) {
                PK3 m = exportG(e);
                sav.setBoxSlot(b, s, &m);
            } else {
                sav.setBoxSlot(b, s, nullptr);
            }
        }
    for (auto& e : party) hidden += e.used && !e.inGame;
    for (auto& e : boxes) hidden += e.used && !e.inGame;

    writeMetaGBA(sav);
    current = gbaKind(sav.game());
    return true;
}

IntegrateResult Bank::integrateGBA(const SaveGBA& sav, const GBAContext& ctxIn, uint32_t lowId, uint8_t media, bool forceNew) {
    IntegrateResult r;
    GBAContext ctx = ctxIn;
    ctx.gameTid = sav.tid();
    ctx.gameSid = sav.sid();
    ctx.gameOt = sav.trainerName();
    Trainer6 st{ctx.gameTid, ctx.gameSid, sav.trainerGender(), ctx.gameOt};
    BankSlot* sl = slot(lowId, media);
    bool isBank = st.tid == trainer.tid && st.sid == trainer.sid && st.name == gen3TrainerName(trainer.name);
    r.newSave = forceNew || (!isBank && (!sl || !sameTrainer(sl->trainer, st.tid, st.sid, st.name)));
    if (!sl) { record(lowId, media, 3, st); sl = slot(lowId, media); }
    if (r.newSave) {
        sl->trainer = st;
        for (const ItemStack& s : gbaTravelItems(sav)) {
            BankItem* e = nullptr;
            for (auto& b : items) if (b.item == s.item) e = &b;
            if (!e) { items.push_back({s.item, 0, 0}); e = &items.back(); }
            e->count += s.count;
        }
        money = std::min<uint32_t>(money + sav.money(), 9999999);
        moneyKnown = true;
    }
    auto add = [&](const PK3& m, bool fromParty, int box, int s) {
        if (!m.present()) return;
        BankEntry* known = nullptr;
        for (auto& e : party) if (!known && e.used && e.hasGba && samePk3(e.gba, m)) known = &e;
        for (auto& e : boxes) if (!known && e.used && e.hasGba && samePk3(e.gba, m)) known = &e;
        PK6 pk = gen3ToPk7(m, ctx);
        if (!known) known = find(pk);
        if (known) {
            if (!known->inGame && known->hasGba && m.exp() > known->gba.exp()) { // dort weitergespielt
                mergeGen3(known->pk, m, known->gba, ctx);
                known->gba = m;
                r.updated++;
            }
            return;
        }
        if (contains(seen, monId(pk))) return; // freigelassen oder alte Kopie
        insertSorted(seen, monId(pk));
        bool pidChanged = reownKeepShiny(pk, trainer);
        BankEntry e;
        e.used = true;
        e.pk = pk;
        e.hasGba = !pidChanged; // mit neuer PID passt der Gen-3-Stand nicht mehr
        e.gba = m;
        BankEntry* dst = nullptr;
        if (fromParty) {
            for (auto& p : party) if (!p.used) { dst = &p; break; }
        } else if (!boxes[size_t(box * SLOTS + s)].used) {
            dst = &boxes[size_t(box * SLOTS + s)];
        }
        if (!dst) dst = freeBoxSlot(boxes);
        if (!dst) { r.noRoom++; return; }
        *dst = e;
        insertSorted(seen, monId(pk));
        r.added++;
    };
    for (const PK3& m : sav.party()) add(m, true, 0, 0);
    for (int b = 0; b < SaveGBA::BOXES; b++)
        for (int s = 0; s < SLOTS; s++) add(sav.boxSlot(b, s), false, b, s);
    return r;
}

// ---------- Gen 4/5 ----------

namespace {

std::string ndsKind(NDSGame g) {
    switch (g) {
    case NDSGame::DP: return "NDS-DP";
    case NDSGame::Pt: return "NDS-Pt";
    case NDSGame::HGSS: return "NDS-HGSS";
    case NDSGame::BW: return "NDS-BW";
    default: return "NDS-B2W2";
    }
}

std::vector<SaveNDS::Pouch> ndsTravelPouches(const SaveNDS& s) {
    using P = SaveNDS::Pouch;
    if (s.generation() == 5) return {P::Items, P::Medicine, P::Berries};
    return {P::Items, P::Medicine, P::Berries, P::Balls, P::Battle};
}

bool ndsItemTravels(uint16_t item) { return item && pouchOf(GameKind::USUM, item) != Pouch::None; }

std::vector<ItemStack> ndsTravelItems(const SaveNDS& s) {
    std::vector<ItemStack> out;
    for (auto p : ndsTravelPouches(s))
        for (const NDSItem& i : s.pouch(p)) {
            if (!ndsItemTravels(i.id) || !i.count) continue;
            bool merged = false;
            for (auto& o : out)
                if (o.item == i.id) { o.count = uint16_t(std::min<uint32_t>(o.count + i.count, 0xFFFF)); merged = true; }
            if (!merged) out.push_back({i.id, i.count});
        }
    return out;
}

bool samePk45(const PK45& a, const PK45& b) { return a.pid() == b.pid() && a.tid() == b.tid() && a.sid() == b.sid(); }

} // namespace

bool Bank::departNDS(const SaveNDS& sav, const NDSContext& ctxIn, std::string& err, bool stay) {
    std::string kind = ndsKind(sav.game());
    if (current != kind) {
        err = "Der Speicher steht in " + (current.empty() ? std::string("keinem Spiel") : current) + ", nicht in " + kind;
        return false;
    }
    NDSContext ctx = ctxIn;
    ctx.game = sav.game();
    ctx.gameTid = sav.tid();
    ctx.gameSid = sav.sid();
    ctx.gameOt = sav.trainerName();
    std::vector<const BankEntry*> matched;
    auto findFor = [&](const PK45& m) -> const BankEntry* {
        auto free = [&](const BankEntry& e) { return std::find(matched.begin(), matched.end(), &e) == matched.end(); };
        for (auto& e : party) if (e.used && e.hasNds && samePk45(e.nds, m) && free(e)) return &e;
        for (auto& e : boxes) if (e.used && e.hasNds && samePk45(e.nds, m) && free(e)) return &e;
        for (auto& e : party) if (e.used && e.pk.ec() == m.pid() && e.pk.pid() == m.pid() && free(e)) return &e;
        for (auto& e : boxes) if (e.used && e.pk.ec() == m.pid() && e.pk.pid() == m.pid() && free(e)) return &e;
        return nullptr;
    };
    auto importN = [&](const PK45& m) {
        const BankEntry* prev = findFor(m);
        BankEntry e;
        if (prev) {
            matched.push_back(prev);
            e = *prev;
            if (prev->hasNds && prev->nds.gen == m.gen) mergeGen45(e.pk, m, prev->nds, ctx);
            else e.pk = gen45ToPk7(m, ctx);
        } else {
            e.pk = gen45ToPk7(m, ctx);
        }
        e.used = true;
        e.hasNds = true;
        e.nds = m;
        e.inGame = stay;
        return e;
    };

    std::array<BankEntry, PARTY> np{};
    std::vector<BankEntry> nb(BOXES * SLOTS);
    std::vector<BankEntry> team;
    for (const PK45& m : sav.party())
        if (m.present()) team.push_back(importN(m));
    std::vector<const BankEntry*> overflow;
    for (int i = 0; i < PARTY; i++) {
        const auto& e = party[size_t(i)];
        if (!e.used || e.inGame) continue;
        if (int(team.size()) < PARTY) team.insert(team.begin() + std::min<size_t>(size_t(i), team.size()), e);
        else overflow.push_back(&e);
    }
    for (size_t i = 0; i < team.size(); i++) np[i] = team[i];
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < SLOTS; s++) {
            PK45 m = sav.boxSlot(b, s);
            if (m.present()) nb[size_t(b * SLOTS + s)] = importN(m);
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

    std::vector<BankItem> ni;
    auto addTo = [&](uint16_t item, uint32_t n) {
        if (!n) return;
        for (auto& b : ni) if (b.item == item) { b.count += n; return; }
        ni.push_back({item, 0, n});
    };
    for (const ItemStack& s : ndsTravelItems(sav)) addTo(s.item, s.count);
    if (stay) for (auto& b : ni) b.written = uint16_t(std::min<uint32_t>(b.count, 0xFFFF));
    for (auto& b : items) addTo(b.item, b.count - std::min<uint32_t>(b.written, b.count));

    remember();
    party = np;
    boxes = nb;
    items = ni;
    {
        uint64_t extra = money > moneyWritten ? money - moneyWritten : 0;
        money = uint32_t(std::min<uint64_t>(uint64_t(sav.money()) + extra, 9999999));
        moneyWritten = stay ? sav.money() : 0;
    }
    moneyKnown = true;
    if (!stay) current.clear();
    return true;
}

bool Bank::arriveNDS(SaveNDS& sav, const NDSContext& ctxIn, int& hidden, std::string& err) {
    if (!current.empty()) { err = "Erst aus " + current + " abreisen"; return false; }
    NDSContext ctx = ctxIn;
    ctx.game = sav.game();
    hidden = 0;
    for (auto& e : party) e.inGame = false;
    for (auto& e : boxes) e.inGame = false;
    auto exportN = [&](BankEntry& e) {
        PK45 m = pk7ToGen45(e.pk, e.hasNds ? &e.nds : nullptr, ctx);
        e.nds = m;
        e.hasNds = true;
        e.inGame = true;
        sav.registerDex(m);
        return m;
    };
    std::vector<PK45> team;
    for (auto& e : party)
        if (e.used && fitsGen45(e.pk, sav.game())) team.push_back(exportN(e));
    if (team.empty()) { err = "Kein Pokémon im Team kann in diesem Spiel existieren"; return false; }
    sav.setParty(team);
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < SLOTS; s++) {
            BankEntry& e = boxes[size_t(b * SLOTS + s)];
            if (e.used && fitsGen45(e.pk, sav.game())) {
                PK45 m = exportN(e);
                sav.setBoxSlot(b, s, &m);
            } else {
                sav.setBoxSlot(b, s, nullptr);
            }
        }
    for (auto& e : party) hidden += e.used && !e.inGame;
    for (auto& e : boxes) hidden += e.used && !e.inGame;

    writeMetaNDS(sav);
    current = ndsKind(sav.game());
    return true;
}

IntegrateResult Bank::integrateNDS(const SaveNDS& sav, const NDSContext& ctxIn, uint32_t lowId, uint8_t media, bool forceNew) {
    IntegrateResult r;
    NDSContext ctx = ctxIn;
    ctx.game = sav.game();
    ctx.gameTid = sav.tid();
    ctx.gameSid = sav.sid();
    ctx.gameOt = sav.trainerName();
    Trainer6 st{ctx.gameTid, ctx.gameSid, sav.trainerGender(), ctx.gameOt};
    BankSlot* sl = slot(lowId, media);
    bool isBank = st.tid == trainer.tid && st.sid == trainer.sid && st.name == ndsTrainerName(trainer.name);
    r.newSave = forceNew || (!isBank && (!sl || !sameTrainer(sl->trainer, st.tid, st.sid, st.name)));
    if (!sl) { record(lowId, media, sav.generation(), st); sl = slot(lowId, media); }
    if (r.newSave) {
        sl->trainer = st;
        for (const ItemStack& s : ndsTravelItems(sav)) {
            BankItem* e = nullptr;
            for (auto& b : items) if (b.item == s.item) e = &b;
            if (!e) { items.push_back({s.item, 0, 0}); e = &items.back(); }
            e->count += s.count;
        }
        money = std::min<uint32_t>(money + sav.money(), 9999999);
        moneyKnown = true;
    }
    auto add = [&](const PK45& m, bool fromParty, int box, int s) {
        if (!m.present()) return;
        BankEntry* known = nullptr;
        for (auto& e : party) if (!known && e.used && e.hasNds && samePk45(e.nds, m)) known = &e;
        for (auto& e : boxes) if (!known && e.used && e.hasNds && samePk45(e.nds, m)) known = &e;
        PK6 pk = gen45ToPk7(m, ctx);
        if (!known) known = find(pk);
        if (known) {
            if (!known->inGame && known->hasNds && known->nds.gen == m.gen && m.exp() > known->nds.exp()) {
                mergeGen45(known->pk, m, known->nds, ctx);
                known->nds = m;
                r.updated++;
            }
            return;
        }
        if (contains(seen, monId(pk))) return;
        insertSorted(seen, monId(pk));
        bool pidChanged = reownKeepShiny(pk, trainer);
        BankEntry e;
        e.used = true;
        e.pk = pk;
        e.hasNds = !pidChanged; // mit neuer PID passt der Gen-4/5-Stand nicht mehr
        e.nds = m;
        BankEntry* dst = nullptr;
        if (fromParty) {
            for (auto& p : party) if (!p.used) { dst = &p; break; }
        } else if (!boxes[size_t(box * SLOTS + s)].used) {
            dst = &boxes[size_t(box * SLOTS + s)];
        }
        if (!dst) dst = freeBoxSlot(boxes);
        if (!dst) { r.noRoom++; return; }
        *dst = e;
        insertSorted(seen, monId(pk));
        r.added++;
    };
    for (const PK45& m : sav.party()) add(m, true, 0, 0);
    for (int b = 0; b < sav.boxCount(); b++)
        for (int s = 0; s < SLOTS; s++) add(sav.boxSlot(b, s), false, b, s);
    return r;
}

// ---------- Items, Geld, Trainer beim Schreiben ----------

void Bank::writeMeta(SaveFile& sav) {
    GameKind k = sav.kind();
    (void)k;
    // Items und Geld
    std::vector<ItemStack> stacks;
    for (auto& b : items) stacks.push_back({b.item, uint16_t(std::min<uint32_t>(b.count, 0xFFFF))});
    std::vector<uint16_t> written;
    writeTravelItems(sav, stacks, written);
    for (size_t i = 0; i < items.size(); i++) items[i].written = written[i];
    if (moneyKnown) { moneyWritten = std::min<uint32_t>(money, 9999999); sav.setMoney(moneyWritten); }

    Trainer6 t = sav.trainer();
    t.tid = trainer.tid;
    t.sid = trainer.sid;
    t.name = trainer.name;
    // Spielergeschlecht bleibt das des Spiels (sonst wechselt die Spielfigur)
    sav.setTrainer(t);
}

void Bank::writeMetaGB(SaveGB& sav) {
    const int gen = sav.generation();
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
    if (moneyKnown) { moneyWritten = std::min<uint32_t>(money, 999999); sav.setMoney(moneyWritten); }
    sav.setTrainer(trainer.name, trainer.tid);
}

void Bank::writeMetaGBA(SaveGBA& sav) {
    // Items: nicht reisende Items bleiben, dann reisende nach Rangfolge (Bälle, Medizin, Schutz, Rest)
    using P = SaveGBA::Pouch;
    auto keepOnly = [&](P p) {
        std::vector<GBAItem> out;
        for (const GBAItem& i : sav.pouch(p)) if (!gbaTravelModern(i.id)) out.push_back(i);
        return out;
    };
    std::vector<GBAItem> bag = keepOnly(P::Items), balls = keepOnly(P::Balls), berries = keepOnly(P::Berries), pc = keepOnly(P::PC);
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return itemPriority(items[a].item) < itemPriority(items[b].item); });
    for (auto& b : items) b.written = 0;
    for (size_t idx : order) {
        BankItem& it = items[idx];
        uint16_t g = modernItemToGen3(it.item);
        if (!g || !it.count || !gbaTravelModern(g)) continue;
        bool isBall = g >= 1 && g <= 12, isBerry = g >= 133 && g <= 175;
        P p = isBall ? P::Balls : isBerry ? P::Berries : P::Items;
        std::vector<GBAItem>* dst = p == P::Balls ? &balls : p == P::Berries ? &berries : &bag;
        if (int(dst->size()) >= sav.pouchCapacity(p)) { p = P::PC; dst = &pc; }
        if (int(dst->size()) >= sav.pouchCapacity(p)) continue;
        uint16_t n = uint16_t(std::min<uint32_t>(it.count, sav.maxStack(p)));
        dst->push_back({g, n});
        it.written = n;
    }
    sav.setPouch(P::Items, bag);
    sav.setPouch(P::Balls, balls);
    sav.setPouch(P::Berries, berries);
    sav.setPouch(P::PC, pc);
    if (moneyKnown) { moneyWritten = std::min<uint32_t>(money, 999999); sav.setMoney(moneyWritten); }
    sav.setTrainer(gen3TrainerName(trainer.name), trainer.tid, trainer.sid, sav.trainerGender()); // Spielfigur bleibt
}

void Bank::writeMetaNDS(SaveNDS& sav) {
    // Items: nicht reisende Items bleiben, dann reisende nach Rangfolge in ihre Tasche
    using P = SaveNDS::Pouch;
    std::vector<std::vector<NDSItem>> lists(8);
    for (auto p : ndsTravelPouches(sav))
        for (const NDSItem& i : sav.pouch(p))
            if (!ndsItemTravels(i.id)) lists[size_t(p)].push_back(i);
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return itemPriority(items[a].item) < itemPriority(items[b].item); });
    for (auto& b : items) b.written = 0;
    for (size_t idx : order) {
        BankItem& it = items[idx];
        int p = ndsTravelPouch(sav.game(), it.item);
        if (p < 0 || !it.count) continue;
        auto& dst = lists[size_t(p)];
        if (int(dst.size()) >= sav.pouchCapacity(P(p))) continue;
        uint16_t n = uint16_t(std::min<uint32_t>(it.count, 999));
        dst.push_back({it.item, n});
        it.written = n;
    }
    for (auto p : ndsTravelPouches(sav)) sav.setPouch(p, lists[size_t(p)]);
    if (moneyKnown) { moneyWritten = std::min<uint32_t>(money, 999999); sav.setMoney(moneyWritten); }
    sav.setTrainer(ndsTrainerName(trainer.name), trainer.tid, trainer.sid, sav.trainerGender()); // Spielfigur bleibt
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
// Version 8 hängt an: u32 nächste Kennung, je Eintrag (Team, Boxen) u32 Kennung, u32 Anzahl Spielstände,
// je Spielstand: u32 Title-ID, u8 Medium, u8 geschrieben, u8 Herkunftsspiel, u64 Fingerabdruck, u32 Geld, u32 Anzahl Items
// (je u16 Item, u16 Anzahl), u32 Anzahl Pokémon (je u32 Kennung, u8 Flags, u16 Länge, Datensatz).
// Version 7 hängt an: u32 Anzahl Gen-4/5-Stände, je u16 Eintrag, u8 Generation, 236 Byte PK4/PK5 (entschlüsselt).
// Version 6 hängt an: u32 Anzahl Gen-3-Stände, je u16 Eintrag + 100 Byte PK3 (entschlüsselt, feste Blockfolge).
// Version 5: wie 4, aber 100 statt 32 Boxen.
// Version 4 hängt zusätzlich an: u32 aktueller Spielstand (untere Title-ID), u8 Medium, 3 frei,
// u32 ins aktuelle Spiel geschriebenes Geld,
// u32 Anzahl gesehener Pokémon, je u64 (EC·2^32 + PID), u32 Anzahl gesehener Gen-1/2-Pokémon,
// je u64 (Schlüssel·2^16 + Familie), u32 Anzahl bekannter Spielstände, je 40 Byte: u32 Title-ID,
// u8 Medium, u8 Generation, u16 TID, u16 SID, 2 frei, 28 Byte Trainername (UTF-8, mit Nullen).

namespace {
constexpr char MAGIC[8] = {'P', 'W', '3', 'D', 'B', 'A', 'N', 'K'};
constexpr size_t ENTRY = 4 + 2 * PK6_PARTY_SIZE;
constexpr size_t HEADER = 8 + 4 + 8 + 26 + 16;
constexpr size_t GBREC = 2 + 4 + 1 + 1 + 48 + 11 + 11;
} // namespace

std::vector<uint8_t> Bank::serialize() const {
    std::vector<uint8_t> out(HEADER + (PARTY + BOXES * SLOTS) * ENTRY, 0);
    std::memcpy(out.data(), MAGIC, 8);
    wr32(&out[8], 8);
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

    // Version 4: aktueller Spielstand, gesehene Pokémon, bekannte Spielstände
    Bank tmpSeen;
    tmpSeen.species = species;
    tmpSeen.seen = seen;
    tmpSeen.seenGb = seenGb;
    tmpSeen.party = party;
    tmpSeen.boxes = boxes;
    tmpSeen.remember();
    tail = out.size();
    out.resize(tail + 12 + 4 + tmpSeen.seen.size() * 8 + 4 + tmpSeen.seenGb.size() * 8 + 4 + slots.size() * 40, 0);
    uint8_t* q = &out[tail];
    wr32(q, currentLow);
    q[4] = currentMedia;
    wr32(q + 8, moneyWritten);
    q += 12;
    auto putList = [&](const std::vector<uint64_t>& v) {
        wr32(q, uint32_t(v.size()));
        q += 4;
        for (uint64_t x : v) { wr32(q, uint32_t(x)); wr32(q + 4, uint32_t(x >> 32)); q += 8; }
    };
    putList(tmpSeen.seen);
    putList(tmpSeen.seenGb);
    wr32(q, uint32_t(slots.size()));
    q += 4;
    for (const BankSlot& s : slots) {
        wr32(q, s.lowId);
        q[4] = s.media;
        q[5] = s.gen;
        wr16(q + 6, s.trainer.tid);
        wr16(q + 8, s.trainer.sid);
        std::memcpy(q + 12, s.trainer.name.data(), std::min<size_t>(s.trainer.name.size(), 27));
        q += 40;
    }
    // Version 6: Gen-3-Stände
    std::vector<std::pair<uint16_t, const BankEntry*>> gbas;
    for (int i = 0; i < PARTY; i++)
        if (party[size_t(i)].used && party[size_t(i)].hasGba) gbas.push_back({uint16_t(i), &party[size_t(i)]});
    for (size_t i = 0; i < boxes.size(); i++)
        if (boxes[i].used && boxes[i].hasGba) gbas.push_back({uint16_t(PARTY + i), &boxes[i]});
    tail = out.size();
    out.resize(tail + 4 + gbas.size() * 102, 0);
    wr32(&out[tail], uint32_t(gbas.size()));
    for (size_t i = 0; i < gbas.size(); i++) {
        uint8_t* p = &out[tail + 4 + i * 102];
        wr16(p, gbas[i].first);
        std::memcpy(p + 2, gbas[i].second->gba.d.data(), 100);
    }
    // Version 7: Gen-4/5-Stände
    std::vector<std::pair<uint16_t, const BankEntry*>> ndss;
    for (int i = 0; i < PARTY; i++)
        if (party[size_t(i)].used && party[size_t(i)].hasNds) ndss.push_back({uint16_t(i), &party[size_t(i)]});
    for (size_t i = 0; i < boxes.size(); i++)
        if (boxes[i].used && boxes[i].hasNds) ndss.push_back({uint16_t(PARTY + i), &boxes[i]});
    tail = out.size();
    out.resize(tail + 4 + ndss.size() * 239, 0);
    wr32(&out[tail], uint32_t(ndss.size()));
    for (size_t i = 0; i < ndss.size(); i++) {
        uint8_t* p = &out[tail + 4 + i * 239];
        wr16(p, ndss[i].first);
        p[2] = uint8_t(ndss[i].second->nds.gen);
        std::memcpy(p + 3, ndss[i].second->nds.d.data(), 236);
    }
    // Version 8: Kennungen und je Spielstand Geschriebenes
    std::vector<uint8_t> v8;
    auto put32 = [&](uint32_t v) { size_t o = v8.size(); v8.resize(o + 4); wr32(&v8[o], v); };
    auto put16 = [&](uint16_t v) { size_t o = v8.size(); v8.resize(o + 2); wr16(&v8[o], v); };
    put32(nextUid);
    for (auto& e : party) put32(e.used ? e.uid : 0);
    for (auto& e : boxes) put32(e.used ? e.uid : 0);
    put32(uint32_t(slots.size()));
    for (const BankSlot& s : slots) {
        put32(s.lowId);
        v8.push_back(s.media);
        v8.push_back(s.synced ? 1 : 0);
        v8.push_back(s.version);
        put32(uint32_t(s.fingerprint));
        put32(uint32_t(s.fingerprint >> 32));
        put32(s.money);
        put32(uint32_t(s.items.size()));
        for (auto& it : s.items) { put16(it.first); put16(it.second); }
        put32(uint32_t(s.mons.size()));
        for (const WrittenMon& m : s.mons) {
            put32(m.uid);
            v8.push_back(m.flags);
            put16(uint16_t(m.rec.size()));
            v8.insert(v8.end(), m.rec.begin(), m.rec.end());
        }
    }
    out.insert(out.end(), v8.begin(), v8.end());
    return out;
}

bool Bank::save(const std::string& path, std::string& err) const {
    std::vector<uint8_t> out = serialize();
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
    uint32_t version = in.size() >= 12 ? rd32(&in[8]) : 0;
    const int fileBoxes = version >= 5 ? BOXES : OLD_BOXES;
    const size_t base = HEADER + (PARTY + size_t(fileBoxes) * SLOTS) * ENTRY;
    fileVersion = version;
    if (in.size() < base || std::memcmp(in.data(), MAGIC, 8) != 0 || version < 1 || version > 8 ||
        (version == 1 && in.size() != base) || (version >= 2 && in.size() < base + 12)) {
        err = path + " ist kein gemeinsamer Speicher (Version 1 bis 8)";
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
    for (int i = 0; i < fileBoxes * SLOTS; i++) get(boxes[size_t(i)]);

    currentLow = 0;
    currentMedia = 0;
    moneyWritten = current.empty() ? 0 : money; // ältere Speicher: alles Geld steht im aktuellen Spiel
    nextUid = 1;
    slots.clear();
    seen.clear();
    seenGb.clear();
    if (version >= 3) {
        if (in.size() < gbAt + 4) { err = path + ": Gen-1/2-Teil fehlt"; return false; }
        uint32_t n = rd32(&in[gbAt]);
        size_t end = gbAt + 4 + size_t(n) * GBREC;
        if (version == 3 ? in.size() != end : in.size() < end) { err = path + ": Gen-1/2-Teil beschädigt"; return false; }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t* q = &in[gbAt + 4 + i * GBREC];
            uint16_t idx = rd16(q);
            BankEntry* e = idx < PARTY ? &party[idx] : (size_t(idx - PARTY) < boxes.size() ? &boxes[idx - PARTY] : nullptr);
            if (!e || q[6] < 1 || q[6] > 2 || q[7] > 48) continue;
            e->hasGb = true;
            e->gbKey = rd32(q + 2);
            e->gb = gbFromRaw(q[6], q + 8, q[7], q + 56, q + 67);
        }
        if (version >= 4) { // Version 5: wie 4, aber 100 Boxen
            const uint8_t* q = &in[end];
            const uint8_t* qe = in.data() + in.size();
            auto bad = [&](size_t need) { return size_t(qe - q) < need; };
            if (bad(12)) { err = path + ": Teil 4 beschädigt"; return false; }
            currentLow = rd32(q);
            currentMedia = q[4];
            moneyWritten = rd32(q + 8);
            q += 12;
            auto getList = [&](std::vector<uint64_t>& v) {
                if (bad(4)) return false;
                uint32_t k = rd32(q);
                q += 4;
                if (bad(size_t(k) * 8)) return false;
                for (uint32_t i = 0; i < k; i++, q += 8) v.push_back(uint64_t(rd32(q)) | (uint64_t(rd32(q + 4)) << 32));
                std::sort(v.begin(), v.end());
                return true;
            };
            if (!getList(seen) || !getList(seenGb) || bad(4)) { err = path + ": Teil 4 beschädigt"; return false; }
            uint32_t k = rd32(q);
            q += 4;
            if (bad(size_t(k) * 40)) { err = path + ": Teil 4 beschädigt"; return false; }
            for (uint32_t i = 0; i < k; i++, q += 40) {
                BankSlot s;
                s.lowId = rd32(q);
                s.media = q[4];
                s.gen = q[5];
                s.trainer.tid = rd16(q + 6);
                s.trainer.sid = rd16(q + 8);
                s.trainer.name.assign(reinterpret_cast<const char*>(q + 12), strnlen(reinterpret_cast<const char*>(q + 12), 28));
                slots.push_back(s);
            }
            if (version >= 6) { // Gen-3-Stände
                if (bad(4)) { err = path + ": Teil 6 beschädigt"; return false; }
                uint32_t n6 = rd32(q);
                q += 4;
                if (bad(size_t(n6) * 102)) { err = path + ": Teil 6 beschädigt"; return false; }
                for (uint32_t i = 0; i < n6; i++, q += 102) {
                    uint16_t idx = rd16(q);
                    BankEntry* e = idx < PARTY ? &party[idx] : (size_t(idx - PARTY) < boxes.size() ? &boxes[idx - PARTY] : nullptr);
                    if (!e) continue;
                    e->hasGba = true;
                    std::memcpy(e->gba.d.data(), q + 2, 100);
                    e->gba.party = true;
                }
            }
            if (version >= 7) { // Gen-4/5-Stände
                if (bad(4)) { err = path + ": Teil 7 beschädigt"; return false; }
                uint32_t n7 = rd32(q);
                q += 4;
                if (bad(size_t(n7) * 239)) { err = path + ": Teil 7 beschädigt"; return false; }
                for (uint32_t i = 0; i < n7; i++, q += 239) {
                    uint16_t idx = rd16(q);
                    BankEntry* e = idx < PARTY ? &party[idx] : (size_t(idx - PARTY) < boxes.size() ? &boxes[idx - PARTY] : nullptr);
                    if (!e || (q[2] != 4 && q[2] != 5)) continue;
                    e->hasNds = true;
                    e->nds.gen = q[2];
                    e->nds.party = true;
                    std::memcpy(e->nds.d.data(), q + 3, 236);
                }
            }
            if (version >= 8) { // Kennungen, je Spielstand Geschriebenes
                const size_t n = PARTY + boxes.size();
                if (bad(4 + n * 4 + 4)) { err = path + ": Teil 8 beschädigt"; return false; }
                nextUid = rd32(q);
                q += 4;
                for (size_t i = 0; i < n; i++, q += 4) (i < PARTY ? party[i] : boxes[i - PARTY]).uid = rd32(q);
                uint32_t ns = rd32(q);
                q += 4;
                for (uint32_t i = 0; i < ns; i++) {
                    if (bad(23)) { err = path + ": Teil 8 beschädigt"; return false; }
                    uint32_t low = rd32(q);
                    uint8_t media = q[4];
                    BankSlot* s = slot(low, media);
                    BankSlot dummy;
                    if (!s) s = &dummy;
                    s->synced = q[5] != 0;
                    s->version = q[6];
                    s->fingerprint = uint64_t(rd32(q + 7)) | (uint64_t(rd32(q + 11)) << 32);
                    s->money = rd32(q + 15);
                    uint32_t ni = rd32(q + 19);
                    q += 23;
                    if (bad(size_t(ni) * 4 + 4)) { err = path + ": Teil 8 beschädigt"; return false; }
                    s->items.clear();
                    for (uint32_t k = 0; k < ni; k++, q += 4) s->items.push_back({rd16(q), rd16(q + 2)});
                    uint32_t nm = rd32(q);
                    q += 4;
                    s->mons.clear();
                    for (uint32_t k = 0; k < nm; k++) {
                        if (bad(7)) { err = path + ": Teil 8 beschädigt"; return false; }
                        WrittenMon m;
                        m.uid = rd32(q);
                        m.flags = q[4];
                        uint16_t len = rd16(q + 5);
                        q += 7;
                        if (bad(len)) { err = path + ": Teil 8 beschädigt"; return false; }
                        m.rec.assign(q, q + len);
                        q += len;
                        s->mons.push_back(std::move(m));
                    }
                }
            }
        }
    }
    return true;
}


// ---------- Alle Spielstände aktuell (world.hpp) ----------

namespace {

enum class Fmt { G67, GB, GBA, NDS };

uint64_t fnv64(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// Datensätze ohne Teamwerte (die stehen bei Box-Pokémon nicht im Spielstand), damit ein
// unverändertes Pokémon in Team und Box gleich aussieht.
std::vector<uint8_t> gbRec(const GBMon& m) {
    std::vector<uint8_t> r(2 + 48 + 22, 0);
    r[0] = uint8_t(m.gen);
    r[1] = uint8_t(std::min<size_t>(m.raw.size(), m.gen == 1 ? 33 : 32));
    std::memcpy(&r[2], m.raw.data(), r[1]);
    std::memcpy(&r[50], m.otRaw.data(), 11);
    std::memcpy(&r[61], m.nickRaw.data(), 11);
    return r;
}
GBMon gbFromRec(const std::vector<uint8_t>& r) { return gbFromRaw(r[0], &r[2], r[1], &r[50], &r[61]); }
PK3 pk3FromRec(const std::vector<uint8_t>& r) {
    PK3 p;
    std::memcpy(p.d.data(), r.data(), std::min<size_t>(r.size(), 100));
    p.party = true;
    return p;
}
std::vector<uint8_t> pk3Rec(const PK3& p) {
    std::vector<uint8_t> r(p.d.begin(), p.d.end());
    std::fill(r.begin() + 0x50, r.end(), 0);
    return r;
}
PK45 pk45FromRec(const std::vector<uint8_t>& r) {
    PK45 p;
    p.gen = r[0];
    std::memcpy(p.d.data(), &r[1], std::min<size_t>(r.size() - 1, 236));
    p.party = true;
    return p;
}
std::vector<uint8_t> pk45Rec(const PK45& p) {
    std::vector<uint8_t> r(1 + 236);
    r[0] = uint8_t(p.gen);
    std::memcpy(&r[1], p.d.data(), 0x88);
    return r;
}
std::vector<uint8_t> g67Rec(const PK6& pk) { // Schlüssel + Prüfwert
    std::vector<uint8_t> r(16);
    uint64_t key = (uint64_t(pk.ec()) << 32) | pk.pid(), h = fnv64(pk.d.data(), 0xE8); // ohne Teamwerte
    for (int i = 0; i < 8; i++) { r[size_t(i)] = uint8_t(key >> (8 * i)); r[size_t(8 + i)] = uint8_t(h >> (8 * i)); }
    return r;
}
uint64_t recU64(const std::vector<uint8_t>& r, size_t o) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= uint64_t(r[o + size_t(i)]) << (8 * i);
    return v;
}

Fmt fmtOf(int gen) { return gen <= 2 ? Fmt::GB : gen == 3 ? Fmt::GBA : gen <= 5 ? Fmt::NDS : Fmt::G67; }

// Ein Pokémon, wie es im Spielstand steht.
struct GameMon {
    uint64_t key = 0;
    std::vector<uint8_t> rec;
    bool team = false;
    int box = -1, slot = -1;
};

uint8_t levelOf(const PK6& pk, const SpeciesData& sp) { return levelForExp(sp.get(pk.species()).growth, pk.exp()); }

} // namespace

namespace { WorldLog g_worldLog = nullptr; }
void setWorldLog(WorldLog fn) { g_worldLog = fn; }
static void wlog(const std::string& s) { if (g_worldLog) g_worldLog(s); }

struct WorldImpl {
    Bank& b;
    const SpeciesData& sp;
    const Names& nm;
    WorldReport& rep;
    std::map<uint32_t, uint32_t> touched; // in diesem Abgleich geändert: Kennung -> EP

    BankEntry* byUid(uint32_t uid) {
        if (!uid) return nullptr;
        for (auto& e : b.party) if (e.used && e.uid == uid) return &e;
        for (auto& e : b.boxes) if (e.used && e.uid == uid) return &e;
        return nullptr;
    }

    GBContext gbCtx(const WorldSave& w) {
        GBContext c;
        c.species = &sp;
        c.names = &nm;
        c.trainer = b.trainer;
        c.gen = w.gen;
        c.version = w.version;
        c.language = w.language;
        c.gameTid = w.gb.tid();
        c.gameOt = w.gb.trainerName();
        return c;
    }
    GBAContext gbaCtx(const WorldSave& w) {
        GBAContext c;
        c.species = &sp;
        c.names = &nm;
        c.trainer = b.trainer;
        c.version = w.version;
        c.gameTid = w.gba.tid();
        c.gameSid = w.gba.sid();
        c.gameOt = w.gba.trainerName();
        return c;
    }
    NDSContext ndsCtx(const WorldSave& w) {
        NDSContext c;
        c.species = &sp;
        c.names = &nm;
        c.trainer = b.trainer;
        c.game = w.nds.game();
        c.version = w.version;
        c.gameTid = w.nds.tid();
        c.gameSid = w.nds.sid();
        c.gameOt = w.nds.trainerName();
        return c;
    }

    uint64_t keyOfRec(Fmt f, const std::vector<uint8_t>& rec) {
        switch (f) {
        case Fmt::G67: return recU64(rec, 0);
        case Fmt::GB: { GBMon m = gbFromRec(rec); return gbId(gbKey(m), b.familyOf(m.species)); }
        case Fmt::GBA: { PK3 p = pk3FromRec(rec); return (uint64_t(p.pid()) << 32) | (uint32_t(p.tid()) << 16) | p.sid(); }
        case Fmt::NDS: { PK45 p = pk45FromRec(rec); return (uint64_t(p.pid()) << 32) | (uint32_t(p.tid()) << 16) | p.sid(); }
        }
        return 0;
    }

    std::vector<GameMon> collect(WorldSave& w) {
        std::vector<GameMon> out;
        Fmt f = fmtOf(w.gen);
        auto add = [&](std::vector<uint8_t> rec, bool team, int box, int slot) {
            GameMon g;
            g.rec = std::move(rec);
            g.team = team;
            g.box = box;
            g.slot = slot;
            g.key = f == Fmt::G67 ? recU64(g67Rec([&] { PK6 p; std::memcpy(p.d.data(), g.rec.data(), PK6_PARTY_SIZE); return p; }()), 0)
                                  : keyOfRec(f, g.rec);
            out.push_back(std::move(g));
        };
        switch (f) {
        case Fmt::G67: {
            SaveFile& s = *w.s67;
            for (int i = 0; i < s.partyCount() && i < Bank::PARTY; i++) {
                PK6 pk = s.partySlot(i);
                if (present(pk)) add(std::vector<uint8_t>(pk.d.begin(), pk.d.end()), true, -1, i);
            }
            for (int bx = 0; bx < s.boxCount() && bx < Bank::BOXES; bx++)
                for (int sl = 0; sl < Bank::SLOTS; sl++)
                    if (!s.boxSlotEmpty(bx, sl)) {
                        PK6 pk = s.boxSlot(bx, sl);
                        add(std::vector<uint8_t>(pk.d.begin(), pk.d.end()), false, bx, sl);
                    }
            break;
        }
        case Fmt::GB:
            for (const GBMon& m : w.gb.party()) add(gbRec(m), true, -1, -1);
            for (int bx = 0; bx < w.gb.boxCount(); bx++)
                for (const GBMon& m : w.gb.box(bx)) add(gbRec(m), false, bx, -1);
            break;
        case Fmt::GBA:
            for (const PK3& m : w.gba.party()) if (m.present()) add(pk3Rec(m), true, -1, -1);
            for (int bx = 0; bx < SaveGBA::BOXES; bx++)
                for (int sl = 0; sl < SaveGBA::SLOTS; sl++) {
                    PK3 m = w.gba.boxSlot(bx, sl);
                    if (m.present()) { m.party = true; add(pk3Rec(m), false, bx, sl); }
                }
            break;
        case Fmt::NDS:
            for (const PK45& m : w.nds.party()) if (m.present()) add(pk45Rec(m), true, -1, -1);
            for (int bx = 0; bx < w.nds.boxCount(); bx++)
                for (int sl = 0; sl < SaveNDS::SLOTS; sl++) {
                    PK45 m = w.nds.boxSlot(bx, sl);
                    if (m.present()) { m.party = true; add(pk45Rec(m), false, bx, sl); }
                }
            break;
        }
        return out;
    }

    PK6 toPk7(WorldSave& w, const GameMon& g) {
        switch (fmtOf(w.gen)) {
        case Fmt::G67: {
            PK6 pk;
            std::memcpy(pk.d.data(), g.rec.data(), PK6_PARTY_SIZE);
            return w.s67->generation() == 6 ? toGen7(pk) : pk;
        }
        case Fmt::GB: return gbToPk7(gbFromRec(g.rec), gbCtx(w));
        case Fmt::GBA: return gen3ToPk7(pk3FromRec(g.rec), gbaCtx(w));
        case Fmt::NDS: return gen45ToPk7(pk45FromRec(g.rec), ndsCtx(w));
        }
        return PK6{};
    }

    BankEntry convertNew(WorldSave& w, const GameMon& g) {
        BankEntry e;
        switch (fmtOf(w.gen)) {
        case Fmt::G67: {
            PK6 pk;
            std::memcpy(pk.d.data(), g.rec.data(), PK6_PARTY_SIZE);
            e = importMon(*w.s67, pk, nullptr);
            break;
        }
        case Fmt::GB: {
            GBMon m = gbFromRec(g.rec);
            e.pk = gbToPk7(m, gbCtx(w));
            e.hasGb = true;
            e.gb = m;
            e.gbKey = gbKey(m);
            break;
        }
        case Fmt::GBA:
            e.gba = pk3FromRec(g.rec);
            e.pk = gen3ToPk7(e.gba, gbaCtx(w));
            e.hasGba = true;
            break;
        case Fmt::NDS:
            e.nds = pk45FromRec(g.rec);
            e.pk = gen45ToPk7(e.nds, ndsCtx(w));
            e.hasNds = true;
            break;
        }
        insertSorted(b.seen, monId(e.pk));
        if (reownKeepShiny(e.pk, b.trainer)) { // alle Pokémon gehören dem Speicher-Trainer
            e.hasGba = false; // alter Stand passt nicht mehr zur PID
            e.hasNds = false;
        }
        e.used = true;
        e.uid = b.nextUid++;
        return e;
    }

    // Änderung aus dem Spiel übernehmen (before = dort zuletzt geschriebener Datensatz).
    void applyChange(WorldSave& w, BankEntry& e, const GameMon& g, const std::vector<uint8_t>& before) {
        uint32_t uid = e.uid;
        switch (fmtOf(w.gen)) {
        case Fmt::G67: {
            PK6 pk;
            std::memcpy(pk.d.data(), g.rec.data(), PK6_PARTY_SIZE);
            BankEntry n = importMon(*w.s67, pk, &e);
            e = n;
            break;
        }
        case Fmt::GB: {
            GBMon now = gbFromRec(g.rec), bef = gbFromRec(before);
            mergeGb(e.pk, now, bef, gbCtx(w));
            e.gb = now;
            e.hasGb = true;
            e.gbKey = gbKey(now);
            break;
        }
        case Fmt::GBA: {
            PK3 now = pk3FromRec(g.rec), bef = pk3FromRec(before);
            mergeGen3(e.pk, now, bef, gbaCtx(w));
            e.gba = now;
            e.hasGba = true;
            break;
        }
        case Fmt::NDS: {
            PK45 now = pk45FromRec(g.rec), bef = pk45FromRec(before);
            if (now.gen == bef.gen) mergeGen45(e.pk, now, bef, ndsCtx(w));
            e.nds = now;
            e.hasNds = true;
            break;
        }
        }
        e.used = true;
        e.uid = uid;
    }

    uint32_t expOf(WorldSave& w, const GameMon& g) {
        switch (fmtOf(w.gen)) {
        case Fmt::G67: return rd32(&g.rec[0x10]);
        case Fmt::GB: return gbFromRec(g.rec).exp;
        case Fmt::GBA: return pk3FromRec(g.rec).exp();
        case Fmt::NDS: return pk45FromRec(g.rec).exp();
        }
        return 0;
    }

    std::vector<ItemStack> travelItems(WorldSave& w) {
        switch (fmtOf(w.gen)) {
        case Fmt::G67: return readTravelItems(*w.s67);
        case Fmt::GB: return gbTravelItems(w.gb);
        case Fmt::GBA: return gbaTravelItems(w.gba);
        case Fmt::NDS: return ndsTravelItems(w.nds);
        }
        return {};
    }
    uint32_t moneyOf(WorldSave& w) {
        switch (fmtOf(w.gen)) {
        case Fmt::G67: return w.s67->money();
        case Fmt::GB: return w.gb.money();
        case Fmt::GBA: return w.gba.money();
        case Fmt::NDS: return w.nds.money();
        }
        return 0;
    }
    Trainer6 trainerOf(WorldSave& w) {
        switch (fmtOf(w.gen)) {
        case Fmt::G67: return w.s67->trainer();
        case Fmt::GB: return Trainer6{w.gb.tid(), 0, 0, w.gb.trainerName()};
        case Fmt::GBA: return Trainer6{w.gba.tid(), w.gba.sid(), w.gba.trainerGender(), w.gba.trainerName()};
        case Fmt::NDS: return Trainer6{w.nds.tid(), w.nds.sid(), w.nds.trainerGender(), w.nds.trainerName()};
        }
        return {};
    }

    // ----- Gespielten Spielstand übernehmen -----
    void readPlayed(WorldSave& w, BankSlot& slot) {
        const Fmt f = fmtOf(w.gen);
        std::vector<GameMon> mons = collect(w);
        std::vector<char> usedW(slot.mons.size(), 0);
        std::multimap<uint64_t, size_t> wByKey;
        for (size_t i = 0; i < slot.mons.size(); i++) wByKey.insert({keyOfRec(f, slot.mons[i].rec), i});

        struct Present { uint32_t uid; bool team; bool borrowed; int box, slot; };
        std::vector<Present> present;
        std::vector<std::pair<BankEntry, GameMon>> fresh; // neue Pokémon mit Platz im Spiel
        int added = 0, updated = 0, released = 0;

        for (const GameMon& g : mons) {
            int wi = -1;
            for (auto it = wByKey.lower_bound(g.key); it != wByKey.end() && it->first == g.key; ++it)
                if (!usedW[it->second]) { wi = int(it->second); break; }
            if (wi >= 0) {
                usedW[size_t(wi)] = 1;
                const WrittenMon& wm = slot.mons[size_t(wi)];
                BankEntry* e = byUid(wm.uid);
                if (!e) continue; // inzwischen entlassen
                bool changed = f == Fmt::G67 ? recU64(g67Rec([&] { PK6 p; std::memcpy(p.d.data(), g.rec.data(), PK6_PARTY_SIZE); return p; }()), 8) != recU64(wm.rec, 8)
                                             : g.rec != wm.rec;
                if (changed) {
                    uint32_t exp = expOf(w, g);
                    auto t = touched.find(e->uid);
                    if (t == touched.end() || exp > t->second) {
                        applyChange(w, *e, g, wm.rec);
                        touched[e->uid] = exp;
                        updated++;
                    }
                }
                present.push_back({e->uid, g.team, (wm.flags & 2) != 0, g.box, g.slot});
                continue;
            }
            // Nicht von hier geschrieben: bekannt (z. B. im Spiel getauscht) oder neu
            PK6 pk = toPk7(w, g);
            if (BankEntry* ex = b.find(pk)) {
                uint32_t exp = expOf(w, g);
                if (pk.exp() > ex->pk.exp() && !touched.count(ex->uid)) {
                    uint32_t uid = ex->uid;
                    BankEntry n = convertNew(w, g);
                    b.nextUid--;
                    n.uid = uid;
                    *ex = n;
                    touched[uid] = exp;
                    updated++;
                }
                present.push_back({ex->uid, g.team, false, g.box, g.slot});
                continue;
            }
            if (contains(b.seen, monId(pk))) continue; // freigelassen
            fresh.push_back({convertNew(w, g), g});
            insertSorted(b.seen, monId(fresh.back().first.pk));
            added++;
        }
        // Freigelassen: hier geschrieben, jetzt fehlend
        for (size_t i = 0; i < slot.mons.size(); i++) {
            if (usedW[i]) continue;
            BankEntry* e = byUid(slot.mons[i].uid);
            if (!e) continue;
            b.remember();
            *e = BankEntry{};
            released++;
        }

        // ----- Plätze -----
        auto freeBox = [&](std::vector<BankEntry>& bx) -> BankEntry* { for (auto& e : bx) if (!e.used) return &e; return nullptr; };
        {
            // Ansicht: nur das Team zählt. Nicht geliehene Teammitglieder kommen ins Team,
            // aus dem Team abgelegte (hier geschriebene) Pokémon wandern in einen freien Boxplatz.
            std::vector<uint32_t> teamUids;
            for (const Present& p : present) if (p.team && !p.borrowed) teamUids.push_back(p.uid);
            std::vector<uint32_t> writtenHere;
            for (const WrittenMon& wm : slot.mons) writtenHere.push_back(wm.uid);
            auto in = [](const std::vector<uint32_t>& v, uint32_t u) { return std::find(v.begin(), v.end(), u) != v.end(); };
            std::vector<BankEntry> team, toBox;
            for (uint32_t u : teamUids) {
                BankEntry* e = byUid(u);
                if (!e) continue;
                team.push_back(*e);
                *e = BankEntry{}; // Platz frei (wird neu verteilt)
            }
            for (auto& fr : fresh) if (fr.second.team) team.push_back(fr.first);
            for (auto& e : b.party) {
                if (!e.used) continue;
                if (in(writtenHere, e.uid)) toBox.push_back(e); // hier im Team gewesen, jetzt nicht mehr
                else team.push_back(e);                          // hier nicht sichtbar: bleibt im Team
                e = BankEntry{};
            }
            for (auto& fr : fresh) if (!fr.second.team) toBox.push_back(fr.first);
            for (size_t i = 0; i < team.size(); i++) {
                if (i < Bank::PARTY) b.party[i] = team[i];
                else toBox.push_back(team[i]);
            }
            for (auto& e : toBox) {
                BankEntry* d = freeBox(b.boxes);
                if (d) *d = e;
                else rep.lines.push_back(w.label + ": kein Platz für " + nm.species(e.pk.species()));
            }
        }

        // ----- Items und Geld als Differenz -----
        std::vector<ItemStack> now = travelItems(w);
        auto writtenCount = [&](uint16_t item) -> int { for (auto& it : slot.items) if (it.first == item) return it.second; return 0; };
        auto nowCount = [&](uint16_t item) -> int { int n = 0; for (auto& it : now) if (it.item == item) n += it.count; return n; };
        std::vector<uint16_t> all;
        for (auto& it : slot.items) all.push_back(it.first);
        for (auto& it : now) all.push_back(it.item);
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        for (uint16_t item : all) {
            int delta = nowCount(item) - writtenCount(item);
            if (!delta) continue;
            BankItem* bi = nullptr;
            for (auto& x : b.items) if (x.item == item) bi = &x;
            if (!bi) { b.items.push_back({item, 0, 0}); bi = &b.items.back(); }
            int64_t c = int64_t(bi->count) + delta;
            bi->count = uint32_t(std::max<int64_t>(0, c));
        }
        b.items.erase(std::remove_if(b.items.begin(), b.items.end(), [](const BankItem& x) { return x.count == 0; }), b.items.end());
        int64_t m = int64_t(b.money) + int64_t(moneyOf(w)) - int64_t(slot.money);
        b.money = uint32_t(std::clamp<int64_t>(m, 0, 9999999));
        b.moneyKnown = true;

        rep.added += added;
        rep.updated += updated;
        rep.released += released;
        if (added || updated || released) {
            std::string l = w.label + ":";
            if (added) l += " " + std::to_string(added) + " neu";
            if (updated) l += " " + std::to_string(updated) + " verändert";
            if (released) l += " " + std::to_string(released) + " freigelassen";
            rep.lines.push_back(l);
        }
    }

    // ----- Ansicht schreiben -----
    // Gen 1–5: Team (passend) + mit höchstem Level aufgefüllt, PC nach Pokédex-Nummer.
    struct View { std::vector<BankEntry*> team; std::vector<bool> borrowed; std::vector<BankEntry*> pc; };
    template <class Fits> View oldView(Fits fits, int capacity) {
        View v;
        for (auto& e : b.party) if (e.used && fits(e.pk)) { v.team.push_back(&e); v.borrowed.push_back(false); }
        std::vector<BankEntry*> cand;
        for (auto& e : b.boxes) if (e.used && fits(e.pk)) cand.push_back(&e);
        if (v.team.size() < Bank::PARTY) {
            std::vector<BankEntry*> byLevel = cand;
            std::stable_sort(byLevel.begin(), byLevel.end(), [&](BankEntry* x, BankEntry* y) {
                uint8_t lx = levelOf(x->pk, sp), ly = levelOf(y->pk, sp);
                if (lx != ly) return lx > ly;
                return x->uid < y->uid;
            });
            for (BankEntry* e : byLevel) {
                if (v.team.size() >= Bank::PARTY) break;
                v.team.push_back(e);
                v.borrowed.push_back(true);
            }
        }
        for (BankEntry* e : cand)
            if (std::find(v.team.begin(), v.team.end(), e) == v.team.end()) v.pc.push_back(e);
        std::stable_sort(v.pc.begin(), v.pc.end(), [](BankEntry* x, BankEntry* y) {
            if (x->pk.species() != y->pk.species()) return x->pk.species() < y->pk.species();
            if (x->pk.form() != y->pk.form()) return x->pk.form() < y->pk.form();
            return x->uid < y->uid;
        });
        if (int(v.pc.size()) > capacity) v.pc.resize(size_t(capacity));
        return v;
    }

    bool writeView(WorldSave& w, BankSlot& slot) {
        std::vector<WrittenMon> written;
        std::vector<uint8_t> out;
        std::string e;
        switch (fmtOf(w.gen)) {
        case Fmt::G67: {
            SaveFile& s = *w.s67;
            GameKind k = s.kind();
            PK6 blank;
            bool haveBlank = false;
            for (int bx = 0; bx < s.boxCount() && !haveBlank; bx++)
                for (int sl = 0; sl < Bank::SLOTS && !haveBlank; sl++)
                    if (s.boxSlot(bx, sl).species() == 0 && s.boxSlot(bx, sl).checksumValid()) { blank = s.boxSlot(bx, sl); haveBlank = true; }
            View v = oldView([&](const PK6& pk) { return fitsIn(pk, k); }, s.boxCount() * Bank::SLOTS);
            if (v.team.empty()) { rep.lines.push_back(w.label + ": kein Pokémon im Team kann dort existieren – nicht geschrieben"); return false; }
            int n = 0;
            for (size_t i = 0; i < v.team.size(); i++) {
                BankEntry& en = *v.team[i];
                // Teamwerte fehlen bei Pokémon, die aus einer Box kommen: sonst im Spiel Level 0 / 0 KP
                PK6 src = en.pk;
                if (src.d[0xEC] == 0 || rd16(src.d.data() + 0xF2) == 0) refreshParty7(src, sp);
                BankEntry tmp = en;
                tmp.pk = src;
                PK6 o = exportMon(tmp, k);
                s.setPartySlot(n++, o);
                s.registerDex(o);
                written.push_back({en.uid, uint8_t(1 | (v.borrowed[i] ? 2 : 0)), g67Rec(o)});
            }
            for (int i = n; i < Bank::PARTY; i++) s.setPartySlot(i, blank);
            s.setPartyCount(n);
            size_t idx = 0;
            for (int bx = 0; bx < s.boxCount(); bx++)
                for (int sl = 0; sl < Bank::SLOTS; sl++) {
                    if (idx < v.pc.size()) {
                        PK6 o = exportMon(*v.pc[idx], k);
                        s.setBoxSlot(bx, sl, o);
                        s.registerDex(o);
                        written.push_back({v.pc[idx]->uid, 0, g67Rec(o)});
                        idx++;
                    } else {
                        s.setBoxSlot(bx, sl, blank);
                    }
                }
            b.writeMeta(s);
            s.finalize();
            auto check = openSave(s.raw(), e);
            if (!check || !check->valid()) { rep.lines.push_back(w.label + ": Gegenprobe fehlgeschlagen – nicht geschrieben"); return false; }
            out = s.raw();
            break;
        }
        case Fmt::GB: {
            SaveGB& s = w.gb;
            GBContext ctx = gbCtx(w);
            const int gen = w.gen;
            View v = oldView([&](const PK6& pk) { return fitsGB(pk, gen); }, s.boxCount() * 20);
            if (v.team.empty()) { rep.lines.push_back(w.label + ": kein passendes Pokémon – nicht geschrieben"); return false; }
            auto exportG = [&](BankEntry& en) {
                GBMon m = pk7ToGb(en.pk, en.hasGb ? &en.gb : nullptr, ctx); // Speicherstand bleibt (Ergebnis je Spielstand gemerkt)
                s.registerDex(m.species);
                return m;
            };
            std::vector<GBMon> team;
            for (size_t i = 0; i < v.team.size(); i++) {
                team.push_back(exportG(*v.team[i]));
                written.push_back({v.team[i]->uid, uint8_t(1 | (v.borrowed[i] ? 2 : 0)), gbRec(team.back())});
            }
            s.setParty(team);
            size_t idx = 0;
            for (int bx = 0; bx < s.boxCount(); bx++) {
                std::vector<GBMon> list;
                for (int i = 0; i < 20 && idx < v.pc.size(); i++, idx++) {
                    list.push_back(exportG(*v.pc[idx]));
                    written.push_back({v.pc[idx]->uid, 0, gbRec(list.back())});
                }
                s.setBox(bx, list);
            }
            b.writeMetaGB(s);
            s.finalize();
            SaveGB check;
            if (!check.load(s.raw(), e) || !check.checksumValid()) { rep.lines.push_back(w.label + ": Gegenprobe fehlgeschlagen – nicht geschrieben"); return false; }
            out = s.raw();
            break;
        }
        case Fmt::GBA: {
            SaveGBA& s = w.gba;
            GBAContext ctx = gbaCtx(w);
            View v = oldView([&](const PK6& pk) { return fitsGen3(pk); }, SaveGBA::BOXES * SaveGBA::SLOTS);
            if (v.team.empty()) { rep.lines.push_back(w.label + ": kein passendes Pokémon – nicht geschrieben"); return false; }
            auto exportG = [&](BankEntry& en) {
                PK3 m = pk7ToGen3(en.pk, en.hasGba ? &en.gba : nullptr, ctx);
                if (!m.isEgg()) s.registerDex(m.species());
                return m;
            };
            std::vector<PK3> team;
            for (size_t i = 0; i < v.team.size(); i++) {
                team.push_back(exportG(*v.team[i]));
                written.push_back({v.team[i]->uid, uint8_t(1 | (v.borrowed[i] ? 2 : 0)), pk3Rec(team.back())});
            }
            s.setParty(team);
            size_t idx = 0;
            for (int bx = 0; bx < SaveGBA::BOXES; bx++)
                for (int sl = 0; sl < SaveGBA::SLOTS; sl++) {
                    if (idx < v.pc.size()) {
                        PK3 m = exportG(*v.pc[idx]);
                        s.setBoxSlot(bx, sl, &m);
                        written.push_back({v.pc[idx]->uid, 0, pk3Rec(m)});
                        idx++;
                    } else {
                        s.setBoxSlot(bx, sl, nullptr);
                    }
                }
            b.writeMetaGBA(s);
            s.finalize();
            SaveGBA check;
            if (!check.load(s.raw(), e) || !check.checksumsValid()) { rep.lines.push_back(w.label + ": Gegenprobe fehlgeschlagen – nicht geschrieben"); return false; }
            out = s.raw();
            break;
        }
        case Fmt::NDS: {
            SaveNDS& s = w.nds;
            NDSContext ctx = ndsCtx(w);
            NDSGame g = s.game();
            View v = oldView([&](const PK6& pk) { return fitsGen45(pk, g); }, s.boxCount() * SaveNDS::SLOTS);
            if (v.team.empty()) { rep.lines.push_back(w.label + ": kein passendes Pokémon – nicht geschrieben"); return false; }
            auto exportN = [&](BankEntry& en) {
                PK45 m = pk7ToGen45(en.pk, en.hasNds ? &en.nds : nullptr, ctx);
                s.registerDex(m);
                return m;
            };
            std::vector<PK45> team;
            for (size_t i = 0; i < v.team.size(); i++) {
                team.push_back(exportN(*v.team[i]));
                written.push_back({v.team[i]->uid, uint8_t(1 | (v.borrowed[i] ? 2 : 0)), pk45Rec(team.back())});
            }
            s.setParty(team);
            size_t idx = 0;
            for (int bx = 0; bx < s.boxCount(); bx++)
                for (int sl = 0; sl < SaveNDS::SLOTS; sl++) {
                    if (idx < v.pc.size()) {
                        PK45 m = exportN(*v.pc[idx]);
                        s.setBoxSlot(bx, sl, &m);
                        written.push_back({v.pc[idx]->uid, 0, pk45Rec(m)});
                        idx++;
                    } else {
                        s.setBoxSlot(bx, sl, nullptr);
                    }
                }
            b.writeMetaNDS(s);
            s.finalize();
            SaveNDS check;
            if (!check.load(s.raw(), e) || !check.checksumsValid()) { rep.lines.push_back(w.label + ": Gegenprobe fehlgeschlagen – nicht geschrieben"); return false; }
            out = s.raw();
            break;
        }
        }
        // Geschriebenes merken
        slot.items.clear();
        for (auto& it : b.items)
            if (it.written) slot.items.push_back({it.item, it.written});
        slot.money = b.moneyWritten;
        slot.mons = std::move(written);
        slot.synced = true;
        slot.trainer = trainerOf(w);
        w.changed = out != w.data;
        w.data = out;
        slot.fingerprint = fnv64(w.data.data(), w.data.size());
        if (w.changed) rep.written++;
        return true;
    }
};

namespace {

bool openWorldSave(WorldSave& w, const SpeciesData& sp) {
    std::string err;
    w.ok = false;
    if (w.gen <= 2) {
        if (!sp.loaded() || !w.gb.load(w.data, err) || !w.gb.checksumValid()) return false;
        w.kind = gbKind(w.gb.generation());
    } else if (w.gen == 3) {
        if (!sp.loaded() || !w.gba.load(w.data, err) || !w.gba.checksumsValid()) return false;
        w.kind = gbaKind(w.gba.game());
    } else if (w.gen <= 5) {
        if (!sp.loaded() || !w.nds.load(w.data, err) || !w.nds.checksumsValid()) return false;
        w.kind = ndsKind(w.nds.game());
    } else {
        w.s67 = openSave(w.data, err);
        if (!w.s67 || !w.s67->valid()) return false;
        w.kind = kindName(w.s67->kind());
    }
    w.ok = true;
    return true;
}

void writeAll(WorldImpl& wi, std::vector<WorldSave>& saves) {
    wi.b.sortBoxes(); // alle Boxen nach Pokédex-Nummer (Wunsch des Nutzers)
    for (WorldSave& w : saves) {
        if (!w.ok) continue;
        BankSlot* s = wi.b.slot(w.lowId, w.media);
        if (!s) { wi.b.record(w.lowId, w.media, w.gen, wi.trainerOf(w)); s = wi.b.slot(w.lowId, w.media); }
        // Speicher vor dem Schreiben sichern, damit ein fehlgeschlagener Spielstand nichts verändert
        if (w.gen <= 5 && !s->version) s->version = w.version;
        // Schlägt das Schreiben fehl, nur zurücknehmen, was writeView am Speicher ändert (Items, Geld)
        std::vector<BankItem> items = wi.b.items;
        uint32_t moneyWritten = wi.b.moneyWritten;
        wlog("schreibe " + w.label);
        if (!wi.writeView(w, *s)) {
            wi.b.items = std::move(items);
            wi.b.moneyWritten = moneyWritten;
        }
    }
    wi.b.current.clear();
    wi.b.currentLow = 0;
    wi.b.currentMedia = 0;
}

} // namespace

void worldSync(Bank& bank, std::vector<WorldSave>& saves, const SpeciesData& species, const Names& names, WorldReport& rep) {
    bank.species = &species;
    WorldImpl wi{bank, species, names, rep, {}};
    wlog("Abgleich: " + std::to_string(saves.size()) + " Spielstände, " + std::to_string(bank.count()) + " Pokémon im Speicher");
    for (WorldSave& w : saves) {
        wlog("öffne " + w.label + " (" + std::to_string(w.data.size()) + " Byte)");
        if (!openWorldSave(w, species)) rep.lines.push_back(w.label + ": Spielstand ungültig, übersprungen");
    }
    bank.ensureUids();
    // Herkunftsspiel je Spielstand festhalten (sonst kippt die Schätzung, sobald dort Pokémon aus allen Spielen liegen)
    for (WorldSave& w : saves) {
        if (!w.ok || w.gen > 5) continue;
        BankSlot* s = bank.slot(w.lowId, w.media);
        if (s && s->version) w.version = s->version;
    }

    // 1. Gespielte Spielstände (vom Abgleich geschrieben, seitdem verändert)
    std::vector<SyncInput> legacy;
    std::vector<WorldSave*> legacySaves;
    for (WorldSave& w : saves) {
        if (!w.ok) continue;
        BankSlot* s = bank.slot(w.lowId, w.media);
        if (s && s->synced && bank.hasTrainer()) {
            if (fnv64(w.data.data(), w.data.size()) != s->fingerprint) {
                w.played = true;
                wlog("übernehme Änderungen aus " + w.label);
                wi.readPlayed(w, *s);
            }
            continue;
        }
        // 2. sonst wie bisher einlesen
        SyncInput in;
        in.label = w.label;
        in.lowId = w.lowId;
        in.media = w.media;
        in.gen = w.gen;
        in.gbVersion = w.version;
        in.language = w.language;
        in.data = w.data;
        in.current = !bank.current.empty() && bank.current == w.kind &&
                     (!bank.currentLow || (bank.currentLow == w.lowId && bank.currentMedia == w.media));
        in.known = bank.fileVersion > 0 && bank.fileVersion < 4;
        legacy.push_back(std::move(in));
    }
    if (!legacy.empty()) {
        // Nur das erste „aktuelle“ gilt
        bool cur = false;
        for (auto& in : legacy) { if (in.current && cur) in.current = false; cur |= in.current; }
        SyncReport sr;
        wlog("lese ein (neu): " + std::to_string(legacy.size()) + " Spielstände");
        syncAll(bank, legacy, species, names, sr);
        wlog("eingelesen: " + std::to_string(bank.count()) + " Pokémon");
        for (auto& l : sr.lines) rep.lines.push_back(l);
        rep.added += sr.added;
        rep.updated += sr.updated;
        bank.current.clear();
        bank.ensureUids();
    }
    if (!bank.hasTrainer()) return;
    // 3. Alle Spielstände schreiben
    writeAll(wi, saves);
    wlog("Abgleich fertig");
}

void worldWrite(Bank& bank, std::vector<WorldSave>& saves, const SpeciesData& species, const Names& names, WorldReport& rep) {
    bank.species = &species;
    WorldImpl wi{bank, species, names, rep, {}};
    bank.ensureUids();
    writeAll(wi, saves);
}

} // namespace pw3d
