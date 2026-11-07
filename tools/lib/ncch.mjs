// NCCH-Container (3DS-Inhalt) und dessen RomFS lesen, ohne alles zu entpacken.
// Für unverschlüsselte Inhalte, z. B. in Azahar installierte Titel oder entschlüsselte CIAs.
import fs from 'node:fs';

const MEDIA = 0x200;
const align = (n, a) => Math.ceil(n / a) * a;

/** Öffnet einen NCCH ab Offset base und liefert {name, files, read(file)}; files = [{path, off, size}]. */
export function openNcchRomfs(file, base = 0) {
  const fd = fs.openSync(file, 'r');
  const read = (off, len) => { const b = Buffer.alloc(len); fs.readSync(fd, b, 0, len, off); return b; };
  const nh = read(base, 0x200);
  if (nh.toString('latin1', 0x100, 0x104) !== 'NCCH') throw new Error(`${file}: kein NCCH`);
  if (!(nh[0x18f] & 4)) throw new Error(`${file}: Inhalt ist verschlüsselt`);
  const productCode = nh.toString('latin1', 0x150, 0x160).replace(/\0+$/, '');
  const files = [];
  if (!nh.readUInt32LE(0x1b4)) return { productCode, files, read: () => null, close: () => fs.closeSync(fd) };
  const romfs = base + nh.readUInt32LE(0x1b0) * MEDIA;
  const ivfc = read(romfs, 0x60);
  if (ivfc.toString('latin1', 0, 4) !== 'IVFC') throw new Error('Kein IVFC-Kopf');
  const l3 = romfs + align(0x60 + ivfc.readUInt32LE(8), 1 << ivfc.readUInt32LE(0x4c));
  const lh = read(l3, 0x28);
  const v = i => lh.readUInt32LE(i * 4);
  const dirMeta = read(l3 + v(3), v(4)), fileMeta = read(l3 + v(7), v(8)), dataOff = l3 + v(9);
  const NONE = 0xffffffff, nm = (b, o, l) => b.subarray(o, o + l).toString('utf16le');
  (function walk(d, p) {
    for (let f = dirMeta.readUInt32LE(d + 12); f !== NONE; f = fileMeta.readUInt32LE(f + 4))
      files.push({ path: p + nm(fileMeta, f + 0x20, fileMeta.readUInt32LE(f + 0x1c)), off: dataOff + Number(fileMeta.readBigUInt64LE(f + 8)), size: Number(fileMeta.readBigUInt64LE(f + 16)) });
    for (let c = dirMeta.readUInt32LE(d + 8); c !== NONE; c = dirMeta.readUInt32LE(c + 4)) walk(c, p + nm(dirMeta, c + 0x18, dirMeta.readUInt32LE(c + 0x14)) + '/');
  })(0, '');
  return { productCode, files, read: f => read(f.off, f.size), close: () => fs.closeSync(fd) };
}
