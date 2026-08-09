#include "TTS.h"
#include "secrets.h"
#include "base64.h"
#include "libhelix-mp3/mp3dec.h"

const char* TTS::DEEPGRAM_URL = "https://api.deepgram.com/v1/speak?encoding=linear16&sample_rate=16000&model=aura-asteria-en";

TTS::TTS() : i2sInitialized(false), softwareGain(1.0), audioBuffer(nullptr), defaultLanguage("en-US"), is_cancellation_requested(false) {
}

TTS::~TTS() {
    releaseSpeakerAccess();
    if (audioBuffer) {
        free(audioBuffer);
    }
}

bool TTS::initialize(const String& apiKey) {
    Serial.println("Initializing TTS...");
    
    // Store API key
    deepgramApiKey = apiKey;
    
    // Allocate audio buffer in PSRAM
    if (!audioBuffer) {
        audioBuffer = (uint8_t*)ps_malloc(BUFFER_SIZE);
        if (!audioBuffer) {
            Serial.println("Failed to allocate TTS audio buffer in PSRAM!");
            return false;
        }
        Serial.printf("Allocated %d bytes for TTS audio buffer in PSRAM\n", BUFFER_SIZE);
    }
    
    // Optimize WiFi for maximum speed
    optimizeWiFiForSpeed();
    
    
    Serial.println("TTS initialized successfully!");
    return true;
}

bool TTS::speakText(const String& text) {
    return speakText(text, defaultLanguage);
}

bool TTS::speakText(const String& text, const String& language) {
    if (text.isEmpty()) {
        Serial.println("TTS: Empty text provided");
        return false;
    }
    
    // Reset cancellation flag at the start of a new speech request
    is_cancellation_requested = false;
    
    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ TTS: WiFi not connected - cannot proceed");
        Serial.printf("WiFi status: %d\n", WiFi.status());
        return false;
    }
    
    // Request I2S access for speaker
    if (!requestSpeakerAccess()) {
        Serial.println("❌ Cannot speak: I2S is busy with another device");
        return false;
    }
    
    Serial.printf("TTS: Speaking text: %s (language: %s)\n", text.c_str(), language.c_str());
    
    // Download entire audio into memory before playing
    uint8_t* audioData = nullptr;
    size_t dataSize = 0;

    unsigned long startTime = millis();

    if (hasDeepgramVoice(language)) {
        Serial.println("🔄 Requesting audio synthesis from Deepgram...");
        if (!callDeepgramAPI(text, language, &audioData, &dataSize)) {
            Serial.println("TTS: Failed to download audio from Deepgram");
            releaseSpeakerAccess();
            return false;
        }
    } else if (googleTtsApiKey.length() >= 10 && hasCloudTtsVoice(language)) {
        Serial.println("🔄 Requesting audio synthesis from Google TTS...");
        if (!callGoogleTtsAPI(text, language, &audioData, &dataSize)) {
            Serial.println("⚠️ Google TTS failed - falling back to Google Translate TTS");
            if (!callGoogleTranslateTtsAPI(text, language, &audioData, &dataSize)) {
                Serial.println("TTS: Failed to download audio from Google Translate TTS");
                releaseSpeakerAccess();
                return false;
            }
        }
    } else {
        Serial.println("🔄 Requesting audio synthesis from Google Translate TTS...");
        if (!callGoogleTranslateTtsAPI(text, language, &audioData, &dataSize)) {
            Serial.println("TTS: Failed to download audio from Google Translate TTS");
            releaseSpeakerAccess();
            return false;
        }
    }
    
    unsigned long downloadTime = millis() - startTime;
    Serial.printf("🎵 Audio ready! Size: %u bytes, Download time: %lu ms\n", dataSize, downloadTime);
    
    // Play the downloaded audio
    Serial.println("▶️ Starting audio playback...");
    unsigned long playbackStartTime = millis();
    bool playResult = playAudioData(audioData, dataSize);
    unsigned long totalTime = millis() - startTime;
    
    if (is_cancellation_requested) {
        Serial.println("🚫 TTS playback cancelled");
    } else if (playResult) {
        Serial.printf("✅ TTS complete! Total time: %lu ms\n", totalTime);
    } else {
        Serial.println("❌ TTS playback failed");
    }
    
    // Clean up allocated memory
    cleanupAudioData(audioData);
    
    // Always release I2S access after speaking
    releaseSpeakerAccess();
    
    return playResult && !is_cancellation_requested;
}

