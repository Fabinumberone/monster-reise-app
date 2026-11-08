// Erzeugt die Dateien, die die Monster Reise App auf der SD braucht, aus eigenen Spieldateien.
// Nichts davon wird mitgeliefert oder committet (enthält Daten aus den Spielen).
//   arten.bin  – Artdaten (Ultrasonne/Ultramond + Gen-1/2-ROM aus der Virtual Console)
//   namen.txt  – deutsche Namen von Pokémon, Items, Attacken (+ englische Artnamen)
//   icons.bin  – PC-Symbole (Ultrasonne/Ultramond)
// usum: Objekt aus container.mjs (openGame), roms: [{ name, data }] (Gen-1/2-ROMs)
import { readGarc, lz11 } from './garc.mjs';
import { readGfText } from './gftext.mjs';
import { decodeRgba5551, encodeRgba5551, rotateCCW } from './pica.mjs';

const SPECIES = 807;

/** Ist das ein Ultrasonne/Ultramond-Inhalt? (deutsche Texte, Symbole, Personal) */
export function isUsum(g) {
  if (!['a/0/3/5', 'a/0/6/2', 'a/0/1/7', 'a/0/1/4'].every(f => g.has(f))) return false;
  try {
    const t = readGfText(readGarc(g.file('a/0/3/5'))[60][0]);
    return t[725] === 'Flamiau' && t.length > 807;
  } catch {
    return false;
  }
}

// ------------------------------------------------------------------------------------ namen.txt
// Format: UTF-8, je Zeile „P|I|A|E <Tab> Nummer <Tab> Name“ (E = englischer Artname).
export function buildNamen(g) {
  const garc = readGarc(g.file('a/0/3/5')); // Deutsch
  const text = i => readGfText(garc[i][0]);
  const species = text(60), items = text(40), moves = text(118);
  if (species[725] !== 'Flamiau' || items[4] !== 'Pokéball' || moves[33] !== 'Tackle')
    throw new Error('Unerwartete Texte – ist das Ultrasonne/Ultramond?');
  const lines = [];
  const add = (tag, list) => list.forEach((n, i) => { if (i > 0 && n && n !== '???') lines.push(`${tag}\t${i}\t${n.replace(/\s+/g, ' ').trim()}`); });
  add('P', species);
  add('I', items);
  add('A', moves);
  const en = readGfText(readGarc(g.file('a/0/3/2'))[60][0]);
  if (en[255] !== 'Torchic') throw new Error('Englische Artnamen nicht gefunden');
  add('E', en);
  return Buffer.from(lines.join('\n') + '\n', 'utf8');
}

// ------------------------------------------------------------------------------------ arten.bin
// "PW3DARTN", u32 2, u16 Arten (807), u16 Satzgröße (28), dann je Art 0..807:
//   u8 Wachstum, u8 Geschlecht (0 nur ♂ … 254 nur ♀, 255 keins), u8 Fähigkeit 1, 2, versteckt,
//   u8 Grundfreundschaft,
//   Gen 1 (0 wenn unbekannt): u8 KP, Ang, Ver, Init, Spez, u8 Typ 1, Typ 2, u8 Fangrate,
//   Gen 2: u8 KP, Ang, Ver, Init, SpAng, SpVer,
//   Gen 7: u8 KP, Ang, Ver, Init, SpAng, SpVer (Basiswerte),
//   u16 Familie (Grundart der Entwicklungsreihe, aus der Entwicklungstabelle a/0/1/4).
const REC = 28;
const G1 = Buffer.from([1, 45, 49, 49, 45, 65, 0x16, 0x03, 45, 64]);
const G2 = Buffer.from([1, 45, 49, 49, 45, 65, 65, 0x16, 0x03, 45, 64]);

/** Sucht in einem ROM die Gen-1- bzw. Gen-2-Basiswerttabelle */
export function findGbTables(data) {
  const r = {};
  const a = data.indexOf(G1);
  if (a >= 0) {
    // Mew: in Gelb direkt hinter Mewtu, in Rot/Blau separat
    let mew = a + 150 * 28;
    if (data[mew] !== 151) mew = data.indexOf(Buffer.from([151, 100, 100, 100, 100, 100]));
    if (mew >= 0) r.gen1 = { table: a, mew };
  }
  const b = data.indexOf(G2);
  if (b >= 0) r.gen2 = { table: b };
  return r;
}

