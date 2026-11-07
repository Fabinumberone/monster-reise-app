// GARC-Archive (Game Freak, 3DS) und LZ11-Kompression
import fs from 'node:fs';

/** Liest ein GARC und gibt pro Eintrag die Unterdateien als Buffer zurück */
export function readGarc(file) {
  const b = fs.readFileSync(file);
  if (b.toString('latin1', 0, 4) !== 'CRAG') throw new Error(`${file}: kein GARC`);
  const headerSize = b.readUInt32LE(4);
  const dataOff = b.readUInt32LE(0x10);
  let p = headerSize;
  // FATO: Offsets der FATB-Einträge
  if (b.toString('latin1', p, p + 4) !== 'OTAF') throw new Error('FATO fehlt');
  const fatoSize = b.readUInt32LE(p + 4);
  const count = b.readUInt16LE(p + 8);
  p += fatoSize;
  if (b.toString('latin1', p, p + 4) !== 'BTAF') throw new Error('FATB fehlt');
  const fatb = p + 12;
  const entries = [];
  for (let i = 0; i < count; i++) {
    let q = fatb + b.readUInt32LE(headerSize + 12 + i * 4);
    const bits = b.readUInt32LE(q);
    q += 4;
    const subs = [];
    for (let s = 0; s < 32; s++) {
      if (!(bits & (1 << s))) continue;
      const start = b.readUInt32LE(q), end = b.readUInt32LE(q + 4);
      q += 12;
      subs.push(b.subarray(dataOff + start, dataOff + end));
    }
    entries.push(subs);
  }
  return entries;
}

/** LZ11 entpacken (Kopfbyte 0x11); andere Daten unverändert zurück */
export function lz11(src) {
  if (src[0] !== 0x11) return src;
  let size = src.readUIntLE(1, 3);
  let p = 4;
  if (size === 0) { size = src.readUInt32LE(4); p = 8; }
  const out = Buffer.alloc(size);
  let o = 0;
  while (o < size) {
    const flags = src[p++];
    for (let bit = 7; bit >= 0 && o < size; bit--) {
      if (!(flags & (1 << bit))) { out[o++] = src[p++]; continue; }
      const b0 = src[p];
      let len, disp;
      switch (b0 >> 4) {
        case 0:
          len = (((src[p] & 0xf) << 4) | (src[p + 1] >> 4)) + 0x11;
          disp = (((src[p + 1] & 0xf) << 8) | src[p + 2]) + 1;
          p += 3;
          break;
        case 1:
          len = (((src[p] & 0xf) << 12) | (src[p + 1] << 4) | (src[p + 2] >> 4)) + 0x111;
          disp = (((src[p + 2] & 0xf) << 8) | src[p + 3]) + 1;
          p += 4;
          break;
        default:
          len = (b0 >> 4) + 1;
          disp = (((b0 & 0xf) << 8) | src[p + 1]) + 1;
          p += 2;
      }
      for (let i = 0; i < len && o < size; i++, o++) out[o] = out[o - disp];
    }
  }
  return out;
}

/** Game-Freak-Paket ("PC", "BM", "ZO" …): zwei Buchstaben, Anzahl, Offsets */
export function readPack(b) {
  const magic = b.toString('latin1', 0, 2);
  const n = b.readUInt16LE(2);
  if (!/^[A-Z]{2}$/.test(magic) || n === 0 || n > 512 || 4 + (n + 1) * 4 > b.length) return null;
  const parts = [];
  for (let i = 0; i < n; i++) {
    const s = b.readUInt32LE(4 + i * 4), e = b.readUInt32LE(8 + i * 4);
    if (s > e || e > b.length) return null;
    parts.push(b.subarray(s, e));
  }
  return { magic, parts };
}
