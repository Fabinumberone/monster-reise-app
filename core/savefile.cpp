#include "savefile.hpp"

#include "save6.hpp"
#include "save7.hpp"

namespace pw3d {

// Leere Plätze sind verschlüsselte Leerdatensätze (Art 0); reine Nullen ergeben eine falsche Prüfsumme.
bool SaveFile::boxSlotEmpty(int box, int slot) const {
    PK6 pk = boxSlot(box, slot);
    return pk.species() == 0 || !pk.checksumValid();
}

bool SaveFile::addToParty(const PK6& pk) {
    int n = partyCount();
    if (n >= 6) return false;
    setPartySlot(n, pk);
    setPartyCount(n + 1);
    return true;
}

int SaveFile::adoptTrainer(const Trainer6& t) {
    Trainer6 old = trainer();
    auto isOld = [&](const PK6& pk) { return pk.tid() == old.tid && pk.sid() == old.sid && pk.otName() == old.name; };
    auto reown = [&](PK6& pk) { pk.setTid(t.tid); pk.setSid(t.sid); pk.setOtName(t.name); };
    int n = 0;
    for (int i = 0; i < partyCount() && i < 6; i++) {
        PK6 pk = partySlot(i);
        if (pk.species() && pk.checksumValid() && isOld(pk)) { reown(pk); setPartySlot(i, pk); n++; }
    }
    for (int b = 0; b < boxCount(); b++)
        for (int s = 0; s < BOX_SLOTS; s++) {
            PK6 pk = boxSlot(b, s);
            if (pk.species() && pk.checksumValid() && isOld(pk)) { reown(pk); setBoxSlot(b, s, pk); n++; }
        }
    Trainer6 nt = old;
    nt.tid = t.tid;
    nt.sid = t.sid;
    nt.name = t.name;
    nt.gender = t.gender;
    setTrainer(nt);
    return n;
}

std::unique_ptr<SaveFile> openSave(std::vector<uint8_t> data, std::string& err) {
    if (Save6::knownSize(data.size())) {
        auto s = std::make_unique<Save6>();
        if (!s->load(std::move(data), err)) return nullptr;
        return s;
    }
    auto s = std::make_unique<Save7>();
    if (!s->load(std::move(data), err)) return nullptr;
    return s;
}

} // namespace pw3d
