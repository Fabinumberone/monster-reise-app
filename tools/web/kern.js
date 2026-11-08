// Browser-Teil des Daten-Werkzeugs: Spieldateien (File/Blob) lesen, Daten erzeugen, ZIP bauen.
// Nutzt isUsum, buildNamen, buildArten, buildIcons, findGbTables aus monsterdaten.mjs.

const MEDIA = 0x200;
const alignUp = (n, a) => Math.ceil(n / a) * a;
const readBlob = async (f, off, len) => Buffer.from(await f.slice(off, off + len).arrayBuffer());

async function ncchBase(f) {
  const h = await readBlob(f, 0, 0x200);
  const magic = h.toString('latin1', 0x100, 0x104);
  if (magic === 'NCCH') return 0;
  if (magic === 'NCSD') return h.readUInt32LE(0x120) * MEDIA;
  if (h.readUInt32LE(0) === 0x2020) {
    const certOff = alignUp(h.readUInt32LE(0), 64);
    const tikOff = certOff + alignUp(h.readUInt32LE(8), 64);
    const tmdOff = tikOff + alignUp(h.readUInt32LE(12), 64);
    const off = tmdOff + alignUp(h.readUInt32LE(16), 64);
    if ((await readBlob(f, off + 0x100, 4)).toString('latin1') === 'NCCH') return off;
  }
  return -1;
}

/** Öffnet eine Spieldatei; Dateien vor dem Zugriff mit load() holen. */
async function openGameWeb(f) {
  const base = await ncchBase(f);
  if (base < 0) throw new Error('kein 3DS-Inhalt');
  const nh = await readBlob(f, base, 0x200);
  if (!(nh[0x18f] & 4)) throw new Error('verschlüsselt');
  const files = [], cache = new Map();
  if (nh.readUInt32LE(0x1b4)) {
    const romfs = base + nh.readUInt32LE(0x1b0) * MEDIA;
    const ivfc = await readBlob(f, romfs, 0x60);
    if (ivfc.toString('latin1', 0, 4) !== 'IVFC') throw new Error('Kein IVFC-Kopf');
    const l3 = romfs + alignUp(0x60 + ivfc.readUInt32LE(8), 2 ** ivfc.readUInt32LE(0x4c));
    const lh = await readBlob(f, l3, 0x28);
    const v = i => lh.readUInt32LE(i * 4);
    const dirMeta = await readBlob(f, l3 + v(3), v(4)), fileMeta = await readBlob(f, l3 + v(7), v(8)), dataOff = l3 + v(9);
    const NONE = 0xffffffff, nm = (b, o, l) => b.toString('utf16le', o, o + l);
    (function walk(d, p) {
      for (let x = dirMeta.readUInt32LE(d + 12); x !== NONE; x = fileMeta.readUInt32LE(x + 4))
        files.push({ path: p + nm(fileMeta, x + 0x20, fileMeta.readUInt32LE(x + 0x1c)), off: dataOff + Number(fileMeta.readBigUInt64LE(x + 8)), size: Number(fileMeta.readBigUInt64LE(x + 16)) });
      for (let c = dirMeta.readUInt32LE(d + 8); c !== NONE; c = dirMeta.readUInt32LE(c + 4)) walk(c, p + nm(dirMeta, c + 0x18, dirMeta.readUInt32LE(c + 0x14)) + '/');
    })(0, '');
  }
  const byPath = new Map(files.map(x => [x.path, x]));
  return {
    name: f.name,
    files,
    has: p => byPath.has(p),
    async load(paths) {
      for (const p of paths) {
        if (cache.has(p)) continue;
        if (p === '.code') {
          if (!nh.readUInt32LE(0x1a4)) continue;
          const exefs = base + nh.readUInt32LE(0x1a0) * MEDIA;
          const eh = await readBlob(f, exefs, 0x200);
          for (let i = 0; i < 10; i++)
            if (eh.toString('latin1', i * 16, i * 16 + 8).replace(/\0+$/, '') === '.code')
              cache.set(p, await readBlob(f, exefs + 0x200 + eh.readUInt32LE(i * 16 + 8), eh.readUInt32LE(i * 16 + 12)));
        } else if (byPath.has(p)) {
          const e = byPath.get(p);
          cache.set(p, await readBlob(f, e.off, e.size));
        }
      }
    },
    file(p) {
      if (!cache.has(p)) throw new Error(`${p} fehlt`);
      return cache.get(p);
    },
    code() { return cache.get('.code') ?? null; },
  };
}

