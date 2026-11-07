// Erzeugt die deutsche Namensliste (Pokémon, Items, Attacken) für Reise-App und PC-Werkzeug
// aus dem eigenen Ultramond- oder Ultrasonne-Dump. Die Datei wird nicht mitgeliefert und
// nicht committet, sondern liegt nur auf der SD-Karte.
//   node tools/gen-namen.mjs [romfs-ordner] [ausgabe]
//   Standard: extracted/um/romfs  ->  %APPDATA%\Azahar\sdmc\pokemon-world-3d\namen.txt
// Format: UTF-8, je Zeile „P|I|A|E <Tab> Nummer <Tab> Name“ (E = englischer Artname).
import fs from 'node:fs';
import path from 'node:path';
import { readGarc } from './lib/garc.mjs';
import { readGfText } from './lib/gftext.mjs';

const root = path.join(import.meta.dirname, '..');
const romfs = process.argv[2] ?? path.join(root, 'extracted', 'um', 'romfs');
const out = process.argv[3] ?? path.join(process.env.APPDATA, 'Azahar', 'sdmc', 'pokemon-world-3d', 'namen.txt');

// USUM: Texte je Sprache in a/0/3/<n>, Deutsch = 5. Datei 60 Pokémon, 40 Items, 118 Attacken.
const garc = readGarc(path.join(romfs, 'a', '0', '3', '5'));
const text = i => readGfText(garc[i][0]);
const species = text(60), items = text(40), moves = text(118);
if (species[725] !== 'Flamiau' || items[4] !== 'Pokéball' || moves[33] !== 'Tackle')
  throw new Error('Unerwartete Texte – ist das ein deutscher USUM-Dump?');

const lines = [];
const add = (tag, list) => list.forEach((n, i) => { if (i > 0 && n && n !== '???') lines.push(`${tag}\t${i}\t${n.replace(/\s+/g, ' ').trim()}`); });
add('P', species);
add('I', items);
add('A', moves);
// Englische Artnamen (für die Spitznamen-Erkennung bei englischen Gen-1/2-Spielen)
const en = readGfText(readGarc(path.join(romfs, 'a', '0', '3', '2'))[60][0]);
if (en[255] !== 'Torchic') throw new Error('Englische Artnamen nicht gefunden');
add('E', en);
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, lines.join('\n') + '\n', 'utf8');
console.log(`${lines.length} Namen -> ${out}`);
console.log('Beispiele:', [species[255], species[725], items[4], items[17], items[22], moves[52]].join(', '));
