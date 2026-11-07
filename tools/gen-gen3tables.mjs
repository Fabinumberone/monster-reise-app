// Erzeugt core/gen3tables.cpp aus PKHeX (GPLv3):
//   SpeciesConverter.cs (Table3NationalToInternal/Table3InternalToNational),
//   ItemConverter.cs (Item3to4, 128 = keine Entsprechung),
//   StringConverter3.cs (G3_EN, gilt auch für Deutsch; Anführungszeichen sprachabhängig).
//   node tools/gen-gen3tables.mjs <ordner mit den .cs-Dateien>
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const dir = process.argv[2];
const read = f => readFileSync(join(dir, f), 'utf8').replace(/\/\/[^\n]*/g, '');
function numList(src, name) {
  const m = src.match(new RegExp(name + '\\s*=>\\s*\\[([^\\]]*)\\]'));
  if (!m) throw new Error('fehlt ' + name);
  return m[1].split(',').map(s => s.trim()).filter(Boolean).map(s => (s === 'NaN' ? 128 : Number(s)));
}

// Arten
const sc = read('SpeciesConverter.cs');
const n2i = numList(sc, 'Table3NationalToInternal'), i2n = numList(sc, 'Table3InternalToNational');
const toInternal = [], toNational = [];
for (let s = 0; s <= 386; s++) toInternal.push(s < 252 ? s : (s - 252 < n2i.length ? s + n2i[s - 252] : s));
for (let r = 0; r < 277 + i2n.length; r++) toNational.push(r < 252 ? r : r < 277 ? 0 : r + i2n[r - 277]);
for (let s = 1; s <= 386; s++) if (toNational[toInternal[s]] !== s) throw new Error('Artentabelle nicht umkehrbar bei ' + s);

// Items
const items = numList(read('ItemConverter.cs'), 'Item3to4').map(v => (v === 128 ? 0 : v));

// Zeichen
const s3 = readFileSync(join(dir, 'StringConverter3.cs'), 'utf8');
const body = s3.match(/G3_EN\s*=>\s*\[([\s\S]*?)\];/)[1].replace(/\/\/[^\n]*/g, '');
const chars = [];
for (const m of body.matchAll(/'((?:\\'|[^'])+)'|\b(HGM|HGF|Terminator)\b/g)) {
  if (m[2] === 'HGM') chars.push('♂');
  else if (m[2] === 'HGF') chars.push('♀');
  else if (m[2] === 'Terminator') chars.push('');
  else chars.push(m[1] === "\\'" ? "'" : m[1]);
}
if (chars.length !== 256) throw new Error('Zeichentabelle hat ' + chars.length + ' Einträge');
chars[0xB1] = '„'; chars[0xB2] = '“'; // Deutsch
chars[0xFF] = '';
const esc = c => c.replace(/\\/g, '\\\\').replace(/"/g, '\\"');

const fmt = (arr, per = 16) => arr.map((v, i) => (i % per === 0 ? '\n    ' : '') + v + ',').join('');
const cpp = `// Automatisch erzeugt von tools/gen-gen3tables.mjs aus PKHeX (SpeciesConverter, ItemConverter,
// StringConverter3; GPLv3). Nicht von Hand ändern.
#include "gen3tables.hpp"

namespace pw3d {

const uint16_t kGen3NationalToInternal[${toInternal.length}] = {${fmt(toInternal)}
};
const uint16_t kGen3InternalToNational[${toNational.length}] = {${fmt(toNational)}
};
const int kGen3InternalCount = ${toNational.length};
const uint16_t kGen3ItemToModern[${items.length}] = {${fmt(items)}
};
const int kGen3ItemCount = ${items.length};
const char* const kGen3Chars[256] = {${chars.map((c, i) => (i % 16 === 0 ? '\n    ' : '') + '"' + esc(c) + '",').join('')}
};

} // namespace pw3d
`;
writeFileSync(join(import.meta.dirname, '..', 'core', 'gen3tables.cpp'), cpp);
console.log(`Arten ${toNational.length}, Items ${items.length}, Zeichen 256 -> core/gen3tables.cpp`);
