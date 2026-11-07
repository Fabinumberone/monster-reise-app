#include "sync.hpp"

#include <memory>

#include "gb.hpp"
#include "gba.hpp"
#include "gbaconvert.hpp"
#include "gbconvert.hpp"
#include "savefile.hpp"

namespace pw3d {

namespace {

struct Opened {
    const SyncInput* in = nullptr;
    std::unique_ptr<SaveFile> sav; // Gen 6/7
    SaveGB gb;                     // Gen 1/2
    SaveGBA gba;                   // Gen 3
    int count = 0;                 // Anzahl Pokémon (für die Wahl des Trainers)
};

GBContext ctxFor(const SyncInput& in, const Bank& bank, const SpeciesData& sp, const Names& names) {
    GBContext c;
    c.species = &sp;
    c.names = &names;
    c.trainer = bank.trainer;
    c.gen = in.gen;
    c.version = in.gbVersion;
    c.language = in.language;
    return c;
}

GBAContext gbaCtxFor(const SyncInput& in, const Bank& bank, const SpeciesData& sp, const Names& names) {
    GBAContext c;
    c.species = &sp;
    c.names = &names;
    c.trainer = bank.trainer;
    c.version = in.gbVersion;
    return c;
}

Trainer6 trainerOf(const Opened& o) {
    if (o.in->gen <= 2) return Trainer6{o.gb.tid(), 0, 0, o.gb.trainerName()};
    if (o.in->gen == 3) return Trainer6{o.gba.tid(), o.gba.sid(), o.gba.trainerGender(), o.gba.trainerName()};
    return o.sav->trainer();
}

} // namespace

void syncAll(Bank& bank, const std::vector<SyncInput>& saves, const SpeciesData& species, const Names& names,
             SyncReport& rep) {
    bank.species = &species;
    std::vector<Opened> ok;
    for (const SyncInput& in : saves) {
        Opened o;
        o.in = &in;
        std::string err;
        if (in.gen == 3) {
            if (!species.loaded()) { rep.lines.push_back(in.label + ": arten.bin fehlt, übersprungen"); continue; }
            if (!o.gba.load(in.data, err) || !o.gba.checksumsValid()) { rep.lines.push_back(in.label + ": Spielstand ungültig, übersprungen"); continue; }
            for (const PK3& m : o.gba.party()) o.count += m.present();
            for (int b = 0; b < SaveGBA::BOXES; b++)
                for (int s = 0; s < SaveGBA::SLOTS; s++) o.count += o.gba.boxSlot(b, s).present();
        } else if (in.gen <= 2) {
            if (!species.loaded()) { rep.lines.push_back(in.label + ": arten.bin fehlt, übersprungen"); continue; }
            if (!o.gb.load(in.data, err) || !o.gb.checksumValid()) { rep.lines.push_back(in.label + ": Spielstand ungültig, übersprungen"); continue; }
            o.count = int(o.gb.party().size());
            for (int b = 0; b < o.gb.boxCount(); b++) o.count += int(o.gb.box(b).size());
        } else {
            o.sav = openSave(in.data, err);
            if (!o.sav || !o.sav->valid()) { rep.lines.push_back(in.label + ": Spielstand ungültig, übersprungen"); continue; }
            o.count = o.sav->partyCount();
            for (int b = 0; b < o.sav->boxCount(); b++)
                for (int s = 0; s < Bank::SLOTS; s++) o.count += !o.sav->boxSlotEmpty(b, s);
        }
        ok.push_back(std::move(o));
    }

    const SyncInput* origin = nullptr;
    // Neuer Speicher: Trainer wählen (Gen 6/7 vor Gen 1/2, dann die meisten Pokémon).
    if (!bank.hasTrainer()) {
        const Opened* best = nullptr;
        for (const Opened& o : ok) {
            bool modern = o.in->gen >= 6;
            bool bestModern = best && best->in->gen >= 6;
            if (!best || (modern && !bestModern) || (modern == bestModern && o.count > best->count)) best = &o;
        }
        if (!best) return;
        origin = best->in;
        bank.trainer = trainerOf(*best);
        bank.current.clear();
        rep.createdTrainer = true;
        rep.lines.push_back("Neuer Speicher, Trainer aus " + best->in->label);
    }

    // Erst das aktuelle Spiel (dessen Stand gilt), dann die übrigen.
    for (int pass = 0; pass < 2; pass++)
        for (const Opened& o : ok) {
            const SyncInput& in = *o.in;
            if (in.current != (pass == 0)) continue;
            std::string err;
            if (in.current) {
                if (!bank.slot(in.lowId, in.media)) bank.record(in.lowId, in.media, in.gen, trainerOf(o));
                bool done = in.gen == 3   ? bank.departGBA(o.gba, gbaCtxFor(in, bank, species, names), err, true)
                            : in.gen <= 2 ? bank.departGB(o.gb, ctxFor(in, bank, species, names), err, true)
                                          : bank.depart(*o.sav, err, true);
                if (!done) rep.lines.push_back(in.label + ": " + err);
                rep.currentSynced = done;
                continue;
            }
            if (in.known && !bank.slot(in.lowId, in.media)) bank.record(in.lowId, in.media, in.gen, trainerOf(o));
            bool force = o.in == origin;
            IntegrateResult r = in.gen == 3   ? bank.integrateGBA(o.gba, gbaCtxFor(in, bank, species, names), in.lowId, in.media, force)
                                : in.gen <= 2 ? bank.integrateGB(o.gb, ctxFor(in, bank, species, names), in.lowId, in.media, force)
                                              : bank.integrate(*o.sav, in.lowId, in.media, force);
            rep.added += r.added;
            rep.updated += r.updated;
            rep.noRoom += r.noRoom;
            if (r.added || r.updated || r.noRoom || r.newSave) {
                std::string l = in.label + ":";
                if (r.added) l += " " + std::to_string(r.added) + " neu";
                if (r.updated) l += " " + std::to_string(r.updated) + " weitertrainiert";
                if (r.noRoom) l += " " + std::to_string(r.noRoom) + " ohne Platz";
                if (r.newSave) l += " (neuer Spielstand, Items und Geld dazu)";
                rep.lines.push_back(l);
            }
        }
}

} // namespace pw3d
