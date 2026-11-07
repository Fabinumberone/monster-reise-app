// BCH (H3D) aus X/Y und Omega Rubin/Alpha Saphir lesen.
// Nach dem Vorbild von Ohana3DS (gdkchan): Kopf, Relokation, Texturen, Modelle, Meshes.

const ATTR = ['position', 'normal', 'tangent', 'color', 'uv0', 'uv1', 'uv2', 'boneIndex', 'boneWeight'];

/** PICA-Befehlspuffer auswerten: letzter Wert je Register + Float-Uniforms */
function readCommands(b, off, words, ignoreAlign = false) {
  const reg = new Map();
  const uniforms = {};
  let cur = 0, pending = [];
  const set = (id, v, mask) => {
    const old = reg.get(id) ?? 0;
    reg.set(id, ((old & (~mask & 0xf)) | (v & (0xfffffff0 | mask))) >>> 0);
  };
  const f32 = (v) => { const t = Buffer.alloc(4); t.writeUInt32LE(v >>> 0); return t.readFloatLE(0); };
  let p = off, read = 0;
  while (read < words) {
    const param = b.readUInt32LE(p), header = b.readUInt32LE(p + 4);
    p += 8; read += 2;
    let id = header & 0xffff;
    const mask = (header >> 16) & 0xf, extra = (header >> 20) & 0x7ff, consecutive = (header & 0x80000000) !== 0;
    set(id, param, mask);
    if (id === 0x23d) break;
    if (id === 0x2c0) cur = param & 0x7fffffff;
    else if (id >= 0x2c1 && id <= 0x2c8) pending.push(f32(reg.get(id)));
    for (let i = 0; i < extra; i++) {
      if (consecutive) id++;
      set(id, b.readUInt32LE(p), mask);
      p += 4; read++;
      if (id > 0x2c0 && id < 0x2c1 + 8) pending.push(f32(reg.get(id)));
    }
    if (pending.length) { (uniforms[cur] ??= []).push(...pending); pending = []; }
    if (!ignoreAlign) while ((p - off) & 7) { p += 4; read++; }
  }
  return { get: (id) => reg.get(id) ?? 0, uniforms };
}

function cstr(b, off) {
  if (!off) return null;
  let e = off;
  while (e < b.length && b[e]) e++;
  return b.toString('latin1', off, e);
}

/** Liest ein BCH; gibt Texturen (nativ, ungekachelt nicht umgerechnet) und Modelle zurück */
export function readBch(src) {
  const b = Buffer.from(src); // Kopie, die Relokation schreibt hinein
  if (b.toString('latin1', 0, 3) !== 'BCH') throw new Error('kein BCH');
  const bc = b[4];
  let p = 8;
  const u = () => { const v = b.readUInt32LE(p); p += 4; return v; };
  const h = {};
  h.main = u(); h.strings = u(); h.cmds = u(); h.data = u(); h.dataExt = bc > 0x20 ? u() : 0; h.reloc = u();
  h.mainLen = u(); h.stringsLen = u(); h.cmdsLen = u(); h.dataLen = u(); h.dataExtLen = bc > 0x20 ? u() : 0; h.relocLen = u();

  // Relokation: relative Zeiger → absolute Offsets in der Datei
  const add = (at, v) => b.writeUInt32LE((b.readUInt32LE(at) + v) >>> 0, at);
  for (let o = h.reloc; o < h.reloc + h.relocLen; o += 4) {
    const value = b.readUInt32LE(o);
    const off = value & 0x1ffffff, flags = value >>> 25;
    switch (flags) {
      case 0: add(off * 4 + h.main, h.main); break;
      case 1: add(off + h.main, h.strings); break;
      case 2: add(off * 4 + h.main, h.cmds); break;
      case 7: case 0xc: add(off * 4 + h.main, h.data); break;
    }
    const c = off * 4 + h.cmds;
    if (c + 4 > b.length) continue;
    if (bc < 0x21) {
      if (flags === 0x25 || flags === 0x27) add(c, h.data);
      else if (flags === 0x28) b.writeUInt32LE((((b.readUInt32LE(c) + h.data) & 0x7fffffff) | 0x80000000) >>> 0, c);
      else if (flags === 0x29) b.writeUInt32LE((b.readUInt32LE(c) + h.data) & 0x7fffffff, c);
    } else {
      switch (flags) {
        case 0x25: case 0x26: add(c, h.data); break;
        case 0x27: b.writeUInt32LE((((b.readUInt32LE(c) + h.data) & 0x7fffffff) | 0x80000000) >>> 0, c); break;
        case 0x28: b.writeUInt32LE((b.readUInt32LE(c) + h.data) & 0x7fffffff, c); break;
        case 0x2b: add(c, h.dataExt); break;
        case 0x2c: b.writeUInt32LE((((b.readUInt32LE(c) + h.dataExt) & 0x7fffffff) | 0x80000000) >>> 0, c); break;
        case 0x2d: b.writeUInt32LE((b.readUInt32LE(c) + h.dataExt) & 0x7fffffff, c); break;
      }
    }
  }

  const dict = (i) => ({ table: b.readUInt32LE(h.main + i * 12), count: b.readUInt32LE(h.main + i * 12 + 4) });
  const ptrs = (d) => [...Array(d.count)].map((_, i) => b.readUInt32LE(d.table + i * 4));

  // ---------------------------------------------------------------- Texturen
  const textures = ptrs(dict(3)).map((t) => {
    const cmdOff = b.readUInt32LE(t), words = b.readUInt32LE(t + 4);
    const name = cstr(b, b.readUInt32LE(t + 0x1c));
    const c = readCommands(b, cmdOff, words);
    const size = c.get(0x82);
    const width = size >>> 16, height = size & 0xffff, format = c.get(0x8e);
    const addr = c.get(0x85);
    const bytes = (width * height * BPP[format]) / 8;
    return { name, width, height, format, data: b.subarray(addr, addr + bytes) };
  });

  // ---------------------------------------------------------------- Modelle
  const models = ptrs(dict(0)).map((m) => readModel(b, m, bc));
  const skeletalAnims = ptrs(dict(8)).map((a) => readSkeletalAnim(b, a));
  return { version: bc, textures, models, skeletalAnims };
}