bool TTS::streamDeepgramAPI(const String& text, const String& language) {
    Serial.printf("🤖 Synthesizing with Deepgram TTS (streaming): \"%s\" (language: %s)\n", text.c_str(), language.c_str());

    // Request I2S access for speaker, forcefully if necessary
    if (!requestSpeakerAccess()) {
        Serial.println("TTS Streaming: Forcing I2S release for speaker...");
        I2SManager::forceReleaseI2SAccess();
        if (!requestSpeakerAccess()) {
            Serial.println("❌ Cannot stream: Failed to get speaker access even after force release");
            return false;
        }
    }

    if (deepgramApiKey.length() < 10) {
        Serial.println("❌ Deepgram API key is not set or too short");
        return false;
    }

    // Check if audioBuffer is allocated
    if (!audioBuffer) {
        Serial.println("❌ TTS audioBuffer not allocated");
        return false;
    }

    // Create local copies to avoid String scope issues
    String localText = String(text);
    String localLanguage = String(language);
    String localApiKey = String(deepgramApiKey);

    String jsonPayload = "{\"text\":\"" + localText + "\"}";
    Serial.println("TTS JSON Request:");
    Serial.println(jsonPayload);

    HTTPClient http;
    
    WiFiClientSecure client;
    client.setInsecure();
    
    // Check WiFi connection before configuring client
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected - cannot proceed with TTS request");
        return false;
    }

    // Build URL with language parameter - use local copies
    String deepgramUrl = "https://api.deepgram.com/v1/speak?encoding=linear16&sample_rate=16000";
    
    // Add model based on language
    if (localLanguage == "es" || localLanguage == "spanish") {
        deepgramUrl += "&model=aura-asteria-es";
    } else if (localLanguage == "fr" || localLanguage == "french") {
        deepgramUrl += "&model=aura-asteria-fr";
    } else if (localLanguage == "de" || localLanguage == "german") {
        deepgramUrl += "&model=aura-asteria-de";
    } else if (localLanguage == "pt" || localLanguage == "portuguese") {
        deepgramUrl += "&model=aura-asteria-pt";
    } else if (localLanguage == "it" || localLanguage == "italian") {
        deepgramUrl += "&model=aura-asteria-it";
    } else {
        // Default to English
        deepgramUrl += "&model=aura-asteria-en";
    }

    http.begin(client, deepgramUrl);
    http.addHeader("Content-Type", "application/json");
    String authHeader = "Token " + localApiKey;
    http.addHeader("Authorization", authHeader);
    http.addHeader("Accept-Encoding", "identity");  // Disable compression to reduce CPU load
    http.setTimeout(60000);  // 1 minute timeout (max for uint16_t)
    http.setReuse(false);  // Don't reuse connections to avoid potential issues

    int httpCode = http.POST(jsonPayload);
    Serial.printf("Deepgram TTS HTTP Response Code: %d\n", httpCode);

    bool success = false;

    if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
            Serial.println("❌ Failed to get stream pointer from HTTPClient");
            http.end();
            return false;
        }

        Serial.println("✅ Starting to stream and play audio data...");
        
        // Clear I2S DMA buffer before starting
        i2s_zero_dma_buffer(I2S_PORT);
        
        size_t totalBytesReceived = 0;
        size_t bytesWrittenToI2S = 0;
        unsigned long lastDataTime = millis();
        const unsigned long streamReadTimeout = 60000;  // 60 second timeout

        // Stream and play audio in real-time
        while (http.connected() && (stream->available() || (millis() - lastDataTime < streamReadTimeout))) {
            if (is_cancellation_requested) {
                Serial.println("🚫 TTS streaming cancelled by request");
                break;
            }
            if (!http.connected()) {
                Serial.println("❌ HTTP connection lost during stream");
                break;
            }
            
            if (stream->available()) {
                size_t bytesRead = stream->readBytes(audioBuffer, BUFFER_SIZE);
                if (bytesRead > 0) {
                    totalBytesReceived += bytesRead;
                    
                    // Write directly to I2S - the audio is already in the right format
                    size_t bytesWritten;
                    esp_err_t err = i2s_write(I2S_PORT, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
                    if (err != ESP_OK) {
                        Serial.printf("❌ I2S write error: %s\n", esp_err_to_name(err));
                        break;
                    }
                    
                    if (bytesWritten < bytesRead) {
                        Serial.printf("⚠️ I2S underrun: tried to write %u, only wrote %u\n", bytesRead, bytesWritten);
                    }
                    
                    bytesWrittenToI2S += bytesWritten;
                    lastDataTime = millis();
                    
                    // Debug output every 8KB
                    if ((totalBytesReceived / 8192) != ((totalBytesReceived - bytesRead) / 8192)) {
                        Serial.printf("🔊 Streamed %u bytes so far...\n", totalBytesReceived);
                    }
                }
            } else {
                yield();
                delay(10);  // Small delay when no data available
            }
        }

        Serial.printf("✅ Finished streaming. Received: %u bytes, Sent to I2S: %u bytes\n", 
                     totalBytesReceived, bytesWrittenToI2S);
        
        if (totalBytesReceived > 0) {
            // Add silence padding to prevent static at the end
            size_t silenceDuration = SAMPLE_RATE * 2 * 0.1;  // 100ms of silence (16-bit samples)
            uint8_t* silenceBuffer = (uint8_t*)ps_calloc(silenceDuration, 1);  // Zero-filled buffer
            if (silenceBuffer != nullptr) {
                size_t silenceWritten;
                esp_err_t err = i2s_write(I2S_PORT, silenceBuffer, silenceDuration, &silenceWritten, 1000);
                if (err == ESP_OK) {
                    Serial.println("🔇 Added silence padding to prevent static");
                }
                free(silenceBuffer);
            }
            
            // Calculate approximate playback duration and wait
            unsigned long estimatedDurationMs = (bytesWrittenToI2S * 1000) / (SAMPLE_RATE * 2);  // 16-bit samples
            unsigned long waitTime = estimatedDurationMs * 3;  // Multiply by 3 to prevent static
            
            Serial.printf("Waiting %lu ms for audio playback to complete...\n", waitTime);
            delay(waitTime);
            
            // Gracefully stop audio output
            Serial.println("🔇 Gracefully stopping audio output...");
            i2s_zero_dma_buffer(I2S_PORT);
            delay(50);  // Small delay to ensure clean stop
        }
        
        success = (totalBytesReceived > 0);
    } else {
        Serial.printf("❌ Deepgram TTS request failed. HTTP Code: %d\n", httpCode);
        String errorPayload = http.getString();
        if (errorPayload.length() > 0) {
            Serial.println("Error payload:");
            Serial.println(errorPayload);
        }
    }
    
    http.end();
    releaseSpeakerAccess();
    return success;
}

bool TTS::ensureInitialized() {
    if (i2sInitialized) {
        return true;
    }
    
    if (deepgramApiKey.isEmpty()) {
        Serial.println("TTS: No API key set for lazy initialization");
        return false;
    }
    
    Serial.println("TTS: Attempting lazy initialization...");
    return initializeI2S();
}

bool TTS::initializeI2S() {
    Serial.println("Initializing I2S for MAX98357A via I2SManager");
    
    // Use I2SManager to initialize speaker I2S
    esp_err_t err = I2SManager::initializeSpeaker();
    if (err != ESP_OK) {
        Serial.printf("Failed to initialize I2S via I2SManager: %s\n", esp_err_to_name(err));
        return false;
    }
    
    i2sInitialized = true;
    Serial.println("I2S initialized successfully via I2SManager!");
    
    return true;
}

bool TTS::requestSpeakerAccess() {
    if (I2SManager::hasI2SAccess(I2SDevice::SPEAKER)) {
        // Already have access
        return true;
    }
    
    if (!I2SManager::requestI2SAccess(I2SDevice::SPEAKER)) {
        return false;
    }
    
    // Initialize I2S for speaker use
    if (!initializeI2S()) {
        I2SManager::releaseI2SAccess(I2SDevice::SPEAKER);
        return false;
    }
    
    return true;
}

void TTS::releaseSpeakerAccess() {
    if (I2SManager::hasI2SAccess(I2SDevice::SPEAKER)) {
        i2sInitialized = false;
        I2SManager::releaseI2SAccess(I2SDevice::SPEAKER);
    }
}

bool TTS::callDeepgramAPI(const String& text, uint8_t** audioData, size_t* dataSize) {
    return callDeepgramAPI(text, defaultLanguage, audioData, dataSize);
}

