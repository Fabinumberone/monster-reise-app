// Textdateien der 3DS-Pokémon-Spiele (Gen 6/7): eine Sektion mit Zeilentabelle,
// jede Zeile UTF-16LE und mit einem pro Zeile wechselnden 16-Bit-Schlüssel verXORt
// (Start 0x7C89, je Zeile + 0x2983, je Zeichen um 3 Bit nach links rotiert).

/** Dekodiert eine Textdatei und gibt die Zeilen als Strings zurück. */
export function readGfText(b) {
  const sections = b.readUInt16LE(0);
  const lines = b.readUInt16LE(2);
  const sectionOff = b.readUInt32LE(0x0c);
  if (sections !== 1 || sectionOff + 4 > b.length) throw new Error('kein Gen-6/7-Text');
  const out = [];
  let key = 0x7c89;
  for (let i = 0; i < lines; i++) {
    const e = sectionOff + 4 + i * 8;
    const off = sectionOff + b.readUInt32LE(e);
    const len = b.readUInt16LE(e + 4);
    let k = key, s = '';
    for (let c = 0; c < len; c++) {
      const ch = b.readUInt16LE(off + c * 2) ^ k;
      k = ((k << 3) | (k >>> 13)) & 0xffff;
      if (ch === 0) break;
      s += String.fromCharCode(ch);
    }
    out.push(fixChars(s));
    key = (key + 0x2983) & 0xffff;
  }
  return out;
}

/** Sonderzeichen der Spiele auf Unicode abbilden; Steuersequenzen (Variablen) entfernen. */
function fixChars(s) {
  return s
    .replace(/⑩/g, '')            // Zeilenformat-Steuerung
    .replace(//g, '♂').replace(//g, '♀')
    .replace(/\u0010[\s\S]{0,8}?/g, '') // Variablen (grob)
    .replace(//g, ' ');
}
