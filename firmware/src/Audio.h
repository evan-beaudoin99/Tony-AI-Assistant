#pragma once

#include <Arduino.h>

// Mic in (INMP441) and speaker out (MAX98357A), each on its own I2S port and FreeRTOS task.
// The mic goes quiet while Tony talks (and a moment after) so he doesn't hear himself.
namespace audio {

constexpr uint32_t MIC_RATE = 16000;      // 16-bit mono to the server
constexpr uint32_t SPEAKER_RATE = 24000;  // 16-bit mono from the server

bool begin();

// Mic. While enabled, 16-bit PCM collects for readMic(); otherwise it's thrown away.
void setMicEnabled(bool on);
size_t micAvailable();
size_t readMic(uint8_t* dst, size_t maxBytes);

// Speaker. One reply = playBegin(), any number of playFeed(), playEnd(); it plays out what's buffered.
void playBegin();
size_t playFeed(const uint8_t* pcm, size_t len);
void playEnd();
void playStop();  // drop everything right now
bool playing();
bool takePlaybackDone();  // true once after a reply finished playing out (not after playStop or a chirp)
float level();            // loudness of what's playing right now, 0..1, for the mouth
void setVolume(uint8_t percent);

// A short "I'm listening" blip, played locally.
void chirp();

}  // namespace audio