bool TTS::callDeepgramAPI(const String& text, const String& language, uint8_t** audioData, size_t* dataSize) {
    Serial.printf("🤖 Calling Deepgram TTS API: \"%s\" (language: %s)\n", text.c_str(), language.c_str());

    if (deepgramApiKey.length() < 10) {
        Serial.println("❌ Deepgram API key is not set or too short");
        return false;
    }

    // Check available memory before proceeding
    size_t freeHeap = ESP.getFreeHeap();
    size_t freePsram = psramFound() ? ESP.getFreePsram() : 0;
    Serial.printf("🔧 Memory check: Free heap=%u, Free PSRAM=%u\n", freeHeap, freePsram);
    
    if (freeHeap < 50000) { // Need at least 50KB free heap for SSL
        Serial.printf("❌ Insufficient heap memory for TTS: %u bytes (need 50KB+)\n", freeHeap);
        return false;
    }

    // Create local copies to avoid String scope issues
    String localText = String(text);
    String localLanguage = String(language);
    String localApiKey = String(deepgramApiKey);
    
    String jsonPayload = "{\"text\":\"" + localText + "\"}";
    
    HTTPClient http;
    
    WiFiClientSecure client;
    client.setInsecure();
    
    // Check WiFi connection before configuring client
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected - cannot proceed with TTS request");
        return false;
    }
    
    // Build URL with language parameter - use local copies
    String deepgramUrl = "https://api.deepgram.com/v1/speak?encoding=linear16&sample_rate=16000";
    
    // Add model based on language
    if (localLanguage == "es" || localLanguage == "spanish") {
        deepgramUrl += "&model=aura-asteria-es";
    } else if (localLanguage == "fr" || localLanguage == "french") {
        deepgramUrl += "&model=aura-asteria-fr";
    } else if (localLanguage == "de" || localLanguage == "german") {
        deepgramUrl += "&model=aura-asteria-de";
    } else if (localLanguage == "pt" || localLanguage == "portuguese") {
        deepgramUrl += "&model=aura-asteria-pt";
    } else if (localLanguage == "it" || localLanguage == "italian") {
        deepgramUrl += "&model=aura-asteria-it";
    } else {
        // Default to English
        deepgramUrl += "&model=aura-asteria-en";
    }

    Serial.printf("🔧 Memory before HTTP begin: %u bytes\n", ESP.getFreeHeap());
    
    if (!http.begin(client, deepgramUrl)) {
        Serial.println("❌ Failed to begin HTTP connection");
        return false;
    }
    
    Serial.printf("🔧 Memory after HTTP begin: %u bytes\n", ESP.getFreeHeap());
    
    http.addHeader("Content-Type", "application/json");
    String authHeader = "Token " + localApiKey;
    http.addHeader("Authorization", authHeader);
    http.addHeader("Accept-Encoding", "identity");  // Disable compression to reduce CPU load
    http.addHeader("Connection", "close");  // Close connection after request to free resources
    http.setTimeout(60000);  // 1 minute timeout (max for uint16_t)
    http.setReuse(false);  // Don't reuse connections to avoid potential issues

    int httpCode = http.POST(jsonPayload);
    Serial.printf("Deepgram TTS HTTP Response Code: %d\n", httpCode);

    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
            Serial.println("❌ Failed to get stream pointer");
            http.end();
            return false;
        }

        // Get content length if available
        int contentLength = http.getSize();
        Serial.printf("Content length: %d bytes\n", contentLength);

        // Allocate initial buffer (larger for better performance)
        size_t bufferSize = (contentLength > 0) ? contentLength : 16384;  // Default 16KB if unknown
        
        // Prefer PSRAM for large audio data if available
        if (psramFound()) {
            *audioData = (uint8_t*)ps_malloc(bufferSize);
            Serial.println("🔧 Allocated audio buffer in PSRAM");
        } else {
            *audioData = (uint8_t*)malloc(bufferSize);
            Serial.println("🔧 Allocated audio buffer in heap");
        }
        
        if (*audioData == nullptr) {
            Serial.println("❌ Failed to allocate memory for audio data");
            http.end();
            return false;
        }

        size_t totalRead = 0;
        unsigned long lastDataTime = millis();
        unsigned long downloadStartTime = millis();
        unsigned long lastProgressTime = millis();
        const unsigned long timeout = 60000;  // 60 second timeout
        const unsigned long progressInterval = 1000;  // Update progress every 1 second
        const size_t readChunkSize = 4096;  // Read in larger 4KB chunks for better performance

        Serial.println("📥 Starting download...");

        // Read all data from stream
        while (http.connected() && (stream->available() || (millis() - lastDataTime < timeout))) {
            if (stream->available()) {
                // Resize buffer if needed (with larger increments)
                if (totalRead + readChunkSize > bufferSize) {
                    bufferSize += 8192;  // Increase by 8KB at a time
                    
                    uint8_t* newBuffer = nullptr;
                    if (psramFound()) {
                        newBuffer = (uint8_t*)ps_realloc(*audioData, bufferSize);
                    } else {
                        newBuffer = (uint8_t*)realloc(*audioData, bufferSize);
                    }
                    
                    if (newBuffer == nullptr) {
                        Serial.println("❌ Failed to resize audio buffer");
                        free(*audioData);
                        *audioData = nullptr;
                        http.end();
                        return false;
                    }
                    *audioData = newBuffer;
                }

                size_t bytesRead = stream->readBytes(*audioData + totalRead, 
                    (readChunkSize < (bufferSize - totalRead)) ? readChunkSize : (bufferSize - totalRead));
                if (bytesRead > 0) {
                    totalRead += bytesRead;
                    lastDataTime = millis();
                    
                    // Show progress every second
                    unsigned long currentTime = millis();
                    if (currentTime - lastProgressTime >= progressInterval) {
                        unsigned long elapsedTime = currentTime - downloadStartTime;
                        float downloadSpeed = 0.0;
                        
                        if (elapsedTime > 0) {
                            downloadSpeed = (totalRead * 1000.0) / elapsedTime;  // bytes per second
                        }
                        
                        // Format speed in appropriate units
                        String speedUnit = "B/s";
                        float displaySpeed = downloadSpeed;
                        
                        if (downloadSpeed >= 1024) {
                            displaySpeed = downloadSpeed / 1024.0;
                            speedUnit = "KB/s";
                            
                            if (displaySpeed >= 1024) {
                                displaySpeed = displaySpeed / 1024.0;
                                speedUnit = "MB/s";
                            }
                        }
                        
                        // Show progress with known content length
                        if (contentLength > 0) {
                            float progressPercent = (totalRead * 100.0) / contentLength;
                            Serial.printf("📥 Progress: %.1f%% (%u/%d bytes) @ %.1f %s\n", 
                                         progressPercent, totalRead, contentLength, displaySpeed, speedUnit.c_str());
                        } else {
                            // Show progress without known total size
                            Serial.printf("📥 Downloaded: %u bytes @ %.1f %s\n", 
                                         totalRead, displaySpeed, speedUnit.c_str());
                        }
                        
                        lastProgressTime = currentTime;
                    }
                }
            } else {
                yield();
                delay(10);
            }
        }

        // Final download summary
        unsigned long totalDownloadTime = millis() - downloadStartTime;
        float avgSpeed = 0.0;
        if (totalDownloadTime > 0) {
            avgSpeed = (totalRead * 1000.0) / totalDownloadTime;
        }
        
        String avgSpeedUnit = "B/s";
        float displayAvgSpeed = avgSpeed;
        if (avgSpeed >= 1024) {
            displayAvgSpeed = avgSpeed / 1024.0;
            avgSpeedUnit = "KB/s";
            if (displayAvgSpeed >= 1024) {
                displayAvgSpeed = displayAvgSpeed / 1024.0;
                avgSpeedUnit = "MB/s";
            }
        }
        
        Serial.printf("✅ Download complete! %u bytes in %lu ms (avg: %.1f %s)\n", 
                     totalRead, totalDownloadTime, displayAvgSpeed, avgSpeedUnit.c_str());

        *dataSize = totalRead;
        success = (totalRead > 0);
    } else {
        Serial.printf("❌ HTTP request failed with code: %d\n", httpCode);
        String response = http.getString();
        if (response.length() > 0) {
            Serial.println("Error response:");
            Serial.println(response);
        }
    }
    
    http.end();
    
    Serial.printf("🔧 Memory after cleanup: %u bytes\n", ESP.getFreeHeap());
    
    return success;
}

bool TTS::hasDeepgramVoice(const String& language) {
    String lang = String(language);
    lang.toLowerCase();
    return lang.isEmpty() ||
           lang == "en" || lang == "en-us" || lang == "english" ||
           lang == "es" || lang == "spanish" ||
           lang == "fr" || lang == "french" ||
           lang == "de" || lang == "german" ||
           lang == "pt" || lang == "portuguese" ||
           lang == "it" || lang == "italian";
}

