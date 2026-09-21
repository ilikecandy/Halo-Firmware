#include <unity.h>
#include <string.h>
#include "base64_core.h"

void setUp() {}
void tearDown() {}

static void expect_encode(const char* input, const char* expected) {
    char out[64];
    size_t n = base64_encode_to_buffer((const uint8_t*)input, strlen(input), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(strlen(expected), n);
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

void test_encode_rfc4648_vectors() {
    expect_encode("", "");
    expect_encode("f", "Zg==");
    expect_encode("fo", "Zm8=");
    expect_encode("foo", "Zm9v");
    expect_encode("foob", "Zm9vYg==");
    expect_encode("fooba", "Zm9vYmE=");
    expect_encode("foobar", "Zm9vYmFy");
}

void test_encode_needs_room_for_terminator() {
    char out[8];
    TEST_ASSERT_EQUAL_size_t(0, base64_encode_to_buffer((const uint8_t*)"foo", 3, out, 4));
    TEST_ASSERT_EQUAL_size_t(4, base64_encode_to_buffer((const uint8_t*)"foo", 3, out, 5));
}

void test_decode_rfc4648_vectors() {
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(1, base64_decode_to_buffer("Zg==", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("f", out, 1);
    TEST_ASSERT_EQUAL_size_t(5, base64_decode_to_buffer("Zm9vYmE=", 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("fooba", out, 5);
    TEST_ASSERT_EQUAL_size_t(6, base64_decode_to_buffer("Zm9vYmFy", 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("foobar", out, 6);
}

void test_decode_skips_line_breaks() {
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(6, base64_decode_to_buffer("Zm9v\r\nYmFy", 10, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("foobar", out, 6);
}

void test_decode_rejects_invalid_character() {
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, base64_decode_to_buffer("Zm9*", 4, out, sizeof(out)));
}

void test_decode_rejects_small_buffer() {
    uint8_t out[5];
    TEST_ASSERT_EQUAL_size_t(0, base64_decode_to_buffer("Zm9vYmFy", 8, out, sizeof(out)));
}

void test_round_trip_all_byte_values() {
    uint8_t input[256];
    for (int i = 0; i < 256; i++) {
        input[i] = (uint8_t)i;
    }
    for (size_t len = 1; len <= sizeof(input); len++) {
        char encoded[400];
        uint8_t decoded[256];
        size_t n = base64_encode_to_buffer(input, len, encoded, sizeof(encoded));
        TEST_ASSERT_EQUAL_size_t(((len + 2) / 3) * 4, n);
        TEST_ASSERT_EQUAL_size_t(len, base64_decode_to_buffer(encoded, n, decoded, sizeof(decoded)));
        TEST_ASSERT_EQUAL_MEMORY(input, decoded, len);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_rfc4648_vectors);
    RUN_TEST(test_encode_needs_room_for_terminator);
    RUN_TEST(test_decode_rfc4648_vectors);
    RUN_TEST(test_decode_skips_line_breaks);
    RUN_TEST(test_decode_rejects_invalid_character);
    RUN_TEST(test_decode_rejects_small_buffer);
    RUN_TEST(test_round_trip_all_byte_values);
    return UNITY_END();
}
