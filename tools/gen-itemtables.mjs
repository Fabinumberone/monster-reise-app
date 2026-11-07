// Erzeugt core/itemtables.cpp aus den Item-Listen von PKHeX (ItemStorage6XY/6AO/7SM/7USUM.cs).
//   node tools/gen-itemtables.mjs <ordner mit den .cs-Dateien>
// Nur die Taschen, die zwischen den Spielen reisen: normale Items, Medizin, Beeren,
// Z-Kristalle (Gen 7) und Roto-Items (USUM).
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const dir = process.argv[2];
function lists(file) {
  const src = readFileSync(join(dir, file), 'utf8').replace(/\/\/[^\n]*/g, '');
  const out = {};
  for (const m of src.matchAll(/public static ReadOnlySpan<ushort> (\w+) =>\s*\[([^\]]*)\]/g))
    out[m[1]] = m[2].split(',').map(s => s.trim()).filter(Boolean).map(Number);
  return out;
}
const xy = lists('ItemStorage6XY.cs'), ao = lists('ItemStorage6AO.cs');
const sm = lists('ItemStorage7SM.cs'), us = lists('ItemStorage7USUM.cs');
const pick = (a, b, k) => a[k] ?? b[k] ?? (() => { throw new Error('fehlt ' + k); })();

const games = {
  XY: { Items: xy.General, Medicine: xy.Medicine, Berries: xy.Berry },
  ORAS: { Items: pick(ao, xy, 'General'), Medicine: pick(ao, xy, 'Medicine'), Berries: pick(ao, xy, 'Berry') },
  SM: { Items: sm.General, Medicine: sm.Medicine, Berries: sm.Berry, ZCrystals: sm.ZCrystalKey },
  USUM: { Items: pick(us, sm, 'General'), Medicine: pick(us, sm, 'Medicine'), Berries: pick(us, sm, 'Berry'), ZCrystals: us.ZCrystalKey, Battle: us.Roto },
};

let cpp = `// Automatisch erzeugt von tools/gen-itemtables.mjs aus PKHeX (ItemStorage*.cs, GPLv3). Nicht von Hand ändern.
#include "items.hpp"

namespace pw3d {
namespace {
`;
const refs = [];
for (const [g, pouches] of Object.entries(games))
  for (const [p, ids] of Object.entries(pouches)) {
    const sorted = [...new Set(ids)].sort((a, b) => a - b);
    const name = `${g}_${p}`;
    cpp += `const uint16_t ${name}[] = {${sorted.join(', ')}};\n`;
    refs.push({ g, p, name, n: sorted.length });
  }
cpp += `} // namespace\n\nconst ItemList ITEM_TABLES[] = {\n`;
for (const r of refs) cpp += `    {GameKind::${r.g}, Pouch::${r.p}, ${r.name}, ${r.n}},\n`;
cpp += `};\nconst int ITEM_TABLE_COUNT = ${refs.length};\n\n} // namespace pw3d\n`;
writeFileSync(join(import.meta.dirname, '..', 'core', 'itemtables.cpp'), cpp);
console.log(refs.map(r => `${r.g} ${r.p}: ${r.n}`).join('\n'));