String TTS::googleLanguageCode(const String& language) {
    String lang = String(language);
    lang.toLowerCase();

    if (lang == "chinese" || lang == "mandarin" || lang == "zh" || lang == "zh-cn") return "cmn-CN";
    if (lang == "japanese" || lang == "ja") return "ja-JP";
    if (lang == "korean" || lang == "ko") return "ko-KR";
    if (lang == "hindi" || lang == "hi") return "hi-IN";
    if (lang == "arabic" || lang == "ar") return "ar-XA";
    if (lang == "russian" || lang == "ru") return "ru-RU";
    if (lang == "dutch" || lang == "nl") return "nl-NL";
    if (lang == "polish" || lang == "pl") return "pl-PL";
    if (lang == "turkish" || lang == "tr") return "tr-TR";
    if (lang == "vietnamese" || lang == "vi") return "vi-VN";
    if (lang == "thai" || lang == "th") return "th-TH";
    if (lang == "indonesian" || lang == "id") return "id-ID";
    if (lang == "greek" || lang == "el") return "el-GR";
    if (lang == "czech" || lang == "cs") return "cs-CZ";
    if (lang == "danish" || lang == "da") return "da-DK";
    if (lang == "finnish" || lang == "fi") return "fi-FI";
    if (lang == "swedish" || lang == "sv") return "sv-SE";
    if (lang == "norwegian" || lang == "no" || lang == "nb") return "nb-NO";
    if (lang == "ukrainian" || lang == "uk") return "uk-UA";
    if (lang == "romanian" || lang == "ro") return "ro-RO";
    if (lang == "hungarian" || lang == "hu") return "hu-HU";
    if (lang == "hebrew" || lang == "he") return "he-IL";
    if (lang == "bengali" || lang == "bn") return "bn-IN";
    if (lang == "tamil" || lang == "ta") return "ta-IN";
    if (lang == "telugu" || lang == "te") return "te-IN";
    if (lang == "malay" || lang == "ms") return "ms-MY";
    if (lang == "filipino" || lang == "tagalog" || lang == "fil" || lang == "tl") return "fil-PH";

    return lang;
}

bool TTS::hasCloudTtsVoice(const String& language) {
    String lang = String(language);
    lang.toLowerCase();
    // Mapped languages come back in BCP-47 form; unmapped ones come back unchanged
    return googleLanguageCode(language) != lang;
}

String TTS::translateTtsLanguageCode(const String& language) {
    String lang = String(language);
    lang.toLowerCase();

    if (lang == "chinese" || lang == "mandarin" || lang == "zh" || lang == "zh-cn") return "zh-CN";
    if (lang == "japanese") return "ja";
    if (lang == "korean") return "ko";
    if (lang == "hindi") return "hi";
    if (lang == "arabic") return "ar";
    if (lang == "russian") return "ru";
    if (lang == "dutch") return "nl";
    if (lang == "polish") return "pl";
    if (lang == "turkish") return "tr";
    if (lang == "vietnamese") return "vi";
    if (lang == "thai") return "th";
    if (lang == "indonesian") return "id";
    if (lang == "greek") return "el";
    if (lang == "czech") return "cs";
    if (lang == "danish") return "da";
    if (lang == "finnish") return "fi";
    if (lang == "swedish") return "sv";
    if (lang == "norwegian" || lang == "nb") return "no";
    if (lang == "ukrainian") return "uk";
    if (lang == "romanian") return "ro";
    if (lang == "hungarian") return "hu";
    if (lang == "hebrew") return "he";
    if (lang == "bengali") return "bn";
    if (lang == "tamil") return "ta";
    if (lang == "telugu") return "te";
    if (lang == "malay") return "ms";
    if (lang == "filipino" || lang == "tagalog" || lang == "fil") return "tl";
    if (lang == "swahili") return "sw";
    if (lang == "urdu") return "ur";
    if (lang == "persian" || lang == "farsi") return "fa";

    return lang;
}

bool TTS::callGoogleTtsAPI(const String& text, const String& language, uint8_t** audioData, size_t* dataSize) {
    String languageCode = googleLanguageCode(language);
    Serial.printf("🤖 Calling Google TTS API: \"%s\" (language: %s -> %s)\n", text.c_str(), language.c_str(), languageCode.c_str());

    if (googleTtsApiKey.length() < 10) {
        Serial.println("❌ Google TTS API key is not set or too short");
        return false;
    }

    // Check available memory before proceeding
    size_t freeHeap = ESP.getFreeHeap();
    size_t freePsram = psramFound() ? ESP.getFreePsram() : 0;
    Serial.printf("🔧 Memory check: Free heap=%u, Free PSRAM=%u\n", freeHeap, freePsram);

    if (freeHeap < 50000) { // Need at least 50KB free heap for SSL
        Serial.printf("❌ Insufficient heap memory for TTS: %u bytes (need 50KB+)\n", freeHeap);
        return false;
    }

    // Create local copies to avoid String scope issues
    String localText = String(text);
    String localApiKey = String(googleTtsApiKey);

    // LINEAR16 at 16kHz so the response plays directly on the existing I2S pipeline
    String jsonPayload = "{\"input\":{\"text\":\"" + localText + "\"},"
                         "\"voice\":{\"languageCode\":\"" + languageCode + "\"},"
                         "\"audioConfig\":{\"audioEncoding\":\"LINEAR16\",\"sampleRateHertz\":" + String(SAMPLE_RATE) + "}}";

    HTTPClient http;

    WiFiClientSecure client;
    client.setInsecure();

    // Check WiFi connection before configuring client
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected - cannot proceed with TTS request");
        return false;
    }

    String googleUrl = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + localApiKey;

    if (!http.begin(client, googleUrl)) {
        Serial.println("❌ Failed to begin HTTP connection");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept-Encoding", "identity");  // Disable compression to reduce CPU load
    http.addHeader("Connection", "close");  // Close connection after request to free resources
    http.setTimeout(60000);  // 1 minute timeout (max for uint16_t)
    http.setReuse(false);  // Don't reuse connections to avoid potential issues

    int httpCode = http.POST(jsonPayload);
    Serial.printf("Google TTS HTTP Response Code: %d\n", httpCode);

    bool success = false;

    if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
            Serial.println("❌ Failed to get stream pointer");
            http.end();
            return false;
        }

        int contentLength = http.getSize();
        Serial.printf("Content length: %d bytes\n", contentLength);

        // Download the full JSON response into PSRAM before decoding
        size_t bufferSize = (contentLength > 0) ? contentLength + 1 : 16384;

        uint8_t* jsonBuffer = nullptr;
        if (psramFound()) {
            jsonBuffer = (uint8_t*)ps_malloc(bufferSize);
            Serial.println("🔧 Allocated response buffer in PSRAM");
        } else {
            jsonBuffer = (uint8_t*)malloc(bufferSize);
            Serial.println("🔧 Allocated response buffer in heap");
        }

        if (jsonBuffer == nullptr) {
            Serial.println("❌ Failed to allocate memory for response data");
            http.end();
            return false;
        }

        size_t totalRead = 0;
        unsigned long lastDataTime = millis();
        const unsigned long timeout = 60000;  // 60 second timeout
        const size_t readChunkSize = 4096;

        Serial.println("📥 Starting download...");

        while (http.connected() && (stream->available() || (millis() - lastDataTime < timeout))) {
            if (stream->available()) {
                if (totalRead + readChunkSize + 1 > bufferSize) {
                    bufferSize += 8192;

                    uint8_t* newBuffer = nullptr;
                    if (psramFound()) {
                        newBuffer = (uint8_t*)ps_realloc(jsonBuffer, bufferSize);
                    } else {
                        newBuffer = (uint8_t*)realloc(jsonBuffer, bufferSize);
                    }

                    if (newBuffer == nullptr) {
                        Serial.println("❌ Failed to resize response buffer");
                        free(jsonBuffer);
                        http.end();
                        return false;
                    }
                    jsonBuffer = newBuffer;
                }

                size_t bytesRead = stream->readBytes(jsonBuffer + totalRead,
                    (readChunkSize < (bufferSize - 1 - totalRead)) ? readChunkSize : (bufferSize - 1 - totalRead));
                if (bytesRead > 0) {
                    totalRead += bytesRead;
                    lastDataTime = millis();
                }
            } else {
                yield();
                delay(10);
            }
        }

        Serial.printf("✅ Download complete! %u bytes\n", totalRead);
        jsonBuffer[totalRead] = '\0';

        // Extract the base64 audioContent value without parsing the full JSON in heap
        char* audioContent = strstr((char*)jsonBuffer, "\"audioContent\"");
        char* b64Start = nullptr;
        char* b64End = nullptr;

        if (audioContent != nullptr) {
            b64Start = strchr(audioContent + 14, '"');
            if (b64Start != nullptr) {
                b64Start++;
                b64End = strchr(b64Start, '"');
            }
        }

        if (b64Start == nullptr || b64End == nullptr || b64End <= b64Start) {
            Serial.println("❌ No audioContent found in Google TTS response");
            free(jsonBuffer);
            http.end();
            return false;
        }

        size_t b64Length = b64End - b64Start;
        size_t decodedCapacity = (b64Length / 4) * 3 + 4;

        uint8_t* decoded = nullptr;
        if (psramFound()) {
            decoded = (uint8_t*)ps_malloc(decodedCapacity);
            Serial.println("🔧 Allocated audio buffer in PSRAM");
        } else {
            decoded = (uint8_t*)malloc(decodedCapacity);
            Serial.println("🔧 Allocated audio buffer in heap");
        }

        if (decoded == nullptr) {
            Serial.println("❌ Failed to allocate memory for audio data");
            free(jsonBuffer);
            http.end();
            return false;
        }

        size_t decodedLength = base64_decode_to_buffer(b64Start, b64Length, decoded, decodedCapacity);
        free(jsonBuffer);

        if (decodedLength == 0) {
            Serial.println("❌ Failed to decode audio data from Google TTS response");
            free(decoded);
            http.end();
            return false;
        }

        // LINEAR16 responses arrive in a WAV container - strip the header to get raw PCM
        size_t pcmOffset = 0;
        if (decodedLength > 44 && memcmp(decoded, "RIFF", 4) == 0) {
            size_t pos = 12;
            while (pos + 8 <= decodedLength) {
                uint32_t chunkSize = decoded[pos + 4] | (decoded[pos + 5] << 8) |
                                     (decoded[pos + 6] << 16) | ((uint32_t)decoded[pos + 7] << 24);
                if (memcmp(decoded + pos, "data", 4) == 0) {
                    pcmOffset = pos + 8;
                    break;
                }
                pos += 8 + chunkSize;
            }
        }

        if (pcmOffset > 0 && pcmOffset < decodedLength) {
            decodedLength -= pcmOffset;
            memmove(decoded, decoded + pcmOffset, decodedLength);
        }

        Serial.printf("✅ Decoded %u bytes of PCM audio from Google TTS\n", decodedLength);

        *audioData = decoded;
        *dataSize = decodedLength;
        success = (decodedLength > 0);
    } else {
        Serial.printf("❌ Google TTS request failed. HTTP Code: %d\n", httpCode);
        String response = http.getString();
        if (response.length() > 0) {
            Serial.println("Error response:");
            Serial.println(response);
        }
    }

    http.end();

    Serial.printf("🔧 Memory after cleanup: %u bytes\n", ESP.getFreeHeap());

    return success;
}

