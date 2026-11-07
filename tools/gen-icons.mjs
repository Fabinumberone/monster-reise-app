// Erzeugt icons.bin (PC-Symbole aller Pokémon) für die Reise-App aus dem eigenen Ultramond-Dump.
// Nicht mitgeliefert, nicht committet, liegt nur auf der SD.
//   node tools/gen-icons.mjs [romfs-ordner] [ausgabe]
//   Standard: extracted/um/romfs -> %APPDATA%\Azahar\sdmc\pokemon-world-3d\icons.bin
//
// Quelle: a/0/6/2 (1154 BFLIM, 64×32, RGBA5551, um 90° gedreht gespeichert). Eintrag 0 ist ein
// Platzhalter, danach Arten in Pokédex-Reihenfolge, jeweils mit ihren Symbolvarianten direkt
// hintereinander. Anzahl je Art: siehe iconCounts() (Formen aus Personal a/0/1/7 bzw. Modelltabelle
// a/0/9/4 ohne weibliche Modelle, plus bekannte Abweichungen).
//
// Ausgabe (siehe app/source/icons.hpp): "PW3DICON", u32 1, u16 Arten, u16 Symbole,
// u16 Start[Arten+1], u8 Anzahl[Arten+1], je Symbol 4096 Byte (aufrecht, PICA-Kacheln).
import fs from 'node:fs';
import path from 'node:path';
import { readGarc, lz11 } from './lib/garc.mjs';
import { decodeRgba5551, encodeRgba5551, rotateCCW } from './lib/pica.mjs';

const root = path.join(import.meta.dirname, '..');
const romfs = process.argv[2] ?? path.join(root, 'extracted', 'um', 'romfs');
const out = process.argv[3] ?? path.join(process.env.APPDATA, 'Azahar', 'sdmc', 'pokemon-world-3d', 'icons.bin');
const SPECIES = 807;

const icons = readGarc(path.join(romfs, 'a', '0', '6', '2'));
const pers = readGarc(path.join(romfs, 'a', '0', '1', '7'));

// Modelltabelle: Eintrag 0 von a/0/9/4, je Art 4 Byte (u16 Start, u8 Anzahl, u8 Flags; Bit 2 = weibliches Modell)
function modelTable() {
  const f = path.join(romfs, 'a', '0', '9', '4'), fd = fs.openSync(f, 'r'), head = Buffer.alloc(0x40000);
  fs.readSync(fd, head, 0, head.length, 0);
  const hs = head.readUInt32LE(4), dataOff = head.readUInt32LE(0x10), fato = head.readUInt32LE(hs + 4), fatb = hs + fato + 12;
  const q = fatb + head.readUInt32LE(hs + 12), st = head.readUInt32LE(q + 4), en = head.readUInt32LE(q + 8);
  const mt = Buffer.alloc(en - st);
  fs.readSync(fd, mt, 0, mt.length, dataOff + st);
  fs.closeSync(fd);
  return mt;
}

// Bekannte Abweichungen (Art: Anzahl Symbole), von Hand an den Bildern geprüft.
const OVERRIDES = JSON.parse(fs.readFileSync(path.join(import.meta.dirname, 'icon-overrides.json'), 'utf8'));

function iconCounts() {
  const mt = modelTable(), counts = [0];
  for (let s = 1; s <= SPECIES; s++) {
    const o = (s - 1) * 4, female = (mt[o + 3] & 2) !== 0, models = mt[o + 2] - (female ? 1 : 0);
    counts[s] = OVERRIDES[s] ?? Math.max(models, pers[s][0][0x20]);
  }
  return counts;
}

const counts = iconCounts();
const start = [0];
let i = 1;
for (let s = 1; s <= SPECIES; s++) { start[s] = i; i += counts[s]; }
if (i !== icons.length) console.warn(`WARNUNG: Zuordnung ergibt ${i} Symbole, Archiv hat ${icons.length}. Ab der ersten Abweichung sind die Symbole verschoben.`);

const header = Buffer.alloc(16 + (SPECIES + 1) * 3);
header.write('PW3DICON', 0, 'latin1');
header.writeUInt32LE(1, 8);
header.writeUInt16LE(SPECIES, 12);
header.writeUInt16LE(icons.length, 14);
for (let s = 0; s <= SPECIES; s++) { header.writeUInt16LE(start[s], 16 + s * 2); header[16 + (SPECIES + 1) * 2 + s] = counts[s]; }
const parts = [header];
for (const e of icons) {
  let d = e[0];
  if (d[0] === 0x11) d = lz11(d);
  const upright = rotateCCW(decodeRgba5551(d, 32, 64), 32, 64); // 64×32, aufrecht
  parts.push(encodeRgba5551(upright, 64, 32));
}
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, Buffer.concat(parts));
console.log(`${icons.length} Symbole -> ${out}`);