export function buildArten(g, roms, log = () => {}) {
  const pers = readGarc(g.file('a/0/1/7'));
  // Entwicklungen: je Art 8 × 8 Byte (u16 Methode, u16 Parameter, u16 Zielart, s8 Form, u8 Level)
  const evo = readGarc(g.file('a/0/1/4'));
  const parent = new Array(SPECIES + 1).fill(0);
  for (let s = 1; s <= SPECIES; s++)
    for (let i = 0; i < 8; i++) {
      const e = evo[s][0].subarray(i * 8, i * 8 + 8);
      const t = e.readUInt16LE(4);
      if (e.readUInt16LE(0) && t && t <= SPECIES && t !== s && !parent[t]) parent[t] = s;
    }
  const family = s => { let f = s, n = 0; while (parent[f] && n++ < 5) f = parent[f]; return f; };
  if (family(3) !== 1 || family(26) !== 172 || family(186) !== 60) throw new Error('Entwicklungstabelle passt nicht');

  let gen1 = null, gen2 = null;
  for (const r of roms) {
    const t = findGbTables(r.data);
    if (t.gen1 && !gen1) gen1 = { ...r, ...t.gen1 };
    if (t.gen2 && !gen2) gen2 = { ...r, ...t.gen2 };
  }
  log(`  Gen-1-Werte: ${gen1 ? gen1.name : 'kein ROM gefunden (Gen 1 reist dann nicht)'}`);
  log(`  Gen-2-Werte: ${gen2 ? gen2.name : 'kein ROM gefunden (Gen 2 reist dann nicht)'}`);

  const out = Buffer.alloc(16 + (SPECIES + 1) * REC);
  out.write('PW3DARTN', 0, 'latin1');
  out.writeUInt32LE(2, 8);
  out.writeUInt16LE(SPECIES, 12);
  out.writeUInt16LE(REC, 14);
  for (let s = 1; s <= SPECIES; s++) {
    const p = pers[s][0], o = 16 + s * REC;
    out[o] = p[0x15]; out[o + 1] = p[0x12];
    out[o + 2] = p[0x18]; out[o + 3] = p[0x19]; out[o + 4] = p[0x1a];
    out[o + 5] = p[0x14];
    p.copy(out, o + 20, 0, 6);
    out.writeUInt16LE(family(s), o + 26);
    if (gen1 && s <= 151) {
      const e = s === 151 ? gen1.mew : gen1.table + (s - 1) * 28;
      if (gen1.data[e] !== s) throw new Error(`Gen-1-Tabelle passt nicht bei Art ${s}`);
      gen1.data.copy(out, o + 6, e + 1, e + 9);
    }
    if (gen2 && s <= 251) {
      const e = gen2.table + (s - 1) * 32;
      if (gen2.data[e] !== s) throw new Error(`Gen-2-Tabelle passt nicht bei Art ${s}`);
      gen2.data.copy(out, o + 14, e + 1, e + 7);
    }
  }
  return { data: out, gen1: !!gen1, gen2: !!gen2 };
}

// ------------------------------------------------------------------------------------ icons.bin
// Bilder: romfs a/0/6/2 (BFLIM, LZ11, 64×32 RGBA5551, um 90° gedreht gespeichert).
// Zuordnung: Tabelle im Programmcode (exefs .code, geladen ab 0x100000), 16 Byte je Art:
//   u16 Symbol m, u16 Symbol w, u32 Zeiger Formenliste m, u32 Zeiger Formenliste w, u16 Formen m, u16 Formen w.
// Ausgabe (app/source/icons.hpp), Version 2:
//   "PW3DICON", u32 2, u16 Arten, u16 Symbole, u32 Offset[Arten+1], u32 Länge Tabellenbereich,
//   Tabellenbereich: je Art u8 Formen, u8 0, dann Formen × (u16 m, u16 w), danach je Symbol 4096 Byte.
const CODE_BASE = 0x100000;

function flipV(px, w, h) {
  const out = Buffer.alloc(px.length);
  for (let y = 0; y < h; y++) px.copy(out, (h - 1 - y) * w * 4, y * w * 4, (y + 1) * w * 4);
  return out;
}
function flipH(px, w, h) {
  const out = Buffer.alloc(px.length);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) px.copy(out, (y * w + (w - 1 - x)) * 4, (y * w + x) * 4, (y * w + x) * 4 + 4);
  return out;
}

export function buildIcons(g) {
  const icons = readGarc(g.file('a/0/6/2'));
  const code = g.code();
  if (!code) throw new Error('Programmcode (ExeFS) fehlt');
  const seq = [1, 2, 3, 5, 6, 7, 10, 11, 12, 14];
  let table = -1;
  for (let i = 0; i + 16 * seq.length <= code.length; i += 2)
    if (seq.every((v, k) => code.readUInt16LE(i + k * 16) === v)) { table = i - 16; break; }
  if (table < 0) throw new Error('Symboltabelle im Programmcode nicht gefunden (Programmcode komprimiert?)');
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
  if (!(entries[25][0][0] === 34 && entries[26][0][0] === 45 && entries[807][0][0] < icons.length))
    throw new Error('Symboltabelle unplausibel');

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
  // Vertikal spiegeln (GPU liest die erste Zeile als v = 1), horizontal, damit sie nach links schauen.
  const pics = icons.map(e => {
    let d = e[0];
    if (d[0] === 0x11) d = lz11(d);
    return encodeRgba5551(flipV(flipH(rotateCCW(decodeRgba5551(d, 32, 64), 32, 64), 64, 32), 64, 32), 64, 32);
  });
  const lenField = Buffer.alloc(4);
  lenField.writeUInt32LE(tab.length + pad.length);
  return Buffer.concat([header, lenField, tab, pad, ...pics]);
}