static const size_t TRANSLATE_TTS_CHUNK_BYTES = 180;  // translate_tts rejects roughly 200+ characters per request
static const char* TRANSLATE_TTS_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36";

static String urlEncodeText(const String& text) {
    const char* hex = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(text.length() * 3);
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            encoded += '%';
            encoded += hex[((unsigned char)c) >> 4];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

static size_t translateTtsChunkLength(const char* text, size_t remaining) {
    if (remaining <= TRANSLATE_TTS_CHUNK_BYTES) {
        return remaining;
    }

    size_t limit = TRANSLATE_TTS_CHUNK_BYTES;
    while (limit > 0 && ((unsigned char)text[limit] & 0xC0) == 0x80) {
        limit--;  // Never split a UTF-8 sequence
    }

    size_t sentenceEnd = 0;
    size_t wordEnd = 0;
    for (size_t i = 0; i < limit; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '.' || c == '!' || c == '?' || c == ';' || c == ':' || c == ',') {
            sentenceEnd = i + 1;
        } else if (c == ' ' || c == '\n') {
            wordEnd = i + 1;
        } else if (c == 0xE3 && i + 2 < limit &&
                   (unsigned char)text[i + 1] == 0x80 && (unsigned char)text[i + 2] == 0x82) {
            sentenceEnd = i + 3;  // 。
        } else if (c == 0xEF && i + 2 < limit && (unsigned char)text[i + 1] == 0xBC &&
                   ((unsigned char)text[i + 2] == 0x81 || (unsigned char)text[i + 2] == 0x9F ||
                    (unsigned char)text[i + 2] == 0x8C)) {
            sentenceEnd = i + 3;  // ！ ？ ，
        }
    }

    if (sentenceEnd > 0) return sentenceEnd;
    if (wordEnd > 0) return wordEnd;
    return (limit > 0) ? limit : 1;
}

static bool decodeMp3ToPcm(const uint8_t* mp3Data, size_t mp3Size, int16_t** pcmOut, size_t* samplesOut, int* sampleRateOut) {
    *pcmOut = nullptr;
    *samplesOut = 0;
    *sampleRateOut = 0;

    HMP3Decoder decoder = MP3InitDecoder();
    if (decoder == nullptr) {
        return false;
    }

    // Max helix output: 1152 samples x 2 channels
    short* frameBuffer = nullptr;
    if (psramFound()) {
        frameBuffer = (short*)ps_malloc(2304 * sizeof(short));
    } else {
        frameBuffer = (short*)malloc(2304 * sizeof(short));
    }
    if (frameBuffer == nullptr) {
        MP3FreeDecoder(decoder);
        return false;
    }

    size_t capacity = 0;
    int16_t* pcm = nullptr;
    size_t sampleCount = 0;

    unsigned char* readPtr = (unsigned char*)mp3Data;
    int bytesLeft = (int)mp3Size;
    bool failed = false;

    while (bytesLeft > 0) {
        int syncOffset = MP3FindSyncWord(readPtr, bytesLeft);
        if (syncOffset < 0) {
            break;
        }
        readPtr += syncOffset;
        bytesLeft -= syncOffset;

        int err = MP3Decode(decoder, &readPtr, &bytesLeft, frameBuffer, 0);
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            break;
        }
        if (err != ERR_MP3_NONE) {
            // Skip a byte and resync - translate_tts streams occasionally carry junk between frames
            readPtr++;
            bytesLeft--;
            continue;
        }

        MP3FrameInfo frameInfo;
        MP3GetLastFrameInfo(decoder, &frameInfo);
        if (frameInfo.outputSamps <= 0 || frameInfo.nChans <= 0) {
            continue;
        }
        if (*sampleRateOut == 0) {
            *sampleRateOut = frameInfo.samprate;
        }

        size_t frameSamples = frameInfo.outputSamps / frameInfo.nChans;
        if (sampleCount + frameSamples > capacity) {
            capacity = (capacity == 0) ? 32768 : capacity * 2;
            int16_t* newPcm = nullptr;
            if (psramFound()) {
                newPcm = (int16_t*)ps_realloc(pcm, capacity * sizeof(int16_t));
            } else {
                newPcm = (int16_t*)realloc(pcm, capacity * sizeof(int16_t));
            }
            if (newPcm == nullptr) {
                failed = true;
                break;
            }
            pcm = newPcm;
        }

        if (frameInfo.nChans == 2) {
            for (size_t i = 0; i < frameSamples; i++) {
                pcm[sampleCount + i] = (int16_t)(((int32_t)frameBuffer[2 * i] + frameBuffer[2 * i + 1]) / 2);
            }
        } else {
            memcpy(pcm + sampleCount, frameBuffer, frameSamples * sizeof(int16_t));
        }
        sampleCount += frameSamples;
    }

    free(frameBuffer);
    MP3FreeDecoder(decoder);

    if (failed || sampleCount == 0 || *sampleRateOut == 0) {
        if (pcm != nullptr) {
            free(pcm);
        }
        return false;
    }

    *pcmOut = pcm;
    *samplesOut = sampleCount;
    return true;
}

