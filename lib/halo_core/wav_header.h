#ifndef WAV_HEADER_H
#define WAV_HEADER_H

#include <stddef.h>
#include <stdint.h>

struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 1;
    uint32_t sample_rate = 16000;
    uint32_t byte_rate = 32000; // sample_rate * num_channels * bits_per_sample / 8
    uint16_t block_align = 2; // num_channels * bits_per_sample / 8
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};

// Header for 16 kHz mono 16-bit PCM of the given size in bytes
inline WAVHeader make_wav_header(size_t pcm_size) {
    WAVHeader header;
    header.chunk_size = sizeof(WAVHeader) + pcm_size - 8;
    header.data_size = pcm_size;
    header.sample_rate = 16000;
    header.byte_rate = 16000 * 2;
    header.block_align = 2;
    return header;
}

#endif
