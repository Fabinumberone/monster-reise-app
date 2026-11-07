// Erzeugt core/ndsitems.cpp aus PKHeX ItemStorage4*/5*.cs (GPLv3): welches Item in welche Tasche
// der Gen-4/5-Spiele gehört (nur die Taschen, aus denen Items reisen können).
//   node tools/gen-ndsitems.mjs <ordner mit den .cs-Dateien>
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const dir = process.argv[2];
function lists(file) {
  const src = readFileSync(join(dir, file), 'utf8').replace(/\/\/[^\n]*/g, '');
  const out = {};
  for (const m of src.matchAll(/static ReadOnlySpan<ushort> (\w+) =>\s*\[([^\]]*)\]/g))
    out[m[1]] = m[2].split(',').map(s => s.trim()).filter(Boolean).map(Number);
  return out;
}
const g4 = lists('ItemStorage4.cs'), hg = lists('ItemStorage4HGSS.cs'), g5 = lists('ItemStorage5.cs');
// Taschen-Nummern wie SaveNDS::Pouch: 0 Items, 4 Medizin, 5 Beeren, 6 Bälle, 7 Kampf-Items
const games = {
  DP: { 0: g4.GeneralDP, 4: g4.Medicine, 5: g4.Berry, 6: g4.BallsDPPt, 7: g4.Battle },
  Pt: { 0: g4.GeneralPt, 4: g4.Medicine, 5: g4.Berry, 6: g4.BallsDPPt, 7: g4.Battle },
  HGSS: { 0: g4.GeneralPt, 4: g4.Medicine, 5: g4.Berry, 6: hg.BallsHGSS, 7: g4.Battle },
  BW: { 0: g5.General, 4: g5.Medicine, 5: g5.Berry },
};
let cpp = `// Automatisch erzeugt von tools/gen-ndsitems.mjs aus PKHeX (ItemStorage4*/5*.cs, GPLv3). Nicht von Hand ändern.
#include "nds.hpp"

namespace pw3d {

namespace {
struct Entry { uint16_t item; int8_t pouch; };
`;
for (const [g, pouches] of Object.entries(games)) {
  const entries = [];
  for (const [p, list] of Object.entries(pouches)) {
    if (!list) throw new Error(`fehlt ${g}/${p}`);
    for (const i of list) entries.push([i, Number(p)]);
  }
  entries.sort((a, b) => a[0] - b[0]);
  cpp += `const Entry k${g}[] = {${entries.map((e, i) => (i % 8 === 0 ? '\n    ' : ' ') + `{${e[0]}, ${e[1]}},`).join('')}\n};\n`;
}
cpp += `
template <size_t N> int find(const Entry (&t)[N], uint16_t item) {
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (t[mid].item < item) lo = mid + 1;
        else hi = mid;
    }
    return (lo < N && t[lo].item == item) ? t[lo].pouch : -1;
}
} // namespace

int ndsTravelPouch(NDSGame g, uint16_t item) {
    switch (g) {
    case NDSGame::DP: return find(kDP, item);
    case NDSGame::Pt: return find(kPt, item);
    case NDSGame::HGSS: return find(kHGSS, item);
    default: return find(kBW, item);
    }
}

} // namespace pw3d
`;
writeFileSync(join(import.meta.dirname, '..', 'core', 'ndsitems.cpp'), cpp);
console.log(Object.entries(games).map(([g, p]) => g + ' ' + Object.values(p).reduce((a, l) => a + l.length, 0)).join(', '));