// ------------------------------------------------------------------- ZIP (ohne Kompression)
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function crc32(b) { let c = 0xffffffff; for (let i = 0; i < b.length; i++) c = CRC_TABLE[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }

function makeZip(entries) {
  const parts = [], central = [];
  let off = 0;
  for (const { name, data } of entries) {
    const n = Buffer.from(name), crc = crc32(data);
    const h = Buffer.alloc(30);
    h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4); h.writeUInt16LE(0x800, 6);
    h.writeUInt32LE(crc, 14); h.writeUInt32LE(data.length, 18); h.writeUInt32LE(data.length, 22); h.writeUInt16LE(n.length, 26);
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6); c.writeUInt16LE(0x800, 8);
    c.writeUInt32LE(crc, 16); c.writeUInt32LE(data.length, 20); c.writeUInt32LE(data.length, 24); c.writeUInt16LE(n.length, 28);
    c.writeUInt32LE(name.endsWith('/') ? 0x10 : 0, 38); c.writeUInt32LE(off, 42);
    parts.push(h, n, data);
    central.push(c, n);
    off += 30 + n.length + data.length;
  }
  const cd = Buffer.concat(central), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(off, 16);
  return Buffer.concat([...parts, cd, end]);
}

// ------------------------------------------------------------------- Ablauf
const USUM_FILES = ['a/0/3/5', 'a/0/3/2', 'a/0/1/7', 'a/0/1/4', 'a/0/6/2', '.code'];
const EXT = /\.(cia|3ds|cci|cxi|app)$/i;

/** files: File-Objekte; liefert die ZIP-Datei (Uint8Array) oder wirft mit verständlicher Meldung. */
async function erzeugeDaten(files, log) {
  let usum = null;
  const roms = [];
  let encrypted = 0;
  for (const f of files.filter(f => EXT.test(f.name))) {
    let g;
    try { g = await openGameWeb(f); } catch (e) {
      if (e.message === 'verschlüsselt') { encrypted++; log(`${f.name}: verschlüsselt`); }
      continue;
    }
    if (!usum && ['a/0/3/5', 'a/0/6/2', 'a/0/1/7', 'a/0/1/4'].every(p => g.has(p))) {
      await g.load(['a/0/3/5']);
      if (isUsum(g)) {
        log(`${f.name}: Ultrasonne/Ultramond`);
        await g.load(USUM_FILES);
        usum = g;
        continue;
      }
    }
    const romFiles = g.files.filter(x => x.path.startsWith('rom/'));
    if (romFiles.length) {
      await g.load(romFiles.map(x => x.path));
      for (const r of romFiles) {
        const data = g.file(r.path);
        const t = findGbTables(data);
        if (t.gen1 || t.gen2) { roms.push({ name: `${r.path.slice(4)} (${f.name})`, data }); log(`${f.name}: Virtual Console (${t.gen1 ? 'Gen 1' : 'Gen 2'})`); }
      }
    }
  }
  if (!usum) {
    throw new Error('Ultrasonne oder Ultramond nicht gefunden.' + (encrypted ? ` ${encrypted} Datei(en) sind verschlüsselt und müssen erst entschlüsselt werden (siehe Anleitung).` : ''));
  }
  log('Erzeuge arten.bin …');
  const arten = buildArten(usum, roms, log);
  log('Erzeuge namen.txt …');
  const namen = buildNamen(usum);
  log('Erzeuge icons.bin …');
  await new Promise(r => setTimeout(r, 0));
  const icons = buildIcons(usum);
  const zip = makeZip([
    { name: 'pokemon-world-3d/', data: Buffer.alloc(0) },
    { name: 'pokemon-world-3d/arten.bin', data: arten.data },
    { name: 'pokemon-world-3d/namen.txt', data: namen },
    { name: 'pokemon-world-3d/icons.bin', data: icons },
  ]);
  return { zip, gen1: arten.gen1, gen2: arten.gen2 };
}
