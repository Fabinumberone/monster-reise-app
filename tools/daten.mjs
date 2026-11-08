// Monster Reise App – Daten erzeugen
//
// Liest eigene, entschlüsselte Spieldateien und erzeugt daraus die Dateien, die die App auf der
// SD-Karte braucht (arten.bin, namen.txt, icons.bin). Es wird nichts heruntergeladen und nichts
// von den Spielen mitgeliefert.
//
//   node daten.mjs <datei oder ordner …> [--ziel <ordner>]
//
// Eingaben (beliebig viele, Ordner werden durchsucht):
//   - Ultrasonne oder Ultramond (Pflicht): entschlüsselte CIA, .3ds/.cci oder .cxi
//   - Virtual Console Rot/Blau/Gelb und Gold/Silber/Kristall (für Gen 1/2): entschlüsselte CIA
//   - oder der SD-Ordner von Azahar (dort liegen installierte Spiele entschlüsselt)
// Ohne Angaben: Azahar-SD (%APPDATA%\Azahar\sdmc) und extracted/um, falls vorhanden.
// Ausgabe: <ziel>/pokemon-world-3d/ – diesen Ordner ins Hauptverzeichnis der SD-Karte kopieren.
// Standardziel: Ordner „SD“ neben dem Aufruf; „--ziel azahar“ schreibt direkt auf die Azahar-SD.
import fs from 'node:fs';
import path from 'node:path';
import { openGame } from './lib/container.mjs';
import { isUsum, buildNamen, buildArten, buildIcons, findGbTables } from './lib/monsterdaten.mjs';

const args = process.argv.slice(2);
let ziel = path.resolve('SD');
const inputs = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--ziel') ziel = args[++i];
  else if (args[i] === '-h' || args[i] === '--hilfe') {
    console.log('Aufruf: node daten.mjs <Spieldateien oder Ordner …> [--ziel <ordner>|azahar]');
    process.exit(0);
  } else inputs.push(args[i]);
}
const azaharSd = process.env.APPDATA ? path.join(process.env.APPDATA, 'Azahar', 'sdmc') : '';
if (ziel === 'azahar') ziel = azaharSd;
if (!inputs.length) {
  const own = path.join(import.meta.dirname, '..', 'extracted', 'um');
  if (fs.existsSync(path.join(own, 'romfs'))) inputs.push(own);
  if (azaharSd && fs.existsSync(azaharSd)) inputs.push(azaharSd);
  if (!inputs.length) {
    console.log('Keine Spieldateien angegeben. Aufruf: node daten.mjs <Spieldateien oder Ordner …>');
    process.exit(1);
  }
}

// Kandidaten sammeln
const EXT = /\.(cia|3ds|cci|cxi|app)$/i;
const files = [];
function walk(p, depth) {
  let st;
  try { st = fs.statSync(p); } catch { console.log(`Nicht gefunden: ${p}`); return; }
  if (st.isFile()) { files.push(p); return; }
  if (fs.existsSync(path.join(p, 'romfs'))) { files.push(p); return; } // entpackter Dump
  if (depth > 10) return;
  for (const n of fs.readdirSync(p)) {
    if (n === 'node_modules' || n.startsWith('.')) continue;
    const q = path.join(p, n);
    let s;
    try { s = fs.statSync(q); } catch { continue; }
    if (s.isDirectory()) walk(q, depth + 1);
    else if (EXT.test(n)) files.push(q);
  }
}
for (const i of inputs) walk(path.resolve(i), 0);

let usum = null;
const roms = [], encrypted = [];
for (const f of files) {
  let g;
  try { g = openGame(f); } catch (e) {
    if (/verschlüsselt/.test(e.message)) encrypted.push(f);
    continue;
  }
  try {
    if (!usum && isUsum(g)) { usum = g; console.log(`Ultrasonne/Ultramond: ${f}`); continue; }
    for (const r of (g.files ?? []).filter(x => x.path.startsWith('rom/'))) {
      const data = g.file(r.path);
      const t = findGbTables(data);
      if (t.gen1 || t.gen2) roms.push({ name: `${r.path.slice(4)} (${path.basename(f)})`, data });
    }
  } catch { /* unlesbar */ }
  if (g !== usum) g.close();
}

if (!usum) {
  console.log('\nUltrasonne oder Ultramond nicht gefunden.');
  if (encrypted.length) console.log(`${encrypted.length} Datei(en) sind verschlüsselt – bitte als entschlüsselte CIA sichern (siehe Anleitung).`);
  process.exit(1);
}

const out = path.join(ziel, 'pokemon-world-3d');
fs.mkdirSync(out, { recursive: true });
const write = (name, data) => { fs.writeFileSync(path.join(out, name), data); console.log(`  ${name} (${(data.length / 1024).toFixed(0)} KB)`); };
console.log(`\nSchreibe nach ${out}:`);
const arten = buildArten(usum, roms, console.log);
write('arten.bin', arten.data);
write('namen.txt', buildNamen(usum));
write('icons.bin', buildIcons(usum));
usum.close();
console.log('\nFertig. Den Ordner „pokemon-world-3d“ ins Hauptverzeichnis der SD-Karte kopieren.');
if (!arten.gen1 || !arten.gen2) console.log('Hinweis: Ohne Gen-1/2-ROM können Pokémon aus Gen 1 bzw. 2 nicht reisen.');
console.log('Diese Dateien enthalten Daten aus deinen Spielen: nicht weitergeben oder hochladen.');
