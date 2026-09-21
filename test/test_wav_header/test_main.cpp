#include <unity.h>
#include <string.h>
#include "wav_header.h"

void setUp() {}
void tearDown() {}

static uint32_t le32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

void test_header_is_44_bytes() {
    TEST_ASSERT_EQUAL_size_t(44, sizeof(WAVHeader));
}

void test_header_layout() {
    const size_t pcm_size = 96000; // 3 seconds, the wake word buffer
    WAVHeader header = make_wav_header(pcm_size);
    uint8_t bytes[sizeof(WAVHeader)];
    memcpy(bytes, &header, sizeof(bytes));

    TEST_ASSERT_EQUAL_MEMORY("RIFF", bytes, 4);
    TEST_ASSERT_EQUAL_UINT32(pcm_size + 36, le32(bytes + 4));
    TEST_ASSERT_EQUAL_MEMORY("WAVE", bytes + 8, 4);
    TEST_ASSERT_EQUAL_MEMORY("fmt ", bytes + 12, 4);
    TEST_ASSERT_EQUAL_UINT32(16, le32(bytes + 16));
    TEST_ASSERT_EQUAL_UINT16(1, le16(bytes + 20));
    TEST_ASSERT_EQUAL_UINT16(1, le16(bytes + 22));
    TEST_ASSERT_EQUAL_UINT32(16000, le32(bytes + 24));
    TEST_ASSERT_EQUAL_UINT32(32000, le32(bytes + 28));
    TEST_ASSERT_EQUAL_UINT16(2, le16(bytes + 32));
    TEST_ASSERT_EQUAL_UINT16(16, le16(bytes + 34));
    TEST_ASSERT_EQUAL_MEMORY("data", bytes + 36, 4);
    TEST_ASSERT_EQUAL_UINT32(pcm_size, le32(bytes + 40));
}

void test_empty_pcm() {
    WAVHeader header = make_wav_header(0);
    TEST_ASSERT_EQUAL_UINT32(36, header.chunk_size);
    TEST_ASSERT_EQUAL_UINT32(0, header.data_size);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_header_is_44_bytes);
    RUN_TEST(test_header_layout);
    RUN_TEST(test_empty_pcm);
    return UNITY_END();
}
