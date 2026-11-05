#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace KlipperTransfer {
// Plazo total, incluso si el servidor mantiene abierto el socket sin enviar datos.
// read() es no bloqueante porque solo se pide lo que available() anuncia.
template <typename Stream, typename Connected, typename Clock, typename Pause>
constexpr bool readExact(Stream& stream, uint8_t* buffer, size_t length,
               Connected connected, Clock clock, Pause pause, uint32_t timeoutMs) {
    const uint32_t started = clock();
    size_t offset = 0;
    while (offset < length) {
        if (static_cast<uint32_t>(clock() - started) >= timeoutMs) return false;
        const int available = stream.available();
        if (available > 0) {
            const size_t remaining = length - offset;
            const size_t chunk = static_cast<size_t>(available) < remaining ? available : remaining;
            const int received = stream.read(buffer + offset, chunk);
            if (received <= 0 || static_cast<size_t>(received) > chunk) return false;
            offset += received;
        } else if (!connected()) {
            return false;
        }
        pause();
    }
    return true;
}

constexpr uint32_t pngU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
constexpr bool isThumbnailPng(const uint8_t* data, size_t length) {
    constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    constexpr uint8_t header[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    constexpr uint8_t end[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D'};
    if (!data || length < 45 || length > 128 * 1024) return false;
    for (size_t i = 0; i < 8; ++i) {
        if (data[i] != signature[i] || data[i + 8] != header[i] || data[length - 12 + i] != end[i]) return false;
    }
    const uint32_t width = pngU32(data + 16), height = pngU32(data + 20);
    // Selection accepts only the historical 100 or preferred 300 thumbnail.
    // Decoder allocation is additionally bounded to 320x320 (409600 RGBA bytes).
    return width == height && (width == 100 || width == 300) &&
           width <= 320 && height <= 320;
}
} // namespace KlipperTransfer