static int16_t* resamplePcmLinear(const int16_t* in, size_t inSamples, int inRate, int outRate, size_t* outSamplesOut) {
    *outSamplesOut = 0;
    if (inSamples == 0 || inRate <= 0 || outRate <= 0) {
        return nullptr;
    }

    size_t outSamples = (size_t)(((uint64_t)inSamples * outRate) / inRate);
    if (outSamples == 0) {
        return nullptr;
    }

    int16_t* out = nullptr;
    if (psramFound()) {
        out = (int16_t*)ps_malloc(outSamples * sizeof(int16_t));
    } else {
        out = (int16_t*)malloc(outSamples * sizeof(int16_t));
    }
    if (out == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < outSamples; i++) {
        uint64_t srcPos = (uint64_t)i * inRate;
        size_t idx = (size_t)(srcPos / outRate);
        int64_t frac = (int64_t)(srcPos % outRate);
        int32_t s0 = in[idx];
        int32_t s1 = (idx + 1 < inSamples) ? in[idx + 1] : s0;
        out[i] = (int16_t)(s0 + (int32_t)(((s1 - s0) * frac) / outRate));
    }

    *outSamplesOut = outSamples;
    return out;
}

static bool fetchTranslateTtsMp3(const String& chunk, const String& languageCode, uint8_t** mp3Data, size_t* mp3Size) {
    *mp3Data = nullptr;
    *mp3Size = 0;

    HTTPClient http;

    WiFiClientSecure client;
    client.setInsecure();

    String translateUrl = "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=" +
                          languageCode + "&q=" + urlEncodeText(chunk);

    if (!http.begin(client, translateUrl)) {
        Serial.println("❌ Failed to begin HTTP connection");
        return false;
    }

    http.setUserAgent(TRANSLATE_TTS_USER_AGENT);  // translate_tts returns 404 for non-browser agents
    http.addHeader("Accept-Encoding", "identity");  // Disable compression to reduce CPU load
    http.addHeader("Connection", "close");  // Close connection after request to free resources
    http.setTimeout(60000);  // 1 minute timeout (max for uint16_t)
    http.setReuse(false);  // Don't reuse connections to avoid potential issues

    int httpCode = http.GET();
    Serial.printf("Google Translate TTS HTTP Response Code: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("❌ Google Translate TTS request failed. HTTP Code: %d\n", httpCode);
        if (httpCode == 404 || httpCode == 429 || httpCode == 400) {
            Serial.println("⚠️ translate_tts is an unofficial endpoint - it may be rate limiting, missing this language, or changed");
        }
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        Serial.println("❌ Failed to get stream pointer");
        http.end();
        return false;
    }

    int contentLength = http.getSize();

    size_t bufferSize = (contentLength > 0) ? contentLength : 16384;

    uint8_t* buffer = nullptr;
    if (psramFound()) {
        buffer = (uint8_t*)ps_malloc(bufferSize);
    } else {
        buffer = (uint8_t*)malloc(bufferSize);
    }

    if (buffer == nullptr) {
        Serial.println("❌ Failed to allocate memory for MP3 data");
        http.end();
        return false;
    }

    size_t totalRead = 0;
    unsigned long lastDataTime = millis();
    const unsigned long timeout = 60000;  // 60 second timeout
    const size_t readChunkSize = 4096;

    while (http.connected() && (stream->available() || (millis() - lastDataTime < timeout))) {
        if (stream->available()) {
            if (totalRead + readChunkSize > bufferSize) {
                bufferSize += 8192;

                uint8_t* newBuffer = nullptr;
                if (psramFound()) {
                    newBuffer = (uint8_t*)ps_realloc(buffer, bufferSize);
                } else {
                    newBuffer = (uint8_t*)realloc(buffer, bufferSize);
                }

                if (newBuffer == nullptr) {
                    Serial.println("❌ Failed to resize MP3 buffer");
                    free(buffer);
                    http.end();
                    return false;
                }
                buffer = newBuffer;
            }

            size_t bytesRead = stream->readBytes(buffer + totalRead,
                (readChunkSize < (bufferSize - totalRead)) ? readChunkSize : (bufferSize - totalRead));
            if (bytesRead > 0) {
                totalRead += bytesRead;
                lastDataTime = millis();
            }
        } else {
            yield();
            delay(10);
        }
    }

    http.end();

    if (totalRead == 0) {
        Serial.println("❌ Google Translate TTS returned no data");
        free(buffer);
        return false;
    }

    *mp3Data = buffer;
    *mp3Size = totalRead;
    return true;
}

