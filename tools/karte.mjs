// Weltkarte für die App umwandeln (Windows, nutzt System.Drawing über PowerShell zum Verkleinern):
//   node tools/karte.mjs <kartenbild.jpg|png> [ziel-ordner]
// Ergebnis: <ziel>/pokemon-world-3d/weltkarte.bin – auf die SD kopieren. Das Kartenbild selbst wird nicht
// mitgeliefert (eigene Datei). Auf dem Mac geht dasselbe mit monster-reise-daten.html (Bild mit hineinziehen).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { encodeMap, MAP_W, MAP_H } from './lib/weltkarte.mjs';

const [src, zielArg] = process.argv.slice(2);
if (!src) { console.log('Aufruf: node tools/karte.mjs <kartenbild> [ziel-ordner]'); process.exit(1); }
const ziel = path.resolve(zielArg ?? 'SD');
const tmp = path.join(os.tmpdir(), `karte-${process.pid}.raw`);
// Auf volle Breite skalieren, mittig auf 400 × 240 zuschneiden, als BGRA-Rohdaten ausgeben
const ps = `
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile('${path.resolve(src).replace(/'/g, "''")}')
$h = [int][Math]::Round(${MAP_W} * $img.Height / $img.Width)
$bmp = New-Object System.Drawing.Bitmap ${MAP_W}, ${MAP_H}, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = 'HighQualityBicubic'
$g.DrawImage($img, 0, [int](( ${MAP_H} - $h) / 2), ${MAP_W}, $h)
$rect = New-Object System.Drawing.Rectangle 0, 0, ${MAP_W}, ${MAP_H}
$data = $bmp.LockBits($rect, 'ReadOnly', ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))
$bytes = New-Object byte[] (${MAP_W} * ${MAP_H} * 4)
for ($y = 0; $y -lt ${MAP_H}; $y++) { [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $bytes, $y * ${MAP_W} * 4, ${MAP_W} * 4) }
$bmp.UnlockBits($data)
[IO.File]::WriteAllBytes('${tmp.replace(/'/g, "''")}', $bytes)
$img.Dispose()
`;
execFileSync('powershell.exe', ['-NoProfile', '-Command', ps], { stdio: 'inherit' });
const bgra = fs.readFileSync(tmp);
fs.unlinkSync(tmp);
const rgba = Buffer.alloc(bgra.length);
for (let i = 0; i < bgra.length; i += 4) { rgba[i] = bgra[i + 2]; rgba[i + 1] = bgra[i + 1]; rgba[i + 2] = bgra[i]; rgba[i + 3] = 255; }
const out = path.join(ziel, 'pokemon-world-3d', 'weltkarte.bin');
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, encodeMap(rgba));
console.log(`Weltkarte -> ${out}`);