export const BPP = { 0: 32, 1: 24, 2: 16, 3: 16, 4: 16, 5: 16, 6: 16, 7: 8, 8: 8, 9: 8, 10: 4, 11: 4, 12: 4, 13: 8 };

function readModel(b, m, bc) {
  let p = m;
  const u = () => { const v = b.readUInt32LE(p); p += 4; return v; };
  const f = () => { const v = b.readFloatLE(p); p += 4; return v; };
  p += 4; // flags, scaling, silhouette
  const world = [...Array(12)].map(f);
  const matTable = u(), matCount = u(); u();
  const vtxTable = u(), vtxCount = u();
  p += bc > 6 ? 0x28 : 0x20;
  const skelOff = u(), skelCount = u(); u();
  const visOff = u(); u();
  const name = cstr(b, u());
  const nodeNameCount = u(), nodeNameOff = u();

  // Namen der Objektknoten (Patricia-Baum: Wurzel + Einträge à 12 Byte)
  const nodeNames = [...Array(nodeNameCount)].map((_, i) => cstr(b, b.readUInt32LE(nodeNameOff + 12 + i * 12 + 8)));

  // Materialien: Texturnamen der drei Einheiten + Shader-Befehle (Alphatest, Blend, Cull)
  const materials = [...Array(matCount)].map((_, i) => {
    const base = matTable + i * (bc < 0x21 ? 0x58 : 0x2c);
    const params = b.readUInt32LE(base);
    const texCmd = b.readUInt32LE(base + 16), texWords = b.readUInt32LE(base + 20);
    const n = bc < 0x21 ? base + 24 + 0x30 : base + 28;
    const tex = [0, 1, 2].map((k) => cstr(b, b.readUInt32LE(n + k * 4)));
    const mname = cstr(b, b.readUInt32LE(n + 12));
    const out = { name: mname, textures: tex };
    // Texturparameter je Einheit (Wrap, Filter)
    if (texWords) {
      const tc = readCommands(b, texCmd, texWords);
      out.wrap = [0x83, 0x93, 0x9b].map((r) => { const v = tc.get(r); return [(v >> 12) & 0xf, (v >> 8) & 0xf]; });
    }
    if (params) {
      // Ab Offset 0x0 der Parameter: Hash, Flags … ; Shader-Befehle stehen nach den Farben (siehe Ohana3DS)
      const fshOff = params + 4 + 2 + 2 + 4 + 3 * 24 + 4 + 12 * 4 + 4 + 6 * 4 + 4 + 24 + 4 + 4;
      const cmdOff = b.readUInt32LE(fshOff), cmdWords = b.readUInt32LE(fshOff + 4);
      const colors = params + 4 + 2 + 2 + 4 + 3 * 24 + 4;
      const rgba = (o) => [b[o], b[o + 1], b[o + 2], b[o + 3]];
      out.diffuse = rgba(colors + 8);
      out.constants = [0, 1, 2, 3, 4, 5].map((k) => rgba(colors + 20 + k * 4));
      const constSel = b.readUInt32LE(fshOff - 8);
      out.constSel = [0, 1, 2, 3, 4, 5].map((k) => (constSel >> (k * 4)) & 0xf);
      if (cmdWords) {
        const fc = readCommands(b, cmdOff, cmdWords);
        const at = fc.get(0x104);
        out.alphaTest = { on: !!(at & 1), func: (at >> 4) & 0xf, ref: (at >> 8) & 0xff };
        out.blend = fc.get(0x101);
        out.colorOut = fc.get(0x100);
        out.depth = fc.get(0x107);
        out.cull = fc.get(0x40) & 3;
        out.tev = [0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8].map((r) => [fc.get(r), fc.get(r + 1), fc.get(r + 2), fc.get(r + 3), fc.get(r + 4)]);
        out.tevBuffer = [fc.get(0xe0), fc.get(0xfd)];
      }
    }
    return out;
  });

  // Skelett
  const bones = [...Array(skelCount)].map((_, i) => {
    let q = skelOff + i * (4 + 4 + 36 + 48 + 8);
    const flags = b.readUInt32LE(q);
    const parent = b.readInt16LE(q + 4);
    const v = (o) => [b.readFloatLE(o), b.readFloatLE(o + 4), b.readFloatLE(o + 8)];
    const scale = v(q + 8), rotation = v(q + 20), translation = v(q + 32);
    const inv = [...Array(12)].map((_, k) => b.readFloatLE(q + 44 + k * 4));
    const bname = cstr(b, b.readUInt32LE(q + 92));
    return { name: bname, parent, scale, rotation, translation, inv, billboard: (flags >> 16) & 0xf };
  });

  const visibility = b.readUInt32LE(visOff);

  // Meshes
  const meshes = [];
  for (let i = 0; i < vtxCount; i++) {
    const e = vtxTable + i * 0x38;
    const materialId = b.readUInt16LE(e), flags = b.readUInt16LE(e + 2);
    const nodeId = b.readUInt16LE(e + 4), priority = b.readUInt16LE(e + 6);
    const vshOff = b.readUInt32LE(e + 8), vshWords = b.readUInt32LE(e + 12);
    const facesOff = b.readUInt32LE(e + 16), facesCount = b.readUInt32LE(e + 20);
    if (bc !== 8 && (flags & 1)) continue; // Silhouette
    const vc = readCommands(b, vshOff, vshWords);
    const u6 = vc.uniforms[6] ?? [0, 0, 0, 0], u7 = vc.uniforms[7] ?? [];
    // Ohana liest die Werte als Stack (letzter zuerst)
    const s6 = [...u6].reverse(), s7 = [...u7].reverse();
    const posOffset = s6.slice(0, 4);
    const [tex0Scale, tex1Scale, tex2Scale, weightScale, posScale, normScale, tanScale, colScale] = s7;

    const bufAddr = vc.get(0x203), stride = (vc.get(0x205) >> 16) & 0xff, total = vc.get(0x205) >>> 28;
    let mainPerm = BigInt(vc.get(0x2bb)) | (BigInt(vc.get(0x2bc)) << 32n);
    let fmt = BigInt(vc.get(0x201)) | (BigInt(vc.get(0x202)) << 32n);
    let perm = BigInt(vc.get(0x204)) | (BigInt(vc.get(0x205) & 0xffff) << 32n);
    const attrs = [];
    let ofs = 0;
    for (let a = 0; a < total; a++) {
      const slot = Number((perm >> BigInt(a * 4)) & 0xfn);
      const kind = ATTR[Number((mainPerm >> BigInt(slot * 4)) & 0xfn)] ?? 'unknown';
      const fv = Number((fmt >> BigInt(slot * 4)) & 0xfn);
      let type = fv & 3; const count = (fv >> 2) + 1;
      if (kind === 'boneWeight') type = 1;
      const size = [1, 1, 2, 4][type];
      attrs.push({ kind, type, count, offset: ofs });
      ofs += size * count;
      if (ofs % size) ofs += size - (ofs % size);
    }

    const submeshes = [];
    for (let fIdx = 0; fIdx < facesCount; fIdx++) {
      const base = facesOff + fIdx * 0x34;
      const skinning = b.readUInt16LE(base);
      const nodeCount = b.readUInt16LE(base + 2);
      const nodes = [...Array(nodeCount)].map((_, k) => b.readUInt16LE(base + 4 + k * 2));
      const fhOff = b.readUInt32LE(base + 0x2c), fhWords = b.readUInt32LE(base + 0x30);
      const ic = readCommands(b, fhOff, fhWords);
      const cfg = ic.get(0x227);
      const iAddr = cfg & 0x7fffffff, short = (cfg >>> 31) === 1;
      const n = ic.get(0x228);
      const indices = new Uint16Array(n);
      for (let k = 0; k < n; k++) indices[k] = short ? b.readUInt16LE(iAddr + k * 2) : b[iAddr + k];
      submeshes.push({ skinning, nodes, indices });
    }

    // Vertices dekodieren (alle bis zum größten Index)
    let maxIndex = 0;
    for (const s of submeshes) for (const k of s.indices) if (k > maxIndex) maxIndex = k;
    const count = submeshes.length ? maxIndex + 1 : 0;
    const read = (o, type) => type === 0 ? b.readInt8(o) : type === 1 ? b[o] : type === 2 ? b.readInt16LE(o) : b.readFloatLE(o);
    const out = { position: new Float32Array(count * 3) };
    for (const a of attrs) {
      if (a.kind === 'normal') out.normal = new Float32Array(count * 3);
      if (a.kind === 'color') out.color = new Float32Array(count * 4);
      if (a.kind === 'uv0') out.uv0 = new Float32Array(count * 2);
      if (a.kind === 'uv1') out.uv1 = new Float32Array(count * 2);
      if (a.kind === 'boneIndex') out.boneIndex = new Uint8Array(count * 4);
      if (a.kind === 'boneWeight') out.boneWeight = new Float32Array(count * 4);
    }
    for (let v = 0; v < count; v++) {
      const vb = bufAddr + v * stride;
      for (const a of attrs) {
        const size = [1, 1, 2, 4][a.type];
        const c = [0, 0, 0, 1];
        for (let k = 0; k < a.count; k++) c[k] = read(vb + a.offset + k * size, a.type);
        switch (a.kind) {
          case 'position': for (let k = 0; k < 3; k++) out.position[v * 3 + k] = c[k] * posScale + posOffset[k]; break;
          case 'normal': for (let k = 0; k < 3; k++) out.normal[v * 3 + k] = c[k] * normScale; break;
          case 'color': for (let k = 0; k < 4; k++) out.color[v * 4 + k] = Math.min(1, Math.max(0, c[k] * colScale)); break;
          case 'uv0': out.uv0[v * 2] = c[0] * tex0Scale; out.uv0[v * 2 + 1] = c[1] * tex0Scale; break;
          case 'uv1': out.uv1[v * 2] = c[0] * tex1Scale; out.uv1[v * 2 + 1] = c[1] * tex1Scale; break;
          case 'boneIndex': for (let k = 0; k < a.count; k++) out.boneIndex[v * 4 + k] = c[k]; break;
          case 'boneWeight': for (let k = 0; k < a.count; k++) out.boneWeight[v * 4 + k] = c[k] * weightScale; break;
        }
      }
    }
    meshes.push({
      materialId, nodeId, priority, name: nodeNames[nodeId] ?? `mesh${i}`, visible: !!(visibility & (1 << nodeId)),
      attrs, stride, raw: b.subarray(bufAddr, bufAddr + count * stride),
      scales: { posOffset, posScale, normScale, colScale, tex0Scale, tex1Scale, weightScale },
      vertices: out, submeshes,
    });
  }
  return { name, world, materials, bones, meshes };
}

// Skelettanimationen kommen in einem eigenen Schritt (Flemmli)
function readSkeletalAnim(b, a) {
  return { offset: a, name: cstr(b, b.readUInt32LE(a)) };
}