bool TTS::callGoogleTranslateTtsAPI(const String& text, const String& language, uint8_t** audioData, size_t* dataSize) {
    String languageCode = translateTtsLanguageCode(language);
    Serial.printf("🤖 Calling Google Translate TTS: \"%s\" (language: %s -> %s)\n", text.c_str(), language.c_str(), languageCode.c_str());

    // Check available memory before proceeding
    size_t freeHeap = ESP.getFreeHeap();
    size_t freePsram = psramFound() ? ESP.getFreePsram() : 0;
    Serial.printf("🔧 Memory check: Free heap=%u, Free PSRAM=%u\n", freeHeap, freePsram);

    if (freeHeap < 50000) { // Need at least 50KB free heap for SSL
        Serial.printf("❌ Insufficient heap memory for TTS: %u bytes (need 50KB+)\n", freeHeap);
        return false;
    }

    // Check WiFi connection before configuring client
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected - cannot proceed with TTS request");
        return false;
    }

    // Create local copies to avoid String scope issues
    String localText = String(text);

    uint8_t* pcmBuffer = nullptr;
    size_t pcmSize = 0;
    size_t pcmCapacity = 0;

    const char* raw = localText.c_str();
    size_t textLength = localText.length();
    size_t offset = 0;
    int chunkCount = 0;

    while (offset < textLength) {
        if (is_cancellation_requested) {
            Serial.println("🚫 Google Translate TTS cancelled by request");
            if (pcmBuffer != nullptr) {
                free(pcmBuffer);
            }
            return false;
        }

        size_t chunkLength = translateTtsChunkLength(raw + offset, textLength - offset);
        String chunk = localText.substring(offset, offset + chunkLength);
        offset += chunkLength;
        chunk.trim();
        if (chunk.isEmpty()) {
            continue;
        }
        chunkCount++;

        uint8_t* mp3Data = nullptr;
        size_t mp3Size = 0;
        if (!fetchTranslateTtsMp3(chunk, languageCode, &mp3Data, &mp3Size)) {
            Serial.printf("❌ Failed to fetch chunk %d from Google Translate TTS\n", chunkCount);
            if (pcmBuffer != nullptr) {
                free(pcmBuffer);
            }
            return false;
        }

        int16_t* chunkPcm = nullptr;
        size_t chunkSamples = 0;
        int sampleRate = 0;
        bool decoded = decodeMp3ToPcm(mp3Data, mp3Size, &chunkPcm, &chunkSamples, &sampleRate);
        free(mp3Data);

        if (!decoded) {
            Serial.printf("❌ Failed to decode MP3 for chunk %d\n", chunkCount);
            if (pcmBuffer != nullptr) {
                free(pcmBuffer);
            }
            return false;
        }

        // translate_tts serves 24kHz mono - resample to the 16kHz the I2S pipeline expects
        if (sampleRate != SAMPLE_RATE) {
            size_t resampledCount = 0;
            int16_t* resampledPcm = resamplePcmLinear(chunkPcm, chunkSamples, sampleRate, SAMPLE_RATE, &resampledCount);
            free(chunkPcm);

            if (resampledPcm == nullptr) {
                Serial.printf("❌ Failed to resample chunk %d from %d Hz\n", chunkCount, sampleRate);
                if (pcmBuffer != nullptr) {
                    free(pcmBuffer);
                }
                return false;
            }
            chunkPcm = resampledPcm;
            chunkSamples = resampledCount;
        }

        size_t chunkBytes = chunkSamples * sizeof(int16_t);
        if (pcmSize + chunkBytes > pcmCapacity) {
            pcmCapacity = pcmSize + chunkBytes + 65536;

            uint8_t* newBuffer = nullptr;
            if (psramFound()) {
                newBuffer = (uint8_t*)ps_realloc(pcmBuffer, pcmCapacity);
            } else {
                newBuffer = (uint8_t*)realloc(pcmBuffer, pcmCapacity);
            }

            if (newBuffer == nullptr) {
                Serial.println("❌ Failed to resize audio buffer");
                free(chunkPcm);
                if (pcmBuffer != nullptr) {
                    free(pcmBuffer);
                }
                return false;
            }
            pcmBuffer = newBuffer;
        }
        memcpy(pcmBuffer + pcmSize, chunkPcm, chunkBytes);
        pcmSize += chunkBytes;
        free(chunkPcm);

        Serial.printf("🔊 Chunk %d ready: %u bytes of PCM (total %u)\n", chunkCount, chunkBytes, pcmSize);
    }

    if (pcmBuffer == nullptr || pcmSize == 0) {
        Serial.println("❌ Google Translate TTS produced no audio");
        if (pcmBuffer != nullptr) {
            free(pcmBuffer);
        }
        return false;
    }

    Serial.printf("✅ Google Translate TTS complete: %d chunks, %u bytes of PCM\n", chunkCount, pcmSize);
    Serial.printf("🔧 Memory after cleanup: %u bytes\n", ESP.getFreeHeap());

    *audioData = pcmBuffer;
    *dataSize = pcmSize;
    return true;
}

bool TTS::playAudioData(const uint8_t* audioData, size_t dataSize) {
    // Track if we had to request speaker access (meaning we need to release it afterwards)
    bool requestedAccess = false;
    
    // Request speaker access first, force if necessary
    if (!I2SManager::hasI2SAccess(I2SDevice::SPEAKER)) {
        Serial.println("TTS: Requesting speaker access for audio playback...");
        requestedAccess = true;
        if (!requestSpeakerAccess()) {
            Serial.println("TTS: Normal speaker access failed, forcing I2S release...");
            I2SManager::forceReleaseI2SAccess();
            if (!requestSpeakerAccess()) {
                Serial.println("TTS: Failed to get speaker access even after force release");
                return false;
            }
        }
    }
    
    if (!i2sInitialized) {
        Serial.println("TTS: I2S not initialized");
        return false;
    }
    
    if (audioData == nullptr || dataSize == 0) {
        Serial.println("TTS: Invalid audio data");
        return false;
    }
    
    Serial.printf("▶️ Playing RAW audio data: %u bytes\n", dataSize);
    
    // Apply software gain if needed (make a copy to avoid modifying original data)
    uint8_t* processedAudioData = nullptr;
    const uint8_t* playbackData = audioData;  // Default to original data
    
    if (softwareGain != 1.0) {
        // Create a copy for gain processing
        processedAudioData = (uint8_t*)ps_malloc(dataSize);
        if (processedAudioData != nullptr) {
            memcpy(processedAudioData, audioData, dataSize);
            applySoftwareGain(processedAudioData, dataSize);
            playbackData = processedAudioData;
        } else {
            Serial.println("⚠️ Failed to allocate memory for gain processing, using original audio");
        }
    }
    
    // Calculate estimated playback duration
    unsigned long estimatedDurationMs = (dataSize * 1000) / (SAMPLE_RATE * 2);  // 16-bit samples
    Serial.printf("⏱️ Estimated playback duration: %lu ms (%.1f seconds)\n", 
                 estimatedDurationMs, estimatedDurationMs / 1000.0);
    Serial.printf("🔊 Software gain: %.2f\n", softwareGain);
    
    // Clear DMA buffer before starting playback
    i2s_zero_dma_buffer(I2S_PORT);
    
    size_t totalWritten = 0;
    size_t bytesWritten;
    unsigned long playbackStartTime = millis();
    unsigned long lastProgressTime = millis();
    const unsigned long progressInterval = 2000;  // Update progress every 2 seconds
    
    // Write raw PCM data directly to I2S in chunks
    while (totalWritten < dataSize) {
        if (is_cancellation_requested) {
            Serial.println("🚫 Audio playback cancelled by request");
            break;
        }
        size_t chunkSize = (BUFFER_SIZE < (dataSize - totalWritten)) ? BUFFER_SIZE : (dataSize - totalWritten);
        
        esp_err_t err = i2s_write(I2S_PORT, playbackData + totalWritten, chunkSize, &bytesWritten, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("❌ I2S write error: %s\n", esp_err_to_name(err));
            break;
        }
        
        if (bytesWritten < chunkSize) {
            Serial.printf("⚠️ I2S underrun: tried to write %u, only wrote %u\n", chunkSize, bytesWritten);
        }
        
        totalWritten += bytesWritten;
        
        // Show playback progress every 2 seconds
        unsigned long currentTime = millis();
        if (currentTime - lastProgressTime >= progressInterval) {
            float progressPercent = (totalWritten * 100.0) / dataSize;
            unsigned long elapsedPlaybackTime = currentTime - playbackStartTime;
            Serial.printf("🎵 Playback progress: %.1f%% (%u/%u bytes, %lu ms elapsed)\n", 
                         progressPercent, totalWritten, dataSize, elapsedPlaybackTime);
            lastProgressTime = currentTime;
        }
        
        yield();  // Allow other tasks to run
    }
    
    Serial.printf("🎵 Finished playing audio. Total bytes sent to I2S: %u\n", totalWritten);
    
    if (totalWritten > 0) {
        // Add silence padding to prevent static at the end
        size_t silenceDuration = SAMPLE_RATE * 2 * 0.1;  // 100ms of silence (16-bit samples)
        uint8_t* silenceBuffer = (uint8_t*)ps_calloc(silenceDuration, 1);  // Zero-filled buffer
        if (silenceBuffer != nullptr) {
            size_t silenceWritten;
            esp_err_t err = i2s_write(I2S_PORT, silenceBuffer, silenceDuration, &silenceWritten, 1000);
            if (err == ESP_OK) {
                Serial.println("🔇 Added silence padding to prevent static");
            }
            free(silenceBuffer);
        }
        
        // Calculate playback duration and wait for completion
        unsigned long estimatedDurationMs = (totalWritten * 1000) / (SAMPLE_RATE * 2);  // 16-bit samples
        unsigned long waitTime = estimatedDurationMs * 3;  // Multiply by 3 to prevent static
        
        Serial.printf("Waiting %lu ms for audio playback to complete...\n", waitTime);
        delay(waitTime);
        
        // Gradually fade out by clearing DMA buffer in smaller steps
        Serial.println("🔇 Gracefully stopping audio output...");
        i2s_zero_dma_buffer(I2S_PORT);
        delay(50);  // Small delay to ensure clean stop
    }
    
    // Clean up processed audio data if we created a copy
    if (processedAudioData != nullptr) {
        free(processedAudioData);
    }
    
    // If we requested access for this playback, release it so microphone can use I2S again
    if (requestedAccess) {
        Serial.println("TTS: Releasing speaker access after ding playback");
        releaseSpeakerAccess();
    }
    
    return totalWritten == dataSize;
}

