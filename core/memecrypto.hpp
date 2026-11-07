// Signatur der Gen-7-Spielstände (Sonne/Mond, Ultrasonne/Ultramond).
//
// Verfahren nach PKHeX (MemeCrypto.cs, MemeKey.cs; GPLv3), an einem echten Ultramond-Spielstand
// bestätigt: Neu-Signieren des unveränderten Spielstands ergibt byte-genau die Originalsignatur.
//
// 0x80-Byte-Bereich im Signaturblock (USUM 0x6C100, SM 0x6BB00):
//   [0x00..0x20] SHA-256 über die Blocktabelle (Dateiende−0x200, USUM 0x150 Byte, SM 0x140 Byte)
//   [0x20..0x80] RSA(AES-verschlüsselt( Nullen … , SHA-1[0..8] ))
#pragma once
#include <cstdint>
#include <vector>

namespace pw3d {

// Signiert den Spielstand neu (Größe 0x6BE00 oder 0x6CC00). false bei unbekannter Größe.
bool signSave7(std::vector<uint8_t>& sav);

// Prüft die vorhandene Signatur gegen die aktuelle Blocktabelle.
bool verifySave7(const std::vector<uint8_t>& sav);

} // namespace pw3d
