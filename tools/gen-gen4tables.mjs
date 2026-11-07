// Erzeugt core/gen4tables.cpp aus PKHeX StringConverter4Util.cs (TableINT, GPLv3): Gen-4-Zeichencode -> Zeichen.
//   node tools/gen-gen4tables.mjs <ordner mit StringConverter4Util.cs>
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const src = readFileSync(join(process.argv[2], 'StringConverter4Util.cs'), 'utf8');
const body = src.match(/TableINT\s*=>\s*\[([\s\S]*?)\];/)[1].replace(/\/\/[^\n]*/g, '');
const chars = [];
for (const m of body.matchAll(/'((?:\\u[0-9A-Fa-f]{4}|\\'|\\\\|[^'])+)'|\b(NUL|EMP|HGM|HGF)\b/g)) {
  if (m[2] === 'NUL' || m[2] === 'EMP') chars.push('');
  else if (m[2] === 'HGM') chars.push('♂');
  else if (m[2] === 'HGF') chars.push('♀');
  else {
    let c = m[1];
    if (c.startsWith('\\u')) c = String.fromCharCode(parseInt(c.slice(2), 16));
    else if (c === "\\'") c = "'";
    else if (c === '\\\\') c = '\\';
    chars.push(c);
  }
}
if (chars[0x12B] !== 'A' && chars.indexOf('A') < 0) throw new Error('Tabelle unerwartet');
const esc = c => c.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
const cpp = `// Automatisch erzeugt von tools/gen-gen4tables.mjs aus PKHeX (StringConverter4Util.TableINT, GPLv3).
// Nicht von Hand ändern.
#include "gen4tables.hpp"

namespace pw3d {

const int kGen4CharCount = ${chars.length};
const char* const kGen4Chars[${chars.length}] = {${chars.map((c, i) => (i % 16 === 0 ? '\n    ' : '') + '"' + esc(c) + '",').join('')}
};

} // namespace pw3d
`;
writeFileSync(join(import.meta.dirname, '..', 'core', 'gen4tables.cpp'), cpp);
console.log(`${chars.length} Zeichen, 'A' bei 0x${chars.indexOf('A').toString(16)}, 'ä' bei 0x${chars.indexOf('ä').toString(16)}`);
