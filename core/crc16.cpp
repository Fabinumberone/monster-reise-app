#include "crc16.hpp"

namespace pw3d {

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= uint16_t(data[i]) << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

uint16_t crc16Invert(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? uint16_t((crc >> 1) ^ 0xA001) : uint16_t(crc >> 1);
    }
    return uint16_t(~crc);
}

} // namespace pw3d
