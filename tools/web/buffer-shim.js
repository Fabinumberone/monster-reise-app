// Kleiner Ersatz für Node-Buffer im Browser (nur was die Bibliotheken in tools/lib brauchen).
class Buffer extends Uint8Array {
  static alloc(n) { return new Buffer(n); }
  static isBuffer(b) { return b instanceof Buffer; }
  static from(x, enc) {
    if (typeof x === 'string') {
      if (enc === 'latin1') { const b = new Buffer(x.length); for (let i = 0; i < x.length; i++) b[i] = x.charCodeAt(i) & 255; return b; }
      const u = new TextEncoder().encode(x);
      return new Buffer(u.buffer, u.byteOffset, u.length);
    }
    if (x instanceof ArrayBuffer) return new Buffer(x);
    const b = new Buffer(x.length);
    b.set(x);
    return b;
  }
  static concat(list) {
    let n = 0;
    for (const b of list) n += b.length;
    const out = new Buffer(n);
    let p = 0;
    for (const b of list) { out.set(b, p); p += b.length; }
    return out;
  }
  readUInt16LE(o = 0) { return this[o] | (this[o + 1] << 8); }
  readUInt32LE(o = 0) { return (this[o] | (this[o + 1] << 8) | (this[o + 2] << 16)) + this[o + 3] * 0x1000000; }
  readUIntLE(o, n) { let v = 0; for (let i = 0; i < n; i++) v += this[o + i] * 2 ** (8 * i); return v; }
  readBigUInt64LE(o) { return BigInt(this.readUInt32LE(o)) + (BigInt(this.readUInt32LE(o + 4)) << 32n); }
  writeUInt16LE(v, o = 0) { this[o] = v & 255; this[o + 1] = (v >> 8) & 255; return o + 2; }
  writeUInt32LE(v, o = 0) { this[o] = v & 255; this[o + 1] = (v >>> 8) & 255; this[o + 2] = (v >>> 16) & 255; this[o + 3] = (v >>> 24) & 255; return o + 4; }
  write(s, o = 0) { for (let i = 0; i < s.length; i++) this[o + i] = s.charCodeAt(i) & 255; return s.length; }
  toString(enc = 'utf8', s = 0, e = this.length) {
    const sub = this.subarray(s, e);
    if (enc === 'latin1') { let r = ''; for (let i = 0; i < sub.length; i++) r += String.fromCharCode(sub[i]); return r; }
    return new TextDecoder(enc === 'utf16le' ? 'utf-16le' : 'utf-8').decode(sub);
  }
  indexOf(v, from = 0) {
    if (typeof v === 'number') return super.indexOf(v, from);
    for (let i = super.indexOf(v[0], from); i >= 0 && i + v.length <= this.length; i = super.indexOf(v[0], i + 1)) {
      let k = 1;
      while (k < v.length && this[i + k] === v[k]) k++;
      if (k === v.length) return i;
    }
    return -1;
  }
  copy(t, ts = 0, ss = 0, se = this.length) { t.set(this.subarray(ss, se), ts); return se - ss; }
}