void TTS::stopPlayback() {
    cancel();
}

void TTS::cancel() {
    // For simple implementation, we just stop feeding data to I2S
    // The MAX98357A will naturally stop when no more data is provided
    Serial.println("TTS: Stopping playback");
    is_cancellation_requested = true;
}

void TTS::setVolume(float volume) {
    // Implement volume control using software gain
    if (volume < 0.0) volume = 0.0;
    if (volume > 1.0) volume = 1.0;
    
    setSoftwareGain(volume * 2.0);  // Map 0.0-1.0 to 0.0-2.0 gain range
    Serial.printf("TTS: Volume set to %.2f (software gain: %.2f)\n", volume, softwareGain);
}

void TTS::setSoftwareGain(float gain) {
    if (gain < 0.0) gain = 0.0;
    if (gain > 2.0) gain = 2.0;
    
    softwareGain = gain;
    Serial.printf("TTS: Software gain set to %.2f\n", softwareGain);
}

float TTS::getSoftwareGain() const {
    return softwareGain;
}

void TTS::applySoftwareGain(uint8_t* audioData, size_t dataSize) {
    if (softwareGain == 1.0 || audioData == nullptr || dataSize == 0) {
        return;  // No gain adjustment needed or invalid data
    }
    
    // Process 16-bit audio samples
    int16_t* samples = (int16_t*)audioData;
    size_t sampleCount = dataSize / 2;  // 16-bit samples = 2 bytes each
    
    for (size_t i = 0; i < sampleCount; i++) {
        // Apply gain with saturation protection
        int32_t amplified = (int32_t)(samples[i] * softwareGain);
        
        // Clamp to 16-bit range to prevent overflow/distortion
        if (amplified > 32767) {
            amplified = 32767;
        } else if (amplified < -32768) {
            amplified = -32768;
        }
        
        samples[i] = (int16_t)amplified;
    }
    
    Serial.printf("🔊 Applied software gain %.2f to %u samples\n", softwareGain, sampleCount);
}

void TTS::cleanupAudioData(uint8_t* audioData) {
    if (audioData != nullptr) {
        free(audioData);
    }
}

void TTS::optimizeWiFiForSpeed() {
    Serial.println("🚀 Optimizing WiFi for maximum speed...");
    
    // Check if WiFi is connected before optimizing
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️ WiFi not connected - some optimizations may not apply until connected");
    }
    
    // Set WiFi to station mode only (no AP mode)
    WiFi.mode(WIFI_STA);

    WiFi.setSleep(false);  // Disable WiFi sleep mode for maximum throughput

    // Only proceed with ESP WiFi optimizations if WiFi is available
    if (WiFi.status() == WL_CONNECTED) {
        // Disable power saving mode for maximum throughput
        esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (err == ESP_OK) {
            Serial.println("✅ WiFi power saving disabled");
        } else {
            Serial.printf("⚠️ Failed to disable WiFi power saving: %s\n", esp_err_to_name(err));
        }
        
        // // Set WiFi to use 40MHz bandwidth (instead of 20MHz) for higher speeds
        // wifi_config_t wifi_config;
        // err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
        // if (err == ESP_OK) {
        //     // Enable 802.11n (HT40) for higher bandwidth
        //     err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
        //     if (err == ESP_OK) {
        //         Serial.println("✅ WiFi bandwidth set to 40MHz (HT40)");
        //     } else {
        //         Serial.printf("⚠️ Failed to set 40MHz bandwidth: %s\n", esp_err_to_name(err));
        //     }
        // }
        
        // Set WiFi protocol to 802.11bgn for maximum compatibility and speed
        err = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        if (err == ESP_OK) {
            Serial.println("✅ WiFi protocol set to 802.11bgn");
        } else {
            Serial.printf("⚠️ Failed to set WiFi protocol: %s\n", esp_err_to_name(err));
        }
        
        // Set maximum transmission power
        err = esp_wifi_set_max_tx_power(WIFI_POWER_19_5dBm);
        if (err == ESP_OK) {
            Serial.println("✅ WiFi TX power set to maximum (19.5 dBm)");
        } else {
            Serial.printf("⚠️ Failed to set max TX power: %s\n", esp_err_to_name(err));
        }
    } else {
        Serial.println("⚠️ Skipping advanced WiFi optimizations - not connected");
    }
    
    // Configure TCP settings for better performance
    Serial.println("🔧 WiFi optimization settings applied");
    
    Serial.println("📡 WiFi optimization complete!");
    Serial.println("💡 For best results, ensure your router supports:");
    Serial.println("   - 802.11n (2.4GHz) or 802.11ac (5GHz)");
    Serial.println("   - 40MHz channel width");
    Serial.println("   - Low network congestion");
}

void TTS::setDefaultLanguage(const String& language) {
    defaultLanguage = language;
    Serial.printf("TTS default language set to: %s\n", language.c_str());
}

void TTS::setGoogleTtsApiKey(const String& apiKey) {
    googleTtsApiKey = apiKey;
}
void TTS::playTone(int frequency, int duration) {
    if (true) return;
    // Track if we had to request speaker access (meaning we need to release it afterwards)
    bool requestedAccess = false;
    
    // Request speaker access first, force if necessary
    if (!I2SManager::hasI2SAccess(I2SDevice::SPEAKER)) {
        Serial.println("TTS: Requesting speaker access for tone playback...");
        requestedAccess = true;
        if (!requestSpeakerAccess()) {
            Serial.println("TTS: Normal speaker access failed for tone, forcing I2S release...");
            I2SManager::forceReleaseI2SAccess();
            if (!requestSpeakerAccess()) {
                Serial.println("TTS: Failed to get speaker access for tone even after force release");
                return;
            }
        }
    }

    size_t numSamples = (SAMPLE_RATE * duration) / 1000;
    size_t dataSize = numSamples * 2; // 16-bit samples
    uint8_t* audioData = (uint8_t*)ps_malloc(dataSize);

    if (!audioData) {
        Serial.println("Failed to allocate memory for tone");
        if (requestedAccess) {
            releaseSpeakerAccess();
        }
        return;
    }

    int16_t* samples = (int16_t*)audioData;
    for (int i = 0; i < numSamples; i++) {
        float angle = 2.0 * PI * frequency * i / SAMPLE_RATE;
        samples[i] = (int16_t)(32767.0 * sin(angle) * 0.5); // 50% volume
    }

    playAudioData(audioData, dataSize);
    free(audioData);
    
    // If we requested access for this tone playback, release it so microphone can use I2S again
    if (requestedAccess) {
        Serial.println("TTS: Releasing speaker access after tone playback");
        releaseSpeakerAccess();
    }
}

