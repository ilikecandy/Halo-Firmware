#include "base64_core.h"

#include <string.h>

const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode_to_buffer(const uint8_t *data, size_t len, char *buffer, size_t bufferSize) {
    size_t encoded_len = ((len + 2) / 3) * 4;
    if (bufferSize < encoded_len + 1) {
        return 0; // Not enough space
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = 0;
        for (int j = 0; j < 3; ++j) {
            val <<= 8;
            if (i + j < len) {
                val |= data[i + j];
            }
        }
        buffer[out_idx++] = b64_alphabet[(val >> 18) & 0x3F];
        buffer[out_idx++] = b64_alphabet[(val >> 12) & 0x3F];
        buffer[out_idx++] = (i + 1 < len) ? b64_alphabet[(val >> 6) & 0x3F] : '=';
        buffer[out_idx++] = (i + 2 < len) ? b64_alphabet[val & 0x3F] : '=';
    }
    buffer[encoded_len] = '\0';
    return encoded_len;
}

size_t base64_decode_to_buffer(const char *data, size_t len, uint8_t *buffer, size_t bufferSize) {
    size_t out_idx = 0;
    uint32_t val = 0;
    int bits = 0;

    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        if (c == '=') {
            break;
        }
        if (c == '\r' || c == '\n') {
            continue;
        }
        const char *p = strchr(b64_alphabet, c);
        if (p == nullptr) {
            return 0; // Invalid character
        }
        val = (val << 6) | (uint32_t)(p - b64_alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_idx >= bufferSize) {
                return 0; // Not enough space
            }
            buffer[out_idx++] = (val >> bits) & 0xFF;
        }
    }
    return out_idx;
}
