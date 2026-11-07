// Entpackt RomFS (und ExeFS) des Hauptinhalts einer entschlüsselten CIA.
// Aufruf: node tools/extract-cia.mjs <datei.cia> <zielordner>
import fs from 'node:fs';
import path from 'node:path';

const [file, out] = process.argv.slice(2);
if (!file || !out) {
  console.error('Aufruf: node tools/extract-cia.mjs <datei.cia> <zielordner>');
  process.exit(1);
}
const fd = fs.openSync(file, 'r');
const read = (off, len) => {
  const b = Buffer.alloc(len);
  fs.readSync(fd, b, 0, len, off);
  return b;
};
const align = (n, a) => Math.ceil(n / a) * a;
const MEDIA = 0x200;

// ---------------------------------------------------------------- CIA → NCCH (Inhalt 0)
const h = read(0, 0x20);
const certOff = align(h.readUInt32LE(0), 64);
const tikOff = certOff + align(h.readUInt32LE(8), 64);
const tmdOff = tikOff + align(h.readUInt32LE(12), 64);
const ncch = tmdOff + align(h.readUInt32LE(16), 64);
const nh = read(ncch, 0x200);
if (nh.subarray(0x100, 0x104).toString('latin1') !== 'NCCH') throw new Error('Kein NCCH gefunden');
if (!(nh[0x18f] & 4)) throw new Error('Inhalt ist verschlüsselt');
console.log('Spiel:', nh.subarray(0x150, 0x160).toString('latin1').replace(/\0+$/, ''));

// ---------------------------------------------------------------- ExeFS
const exefsOff = ncch + nh.readUInt32LE(0x1a0) * MEDIA;
if (nh.readUInt32LE(0x1a4)) {
  const eh = read(exefsOff, 0x200);
  const dir = path.join(out, 'exefs');
  fs.mkdirSync(dir, { recursive: true });
  for (let i = 0; i < 10; i++) {
    const name = eh.subarray(i * 16, i * 16 + 8).toString('latin1').replace(/\0+$/, '');
    if (!name) continue;
    const off = eh.readUInt32LE(i * 16 + 8), size = eh.readUInt32LE(i * 16 + 12);
    fs.writeFileSync(path.join(dir, name.replace(/^\./, '') + '.bin'), read(exefsOff + 0x200 + off, size));
  }
}

// ---------------------------------------------------------------- RomFS (IVFC Level 3)
const romfs = ncch + nh.readUInt32LE(0x1b0) * MEDIA;
const ivfc = read(romfs, 0x60);
if (ivfc.subarray(0, 4).toString('latin1') !== 'IVFC') throw new Error('Kein IVFC-Kopf');
const masterHash = ivfc.readUInt32LE(8);
const l3Block = 1 << ivfc.readUInt32LE(0x4c);
const l3 = romfs + align(0x60 + masterHash, l3Block);
const lh = read(l3, 0x28);
const [, , , dirMetaOff, dirMetaLen, , , fileMetaOff, fileMetaLen, fileDataOff] = [...Array(10)].map((_, i) => lh.readUInt32LE(i * 4));
const dirMeta = read(l3 + dirMetaOff, dirMetaLen);
const fileMeta = read(l3 + fileMetaOff, fileMetaLen);
const NONE = 0xffffffff;
const name = (buf, off, len) => buf.subarray(off, off + len).toString('utf16le');

let files = 0, bytes = 0;
const CHUNK = 16 << 20;
function writeFile(target, off, size) {
  const wfd = fs.openSync(target, 'w');
  for (let p = 0; p < size; p += CHUNK) {
    const n = Math.min(CHUNK, size - p);
    fs.writeSync(wfd, read(off + p, n));
  }
  fs.closeSync(wfd);
}
function walkDir(dOff, dirPath) {
  fs.mkdirSync(dirPath, { recursive: true });
  for (let f = dirMeta.readUInt32LE(dOff + 12); f !== NONE; ) {
    const size = Number(fileMeta.readBigUInt64LE(f + 16));
    const off = Number(fileMeta.readBigUInt64LE(f + 8));
    writeFile(path.join(dirPath, name(fileMeta, f + 0x20, fileMeta.readUInt32LE(f + 0x1c))), l3 + fileDataOff + off, size);
    files++;
    bytes += size;
    f = fileMeta.readUInt32LE(f + 4);
  }
  for (let c = dirMeta.readUInt32LE(dOff + 8); c !== NONE; c = dirMeta.readUInt32LE(c + 4)) {
    walkDir(c, path.join(dirPath, name(dirMeta, c + 0x18, dirMeta.readUInt32LE(c + 0x14))));
  }
}
walkDir(0, path.join(out, 'romfs'));
console.log(`${files} Dateien, ${(bytes / 1048576).toFixed(0)} MB entpackt nach ${out}`);
