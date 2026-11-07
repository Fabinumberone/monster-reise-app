// Erzeugt schlichtes, eigenes Icon (48×48), Banner (256×128) und einen stillen Banner-Ton
// für die .cia der Reise-App: Farbverlauf mit drei verbundenen Punkten (Hoenn, Kalos, Alola).
//   node tools/gen-cia-art.mjs   -> app/assets/icon.png, banner.png, banner.wav
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { deflateSync } from 'node:zlib';

const out = join(import.meta.dirname, '..', 'app', 'assets');
mkdirSync(out, { recursive: true });

function crc32(buf) {
  let c, crc = 0xffffffff;
  for (const b of buf) { c = (crc ^ b) & 0xff; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(w, h, px) { // px: Float32 RGBA 0..1
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 4; c++)
    raw[y * (w * 4 + 1) + 1 + x * 4 + c] = Math.round(Math.max(0, Math.min(1, px[(y * w + x) * 4 + c])) * 255);
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);

function draw(w, h) {
  const px = new Float32Array(w * h * 4);
  const s = Math.min(w, h);
  const pts = [[0.2, 0.68, [0.86, 0.2, 0.2]], [0.5, 0.32, [0.2, 0.45, 0.9]], [0.8, 0.68, [0.95, 0.75, 0.15]]]
    .map(([x, y, c]) => [x * w, y * h, c]);
  const r = s * 0.13, lw = s * 0.035;
  const segDist = (x, y, [ax, ay], [bx, by]) => {
    const dx = bx - ax, dy = by - ay, t = Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)));
    return Math.hypot(x - ax - t * dx, y - ay - t * dy);
  };
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let col = mix([0.45, 0.78, 0.95], [0.1, 0.55, 0.5], y / h); // Himmel → Meer
    // Verbindungslinien
    const d = Math.min(segDist(x, y, pts[0], pts[1]), segDist(x, y, pts[1], pts[2]));
    col = mix(col, [1, 1, 1], Math.max(0, Math.min(1, lw - d + 0.5)) * 0.9);
    // Punkte mit weißem Rand
    for (const [cx, cy, c] of pts) {
      const dd = Math.hypot(x + 0.5 - cx, y + 0.5 - cy);
      col = mix(col, [1, 1, 1], Math.max(0, Math.min(1, r + lw - dd + 0.5)));
      col = mix(col, c, Math.max(0, Math.min(1, r - dd + 0.5)));
    }
    px.set([...col, 1], (y * w + x) * 4);
  }
  return px;
}

writeFileSync(join(out, 'icon.png'), png(48, 48, draw(48, 48)));
writeFileSync(join(out, 'banner.png'), png(256, 128, draw(256, 128)));

// 0,5 s Stille, 16 Bit mono, 22050 Hz
const n = 11025, wav = Buffer.alloc(44 + n * 2);
wav.write('RIFF', 0); wav.writeUInt32LE(36 + n * 2, 4); wav.write('WAVE', 8); wav.write('fmt ', 12);
wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22); wav.writeUInt32LE(22050, 24);
wav.writeUInt32LE(44100, 28); wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write('data', 36); wav.writeUInt32LE(n * 2, 40);
writeFileSync(join(out, 'banner.wav'), wav);
console.log('icon.png, banner.png, banner.wav erzeugt in', out);
