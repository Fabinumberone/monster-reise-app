// Erzeugt icons.bin (PC-Symbole aller Pokémon) für die Reise-App aus dem eigenen Ultramond- oder
// Ultrasonne-Dump. Nicht mitgeliefert, nicht committet, liegt nur auf der SD.
//   node tools/gen-icons.mjs [dump-ordner] [ausgabe]
//   Standard: extracted/um -> %APPDATA%\Azahar\sdmc\pokemon-world-3d\icons.bin
//
// Bilder: romfs a/0/6/2 (1154 BFLIM, LZ11, 64×32 RGBA5551, um 90° gedreht gespeichert).
// Zuordnung: Tabelle im Programmcode (exefs/code.bin, geladen ab 0x100000), 16 Byte je Art:
//   u16 Symbol (männlich/normal), u16 Symbol (weiblich), u32 Zeiger Formenliste m, u32 Zeiger
//   Formenliste w, u16 Formen m, u16 Formen w. Formenlisten sind u16-Arrays mit dem Symbol je Form.
// Die Tabelle wird über die bekannte Folge der ersten Symbole (1, 2, 3, 5, 6, 7, 10 …) gefunden.
//
// Ausgabe (siehe app/source/icons.hpp), Version 2:
//   "PW3DICON", u32 2, u16 Arten, u16 Symbole, u32 Offset[Arten+1] (relativ zum Tabellenbereich),
//   Tabellenbereich: je Art u8 Formen, u8 0, dann Formen × (u16 m, u16 w),
//   danach je Symbol 4096 Byte (aufrecht, PICA-Kacheln, Symbol bei x 12–52, y 0–30).
import fs from 'node:fs';
import path from 'node:path';
import { readGarc, lz11 } from './lib/garc.mjs';
import { decodeRgba5551, encodeRgba5551, rotateCCW } from './lib/pica.mjs';

const root = path.join(import.meta.dirname, '..');
const dump = process.argv[2] ?? path.join(root, 'extracted', 'um');
const out = process.argv[3] ?? path.join(process.env.APPDATA, 'Azahar', 'sdmc', 'pokemon-world-3d', 'icons.bin');
const SPECIES = 807, CODE_BASE = 0x100000;

const icons = readGarc(path.join(dump, 'romfs', 'a', '0', '6', '2'));
const code = fs.readFileSync(path.join(dump, 'exefs', 'code.bin'));

// Tabelle suchen: Eintrag für Art 1 beginnt mit 1, Art 2 mit 2, Art 3 mit 3, Art 4 mit 5 …
function findTable() {
  const seq = [1, 2, 3, 5, 6, 7, 10, 11, 12, 14];
  for (let i = 0; i + 16 * seq.length <= code.length; i += 2) {
    if (seq.every((v, k) => code.readUInt16LE(i + k * 16) === v)) return i - 16; // Art 0 davor
  }
  throw new Error('Symboltabelle im Programmcode nicht gefunden – ist das ein USUM-Dump?');
}
const table = findTable();
const list = (ptr, n) => Array.from({ length: n }, (_, k) => code.readUInt16LE(ptr - CODE_BASE + k * 2));

const entries = [];
for (let s = 0; s <= SPECIES; s++) {
  const o = table + s * 16;
  const m = code.readUInt16LE(o), w = code.readUInt16LE(o + 2);
  const mp = code.readUInt32LE(o + 4), fp = code.readUInt32LE(o + 8);
  const mn = code.readUInt16LE(o + 12), fn = code.readUInt16LE(o + 14);
  const ml = mp ? list(mp, mn) : [], fl = fp ? list(fp, fn) : [];
  const n = Math.max(1, ml.length, fl.length);
  const forms = [];
  for (let f = 0; f < n; f++) {
    const male = ml.length ? ml[Math.min(f, ml.length - 1)] : m;
    const female = fl.length ? fl[Math.min(f, fl.length - 1)] : ml.length ? male : w;
    forms.push([male, female]);
  }
  entries.push(forms);
}
const check = entries[25][0][0] === 34 && entries[26][0][0] === 45 && entries[807][0][0] < icons.length;
if (!check) throw new Error('Symboltabelle unplausibel');

const tabParts = [], offsets = [];
let pos = 0;
for (const forms of entries) {
  offsets.push(pos);
  const b = Buffer.alloc(2 + forms.length * 4);
  b[0] = forms.length;
  forms.forEach(([mm, ww], k) => { b.writeUInt16LE(mm, 2 + k * 4); b.writeUInt16LE(ww, 4 + k * 4); });
  tabParts.push(b);
  pos += b.length;
}
const header = Buffer.alloc(16 + (SPECIES + 1) * 4);
header.write('PW3DICON', 0, 'latin1');
header.writeUInt32LE(2, 8);
header.writeUInt16LE(SPECIES, 12);
header.writeUInt16LE(icons.length, 14);
offsets.forEach((v, s) => header.writeUInt32LE(v, 16 + s * 4));
const tab = Buffer.concat(tabParts);
const pad = Buffer.alloc((4 - (tab.length % 4)) % 4);

// Vertikal spiegeln: Die GPU liest die erste Speicherzeile als v = 1 (oben), encodeRgba5551 legt
// aber die untere Bildzeile zuerst ab. In Azahar bestätigt (ohne Spiegelung kopfüber).
function flipV(px, w, h) {
  const out = Buffer.alloc(px.length);
  for (let y = 0; y < h; y++) px.copy(out, (h - 1 - y) * w * 4, y * w * 4, (y + 1) * w * 4);
  return out;
}
// Horizontal spiegeln, damit die Pokémon wie im PC der Spiele nach links schauen (Wunsch des Nutzers).
// Der Symbolbereich x 12–51 bleibt dabei an derselben Stelle (x -> 63 − x).
function flipH(px, w, h) {
  const out = Buffer.alloc(px.length);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) px.copy(out, (y * w + (w - 1 - x)) * 4, (y * w + x) * 4, (y * w + x) * 4 + 4);
  return out;
}
const pics = icons.map(e => {
  let d = e[0];
  if (d[0] === 0x11) d = lz11(d);
  return encodeRgba5551(flipV(flipH(rotateCCW(decodeRgba5551(d, 32, 64), 32, 64), 64, 32), 64, 32), 64, 32);
});
const lenField = Buffer.alloc(4);
lenField.writeUInt32LE(tab.length + pad.length);
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, Buffer.concat([header, lenField, tab, pad, ...pics]));
console.log(`Symboltabelle bei code.bin 0x${table.toString(16)}; ${icons.length} Symbole -> ${out}`);
console.log('Stichprobe:', [[25, 0], [26, 1], [201, 5], [678, 1]].map(([s, f]) => `${s}/${f}=${entries[s][Math.min(f, entries[s].length - 1)].join('|')}`).join(' '));
