#include "Audio.h"

#include <ESP_I2S.h>

#include <atomic>

#include "RingBuffer.h"
#include "pins.h"

namespace {

// The INMP441 sends 24-bit samples left-justified in 32-bit slots. >> 16 is unity; each step less doubles the gain.
constexpr int MIC_SHIFT = 12;
// The mic stays muted this long after the speaker goes quiet, so Tony's last word doesn't echo back.
constexpr uint32_t ECHO_TAIL_MS = 350;

I2SClass micI2s;
I2SClass spkI2s;
ByteRing micRing;  // ~1 s of mic audio in internal RAM
ByteRing spkRing;  // ~21 s of speech in PSRAM; the server stays at most a few seconds ahead

std::atomic<bool> micOn{false};
std::atomic<bool> playingFlag{false};
std::atomic<bool> streamEnded{true};
std::atomic<bool> flushReq{false};
std::atomic<bool> chirpReq{false};
std::atomic<bool> doneFlag{false};
std::atomic<uint32_t> lastSoundMs{0};
std::atomic<float> levelNow{0.0f};
std::atomic<uint8_t> volume{70};

void playChirp(int16_t* buf, size_t n) {
  const float total = audio::SPEAKER_RATE * 0.12f;  // 120 ms, sliding up a fifth
  float phase = 0;
  for (size_t done = 0; done < total; done += n) {
    for (size_t i = 0; i < n; ++i) {
      float t = (done + i) / total;
      float env = t < 1.0f ? sinf(PI * t) : 0.0f;
      phase += TWO_PI * (880.0f + 440.0f * t) / audio::SPEAKER_RATE;
      buf[i] = static_cast<int16_t>(sinf(phase) * env * 5000.0f * volume.load() / 100.0f);
    }
    spkI2s.write(reinterpret_cast<uint8_t*>(buf), n * sizeof(int16_t));
  }
  lastSoundMs = millis();
}

void speakerTask(void*) {
  static int16_t buf[480];  // 20 ms
  for (;;) {
    if (flushReq.exchange(false)) {
      spkRing.clear();
      playingFlag = false;
      streamEnded = true;
    }
    if (chirpReq.exchange(false)) {
      playChirp(buf, 480);
      continue;
    }

    size_t got = playingFlag ? spkRing.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf)) : 0;
    if (got < sizeof(int16_t)) {
      if (playingFlag && streamEnded) {
        playingFlag = false;
        doneFlag = true;
      }
      levelNow = levelNow.load() * 0.7f;
      memset(buf, 0, sizeof(buf));  // keep the amp fed with silence so it doesn't click or buzz
      spkI2s.write(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
      continue;
    }

    size_t n = got / sizeof(int16_t);
    int32_t vol = volume.load();
    float sum = 0;
    for (size_t i = 0; i < n; ++i) {
      float v = buf[i];
      sum += v * v;
      buf[i] = static_cast<int16_t>(buf[i] * vol / 100);
    }
    levelNow = sqrtf(sum / n) / 32768.0f;
    lastSoundMs = millis();
    spkI2s.write(reinterpret_cast<uint8_t*>(buf), n * sizeof(int16_t));
  }
}

void micTask(void*) {
  static int32_t raw[320];  // 20 ms
  static int16_t pcm[320];
  float prevIn = 0, prevOut = 0;  // DC-blocking filter state
  for (;;) {
    size_t n = micI2s.readBytes(reinterpret_cast<char*>(raw), sizeof(raw)) / sizeof(int32_t);
    if (n == 0) {
      vTaskDelay(1);
      continue;
    }
    for (size_t i = 0; i < n; ++i) {
      float in = static_cast<float>(raw[i] >> MIC_SHIFT);
      float out = in - prevIn + 0.995f * prevOut;  // the INMP441 has a DC offset; this removes it
      prevIn = in;
      prevOut = out;
      pcm[i] = static_cast<int16_t>(constrain(out, -32767.0f, 32767.0f));
    }
    bool live = micOn && !playingFlag && millis() - lastSoundMs.load() > ECHO_TAIL_MS;
    if (live) micRing.write(reinterpret_cast<uint8_t*>(pcm), n * sizeof(int16_t));
  }
}

}  // namespace

namespace audio {

bool begin() {
  if (!micRing.begin(32768, false) || !spkRing.begin(1 << 20, true)) {
    Serial.println("audio: buffer allocation failed");
    return false;
  }

  micI2s.setPins(PIN_MIC_SCK, PIN_MIC_WS, -1, PIN_MIC_SD);
  if (!micI2s.begin(I2S_MODE_STD, MIC_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("audio: mic I2S failed");
    return false;
  }
  micI2s.setTimeout(100);

  // Mono, sent on both slots: the MAX98357A (SD pin unconnected) plays the average of left and right.
  spkI2s.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  if (!spkI2s.begin(I2S_MODE_STD, SPEAKER_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH)) {
    Serial.println("audio: speaker I2S failed");
    return false;
  }

  xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(speakerTask, "speaker", 4096, nullptr, 6, nullptr, 0);
  return true;
}

void setMicEnabled(bool on) { micOn = on; }

size_t micAvailable() { return micRing.used(); }

size_t readMic(uint8_t* dst, size_t maxBytes) { return micRing.read(dst, maxBytes); }

void playBegin() {
  // A stop that the speaker task hasn't handled yet would eat the new reply.
  for (int i = 0; i < 50 && flushReq.load(); ++i) vTaskDelay(1);
  doneFlag = false;
  streamEnded = false;
  playingFlag = true;
}

size_t playFeed(const uint8_t* pcm, size_t len) {
  size_t wrote = spkRing.write(pcm, len & ~size_t(1));
  if (wrote < (len & ~size_t(1))) Serial.println("audio: speaker buffer full, dropped some");
  return wrote;
}

void playEnd() { streamEnded = true; }

void playStop() {
  flushReq = true;
  doneFlag = false;
}

bool playing() { return playingFlag; }

bool takePlaybackDone() { return doneFlag.exchange(false); }

float level() { return levelNow; }

void setVolume(uint8_t percent) { volume = min<uint8_t>(percent, 100); }

void chirp() { chirpReq = true; }

}  // namespace audio
