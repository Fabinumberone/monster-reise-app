// PICA200-Texturen (3DS): 8×8-Kacheln, innerhalb der Kachel Morton-Reihenfolge,
// Zeilen von unten nach oben. Hier nur RGBA5551 (Format 7 in BFLIM), mehr wird nicht gebraucht.

function mortonXY(i) {
  let x = 0, y = 0;
  for (let b = 0; b < 3; b++) { x |= ((i >> (2 * b)) & 1) << b; y |= ((i >> (2 * b + 1)) & 1) << b; }
  return [x, y];
}

/** Dekodiert gekachelte RGBA5551-Daten (w×h) in RGBA8888 (Zeilen von oben nach unten). */
export function decodeRgba5551(data, w, h) {
  const out = Buffer.alloc(w * h * 4);
  let p = 0;
  for (let ty = 0; ty < h; ty += 8) for (let tx = 0; tx < w; tx += 8) for (let i = 0; i < 64; i++) {
    const [x, y] = mortonXY(i);
    const v = data.readUInt16LE(p); p += 2;
    const px = tx + x, py = h - 1 - (ty + y); // PICA: unten beginnend
    const o = (py * w + px) * 4;
    out[o] = ((v >> 11) & 31) * 255 / 31; out[o + 1] = ((v >> 6) & 31) * 255 / 31;
    out[o + 2] = ((v >> 1) & 31) * 255 / 31; out[o + 3] = (v & 1) * 255;
  }
  return out;
}

/** RGBA8888 (Zeilen von oben) in gekachelte RGBA5551-Daten kodieren (Umkehrung von decodeRgba5551). */
export function encodeRgba5551(px, w, h) {
  const out = Buffer.alloc(w * h * 2);
  let p = 0;
  for (let ty = 0; ty < h; ty += 8) for (let tx = 0; tx < w; tx += 8) for (let i = 0; i < 64; i++) {
    const [x, y] = mortonXY(i);
    const o = ((h - 1 - (ty + y)) * w + tx + x) * 4;
    const v = ((px[o] >> 3) << 11) | ((px[o + 1] >> 3) << 6) | ((px[o + 2] >> 3) << 1) | (px[o + 3] >= 128 ? 1 : 0);
    out.writeUInt16LE(v, p); p += 2;
  }
  return out;
}

/** Bild (RGBA8888) um 90° gegen den Uhrzeigersinn drehen: w×h -> h×w. */
export function rotateCCW(px, w, h) {
  const out = Buffer.alloc(px.length);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++)
    px.copy(out, ((w - 1 - x) * h + y) * 4, (y * w + x) * 4, (y * w + x) * 4 + 4);
  return out;
}
