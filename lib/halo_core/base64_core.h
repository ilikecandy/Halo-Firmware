#ifndef BASE64_CORE_H
#define BASE64_CORE_H

#include <stddef.h>
#include <stdint.h>

extern const char b64_alphabet[];

size_t base64_encode_to_buffer(const uint8_t *data, size_t len, char *buffer, size_t bufferSize);
size_t base64_decode_to_buffer(const char *data, size_t len, uint8_t *buffer, size_t bufferSize);

#endif
