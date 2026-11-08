// Baut das Daten-Werkzeug als einzelne HTML-Datei (läuft offline in jedem Browser, auch auf dem Mac):
//   node tools/gen-html.mjs [ausgabe]   Standard: build/release/monster-reise-daten.html
// Die Bibliotheken aus tools/lib werden eingebettet (import/export entfernt), Node-Buffer ersetzt
// tools/web/buffer-shim.js. Enthält keine Spieldaten.
import fs from 'node:fs';
import path from 'node:path';

const root = path.join(import.meta.dirname, '..');
const out = process.argv[2] ?? path.join(root, 'build', 'release', 'monster-reise-daten.html');
const strip = src => src.replace(/^import .*$/gm, '').replace(/^export /gm, '');
const parts = [
  'tools/web/buffer-shim.js',
  'tools/lib/garc.mjs',
  'tools/lib/gftext.mjs',
  'tools/lib/pica.mjs',
  'tools/lib/monsterdaten.mjs',
  'tools/web/kern.js',
].map(f => `// ---- ${f}\n` + strip(fs.readFileSync(path.join(root, f), 'utf8')));
const libs = '//KERN-START\n' + parts.join('\n') + '\n//KERN-END';
const html = fs.readFileSync(path.join(root, 'tools', 'web', 'vorlage.html'), 'utf8').replace('/*BIBLIOTHEKEN*/', () => libs);
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, html);
console.log(`${out} (${(html.length / 1024).toFixed(0)} KB)`);
