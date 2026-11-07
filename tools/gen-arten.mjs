// Erzeugt arten.bin (Artdaten für die Umwandlung Gen 1/2 <-> Gen 7) aus den eigenen Dumps:
//  - Ultramond/Ultrasonne (Personal a/0/1/7): Wachstum, Geschlechterverteilung, Fähigkeiten, Freundschaft
//  - Gen-1-ROM (Rot/Blau/Gelb, aus der installierten Virtual-Console-Fassung): Basiswerte, Typen, Fangrate
//  - Gen-2-ROM (Gold/Silber/Kristall): Basiswerte
// Die ROMs werden in den in Azahar installierten VC-Titeln gesucht (Ordner rom/ im RomFS).
// Nicht mitgeliefert, nicht committet, liegt nur auf der SD.
//   node tools/gen-arten.mjs [um-dump] [ausgabe]
//
// arten.bin: "PW3DARTN", u32 1, u16 Arten (807), u16 Satzgröße (20), dann je Art 0..807:
//   u8 Wachstum, u8 Geschlecht (0 nur ♂ … 254 nur ♀, 255 keins), u8 Fähigkeit 1, 2, versteckt,
//   u8 Grundfreundschaft,
//   Gen 1 (0 wenn unbekannt): u8 KP, Ang, Ver, Init, Spez, u8 Typ 1, Typ 2, u8 Fangrate,
//   Gen 2: u8 KP, Ang, Ver, Init, SpAng, SpVer.
import fs from 'node:fs';
import path from 'node:path';
import { readGarc } from './lib/garc.mjs';
import { openNcchRomfs } from './lib/ncch.mjs';

const root = path.join(import.meta.dirname, '..');
const dump = process.argv[2] ?? path.join(root, 'extracted', 'um');
const sd = path.join(process.env.APPDATA, 'Azahar', 'sdmc');
const out = process.argv[3] ?? path.join(sd, 'pokemon-world-3d', 'arten.bin');
const SPECIES = 807, REC = 20;

const pers = readGarc(path.join(dump, 'romfs', 'a', '0', '1', '7'));

// Gen-1/2-ROMs in den installierten VC-Titeln suchen
function findRoms() {
  const base = path.join(sd, 'Nintendo 3DS', '00000000000000000000000000000000', '00000000000000000000000000000000', 'title', '00040000');
  const roms = [];
  for (const t of fs.existsSync(base) ? fs.readdirSync(base) : []) {
    const dir = path.join(base, t, 'content');
    if (!fs.existsSync(dir)) continue;
    for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.app'))) {
      try {
        const n = openNcchRomfs(path.join(dir, f));
        const r = n.files.find(x => x.path.startsWith('rom/'));
        if (r) roms.push({ title: t, name: r.path, data: n.read(r) });
        n.close();
      } catch { /* kein lesbarer Inhalt */ }
    }
  }
  return roms;
}
const roms = findRoms();
const G1 = Buffer.from([1, 45, 49, 49, 45, 65, 0x16, 0x03, 45, 64]);
const G2 = Buffer.from([1, 45, 49, 49, 45, 65, 65, 0x16, 0x03, 45, 64]);
let gen1 = null, gen2 = null;
for (const r of roms) {
  const a = r.data.indexOf(G1);
  if (a >= 0 && !gen1) {
    // Mew: in Gelb direkt hinter Mewtu, in Rot/Blau separat
    let mew = a + 150 * 28;
    if (r.data[mew] !== 151) mew = r.data.indexOf(Buffer.from([151, 100, 100, 100, 100, 100]));
    gen1 = { ...r, table: a, mew };
  }
  const b = r.data.indexOf(G2);
  if (b >= 0 && !gen2) gen2 = { ...r, table: b };
}
console.log('Gen-1-ROM:', gen1 ? `${gen1.title} ${gen1.name}` : 'keine gefunden');
console.log('Gen-2-ROM:', gen2 ? `${gen2.title} ${gen2.name}` : 'keine gefunden');

const outBuf = Buffer.alloc(16 + (SPECIES + 1) * REC);
outBuf.write('PW3DARTN', 0, 'latin1');
outBuf.writeUInt32LE(1, 8);
outBuf.writeUInt16LE(SPECIES, 12);
outBuf.writeUInt16LE(REC, 14);
for (let s = 1; s <= SPECIES; s++) {
  const p = pers[s][0], o = 16 + s * REC;
  outBuf[o] = p[0x15]; outBuf[o + 1] = p[0x12];
  outBuf[o + 2] = p[0x18]; outBuf[o + 3] = p[0x19]; outBuf[o + 4] = p[0x1a];
  outBuf[o + 5] = p[0x14];
  if (gen1 && s <= 151) {
    const e = s === 151 ? gen1.mew : gen1.table + (s - 1) * 28;
    if (gen1.data[e] !== s) throw new Error(`Gen-1-Tabelle passt nicht bei Art ${s}`);
    gen1.data.copy(outBuf, o + 6, e + 1, e + 9); // 5 Basiswerte, 2 Typen, Fangrate
  }
  if (gen2 && s <= 251) {
    const e = gen2.table + (s - 1) * 32;
    if (gen2.data[e] !== s) throw new Error(`Gen-2-Tabelle passt nicht bei Art ${s}`);
    gen2.data.copy(outBuf, o + 14, e + 1, e + 7); // 6 Basiswerte
  }
}
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, outBuf);
console.log(`Artdaten für ${SPECIES} Arten -> ${out}`);
