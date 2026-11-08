// Öffnet eigene, entschlüsselte 3DS-Inhalte ohne sie zu entpacken:
//  - CIA (Inhalt 0), .3ds/.cci (NCSD, Partition 0), .cxi/.app (NCCH)
//  - entpackter Ordner mit romfs/ und exefs/ (z. B. aus extract-cia.mjs)
// Liefert { name, productCode, has(pfad), file(pfad), code() } – pfad relativ zum RomFS ("a/0/3/5").
import fs from 'node:fs';
import path from 'node:path';
import { openNcchRomfs } from './ncch.mjs';

const MEDIA = 0x200;
const align = (n, a) => Math.ceil(n / a) * a;

function readAt(file, off, len) {
  const fd = fs.openSync(file, 'r');
  try {
    const b = Buffer.alloc(len);
    fs.readSync(fd, b, 0, len, off);
    return b;
  } finally {
    fs.closeSync(fd);
  }
}

/** Offset des NCCH in einer Datei (CIA, NCSD oder NCCH), sonst -1 */
export function ncchOffset(file) {
  const h = readAt(file, 0, 0x200);
  if (h.toString('latin1', 0x100, 0x104) === 'NCCH') return 0;
  if (h.toString('latin1', 0x100, 0x104) === 'NCSD') return h.readUInt32LE(0x120) * MEDIA;
  // CIA: Kopfgröße 0x2020
  if (h.readUInt32LE(0) === 0x2020) {
    const certOff = align(h.readUInt32LE(0), 64);
    const tikOff = certOff + align(h.readUInt32LE(8), 64);
    const tmdOff = tikOff + align(h.readUInt32LE(12), 64);
    const off = tmdOff + align(h.readUInt32LE(16), 64);
    if (readAt(file, off + 0x100, 4).toString('latin1') === 'NCCH') return off;
  }
  return -1;
}

function exefsFile(file, base, want) {
  const nh = readAt(file, base, 0x200);
  if (!nh.readUInt32LE(0x1a4)) return null;
  const exefs = base + nh.readUInt32LE(0x1a0) * MEDIA;
  const eh = readAt(file, exefs, 0x200);
  for (let i = 0; i < 10; i++) {
    const name = eh.toString('latin1', i * 16, i * 16 + 8).replace(/\0+$/, '');
    if (name !== want) continue;
    return readAt(file, exefs + 0x200 + eh.readUInt32LE(i * 16 + 8), eh.readUInt32LE(i * 16 + 12));
  }
  return null;
}

export function openGame(p) {
  if (fs.statSync(p).isDirectory()) {
    const romfs = path.join(p, 'romfs');
    return {
      name: p,
      productCode: '',
      has: f => fs.existsSync(path.join(romfs, f)),
      file: f => fs.readFileSync(path.join(romfs, f)),
      code: () => (fs.existsSync(path.join(p, 'exefs', 'code.bin')) ? fs.readFileSync(path.join(p, 'exefs', 'code.bin')) : null),
      close() {},
    };
  }
  const base = ncchOffset(p);
  if (base < 0) throw new Error('kein 3DS-Inhalt (CIA, .3ds oder NCCH)');
  const n = openNcchRomfs(p, base); // wirft bei verschlüsselten Inhalten
  const byPath = new Map(n.files.map(f => [f.path, f]));
  return {
    name: p,
    productCode: n.productCode,
    files: n.files,
    has: f => byPath.has(f),
    file: f => {
      const e = byPath.get(f);
      if (!e) throw new Error(`${f} fehlt`);
      return n.read(e);
    },
    code: () => exefsFile(p, base, '.code'),
    close: () => n.close(),
  };
}
