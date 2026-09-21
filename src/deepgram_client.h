#ifndef DEEPGRAM_CLIENT_H
#define DEEPGRAM_CLIENT_H

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "wav_header.h"

class DeepgramClient {
private:
    const char* api_key;
    String defaultLanguage;
    DynamicJsonDocument* stt_doc;
    char* response_buffer;

    static const size_t STT_DOC_SIZE = 2048;
    static const size_t RESPONSE_BUFFER_SIZE = 2048;
    
    // Helper function to create WAV data from raw PCM
    uint8_t* createWAVData(const uint8_t* pcm_data, size_t pcm_size, size_t* wav_size);
    
    // Helper function to extract transcript from Deepgram response
    String extractTranscript(const String& response);
    
    // Helper function to extract search results from Deepgram response
    // TODO INCREASE CONFIDENCE
    bool extractSearchResults(const String& response, const String& searchTerm, float minConfidence = 0.80f);

public:
    DeepgramClient(const char* api_key);
    ~DeepgramClient();
    bool begin();
    String transcribe(const uint8_t* audio_data, size_t data_size);
    String transcribe(const uint8_t* audio_data, size_t data_size, const String& language);
    
    // Search for specific terms/phrases in audio (for wake word detection)
    bool searchForWakeWords(const uint8_t* audio_data, size_t data_size, const char* wakeWords[], int wakeWordCount, float minConfidence = 0.5);
    
    // Set default language for transcription
    void setDefaultLanguage(const String& language);
};

#endif