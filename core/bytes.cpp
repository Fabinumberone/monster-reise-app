#include "bytes.hpp"

#include <cstring>

namespace pw3d {

std::string utf16ToUtf8(const uint8_t* p, size_t maxChars) {
    std::string out;
    for (size_t i = 0; i < maxChars; i++) {
        uint32_t c = rd16(p + i * 2);
        if (c == 0) break;
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

void utf8ToUtf16(const std::string& s, uint8_t* p, size_t maxChars) {
    std::memset(p, 0, maxChars * 2);
    size_t n = 0;
    for (size_t i = 0; i < s.size() && n + 1 < maxChars;) {
        uint8_t b = uint8_t(s[i]);
        uint32_t c;
        if (b < 0x80) { c = b; i += 1; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) { c = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; }
        else if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) { c = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3; }
        else { c = '?'; i += 1; }
        wr16(p + n * 2, uint16_t(c));
        n++;
    }
}

} // namespace pw3d
