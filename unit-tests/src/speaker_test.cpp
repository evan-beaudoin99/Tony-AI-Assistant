// Test 3: the speaker.
//
// Plays a set of test sounds through the MAX98357A, with the same I2S settings as Tony's firmware
// (firmware/src/Audio.cpp: 24 kHz, 16-bit, mono on both slots). Runs every sound once at start-up, telling you
// on the serial monitor what you should hear. Then:
//   1-5  play one sound     a  all of them     s  stop     +/-  volume     h  this menu
//   BOOT button: tap = next sound, hold = stop

#include <Arduino.h>
#include <ESP_I2S.h>

#include <atomic>

#include "Button.h"
#include "TestKit.h"
#include "pins.h"

namespace {

constexpr uint32_t RATE = 24000;
constexpr size_t BLOCK = 240;  // 10 ms
constexpr int SOUND_COUNT = 5;
constexpr int ALL = 99;

I2SClass spkI2s;
Button button;

std::atomic<int> request{0};  // which sound to play next; 0 = none
std::atomic<bool> stopReq{false};
std::atomic<bool> busy{false};
std::atomic<uint8_t> volume{60};
float phase = 0;  // carried across notes so joins don't click

struct Sound {
  const char* name;
  const char* expect;
};

const Sound SOUNDS[SOUND_COUNT] = {
    {"Beep: 440 Hz for 1 s",
     "A clean, steady beep. Buzzing or rattling: the speaker is loose or touching something."},
    {"Sweep: 100 Hz -> 8 kHz over 4 s",
     "A smooth rising whistle. Small speakers are faint below ~300 Hz, that's normal. A sudden rattle at one\n"
     "         pitch means something in the case is vibrating."},
    {"Volume steps: 1 kHz at 10, 25, 50, 75, 100 %",
     "Five beeps, each clearly louder. Crackle on the loudest, or the board resets: the 5 V rail is sagging,\n"
     "         add the 470 uF capacitor at the amp."},
    {"Tune: a little arpeggio, up and back down",
     "Eight clean notes. Notes that blur together or warble: I2S timing trouble, keep BCLK/LRC/DIN short."},
    {"Tony's chirp: the \"I'm listening\" blip",
     "A short rising blip, the same one Tony makes when he hears his name."},
};

void write(int16_t* buf, size_t n) { spkI2s.write(reinterpret_cast<uint8_t*>(buf), n * sizeof(int16_t)); }

// A tone, optionally gliding from hz to hzEnd, with 5 ms fades so it starts and stops without a click.
// level is 0..1 of full scale, before the master volume. Returns false if a stop came in.
bool tone(float hz, float hzEnd, float seconds, float level, bool logGlide = false) {
  static int16_t buf[BLOCK];
  const size_t total = static_cast<size_t>(seconds * RATE);
  const size_t fade = RATE / 200;
  for (size_t done = 0; done < total; done += BLOCK) {
    if (stopReq) return false;
    size_t n = min(BLOCK, total - done);
    float amp = level * 32000.0f * volume.load() / 100.0f;
    for (size_t i = 0; i < n; ++i) {
      size_t k = done + i;
      float t = static_cast<float>(k) / total;
      float f = logGlide ? hz * powf(hzEnd / hz, t) : hz + (hzEnd - hz) * t;
      float env = min(1.0f, min(static_cast<float>(k) / fade, static_cast<float>(total - k) / fade));
      phase += TWO_PI * f / RATE;
      if (phase > TWO_PI) phase -= TWO_PI;
      buf[i] = static_cast<int16_t>(sinf(phase) * amp * env);
    }
    write(buf, n);
  }
  return true;
}

bool silence(float seconds) { return tone(0, 0, seconds, 0); }

bool play(int which) {
  switch (which) {
    case 1:
      return tone(440, 440, 1.0f, 0.3f);
    case 2:
      return tone(100, 8000, 4.0f, 0.3f, true);
    case 3:
      for (float pct : {0.10f, 0.25f, 0.50f, 0.75f, 1.00f}) {
        Serial.printf("         %d %%\n", static_cast<int>(pct * 100));
        if (!tone(1000, 1000, 0.5f, 0.9f * pct) || !silence(0.3f)) return false;
      }
      return true;
    case 4: {
      // C major arpeggio, up and back down
      const float notes[] = {261.63f, 329.63f, 392.00f, 523.25f, 659.25f, 523.25f, 392.00f, 261.63f};
      for (float hz : notes) {
        if (!tone(hz, hz, 0.18f, 0.3f) || !silence(0.04f)) return false;
      }
      return true;
    }
    case 5: {
      // Exactly Audio.cpp's chirp: 120 ms sliding up a fifth, sine envelope, 5000 peak at full volume.
      static int16_t buf[BLOCK];
      const float total = RATE * 0.12f;
      float ph = 0;
      for (size_t done = 0; done < total; done += BLOCK) {
        for (size_t i = 0; i < BLOCK; ++i) {
          float t = (done + i) / total;
          float env = t < 1.0f ? sinf(PI * t) : 0.0f;
          ph += TWO_PI * (880.0f + 440.0f * t) / RATE;
          buf[i] = static_cast<int16_t>(sinf(ph) * env * 5000.0f * volume.load() / 100.0f);
        }
        write(buf, BLOCK);
      }
      return true;
    }
  }
  return true;
}

bool playAnnounced(int which) {
  const Sound& s = SOUNDS[which - 1];
  Serial.printf("\n  %d. %s\n     You should hear: %s\n", which, s.name, s.expect);
  bool done = play(which);
  if (!done) Serial.println("     (stopped)");
  return done;
}

void speakerTask(void*) {
  static int16_t quiet[BLOCK] = {};
  for (;;) {
    int which = request.exchange(0);
    if (which == 0) {
      write(quiet, BLOCK);  // a starved amp clicks and buzzes, so keep it fed
      continue;
    }
    stopReq = false;
    busy = true;
    if (which == ALL) {
      for (int i = 1; i <= SOUND_COUNT; ++i) {
        if (!playAnnounced(i) || !silence(0.8f)) break;
      }
    } else {
      playAnnounced(which);
    }
    busy = false;
  }
}

void ask(int which) {
  if (busy) {
    stopReq = true;
    while (busy) delay(1);
  }
  request = which;
}

void help() {
  Serial.println("\n  Keys:");
  for (int i = 0; i < SOUND_COUNT; ++i) Serial.printf("    %d  %s\n", i + 1, SOUNDS[i].name);
  Serial.println("    a  all of them\n    s  stop\n    +  louder   -  quieter\n    h  this menu");
  Serial.printf("  BOOT button: tap = next sound, hold = stop.   Volume %d %%\n", volume.load());
}

void setVolume(int v) {
  volume = constrain(v, 0, 100);
  Serial.printf("volume %d %%\n", volume.load());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  check::title("SPEAKER");
  check::lastReset();
  button.begin(PIN_BUTTON);

  spkI2s.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  bool ok = spkI2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH);
  check::expect(ok, "Amp I2S started (BCLK 38, LRC 39, DIN 40)",
                "The I2S driver refused the pins. Did something else in pins.h move onto 38-40?");
  check::info("Everything else here is by ear: the amp can't tell the ESP32 whether a speaker is attached.");
  check::info("Hear nothing at all? Amp VIN on the 5 V rail (battery switch on, or USB 5V), GND shared, speaker");
  check::info("on OUT+ / OUT- (neither to GND), and the microSD slot empty: it shares pins 38-40 with the amp.");
  check::summary();
  if (!ok) return;

  xTaskCreatePinnedToCore(speakerTask, "speaker", 4096, nullptr, 6, nullptr, 0);
  Serial.println("  Playing every test sound once...");
  request = ALL;
  delay(100);
  while (busy) delay(10);
  Serial.println("\n  Done. If every sound matched its description, the speaker is good.");
  help();
}

void loop() {
  static int next = 1;
  switch (button.poll(millis())) {
    case Button::Tap:
      ask(next);
      next = next % SOUND_COUNT + 1;
      break;
    case Button::Hold:
      stopReq = true;
      break;
    default:
      break;
  }

  while (Serial.available()) {
    int c = Serial.read();
    if (c >= '1' && c < '1' + SOUND_COUNT) ask(c - '0');
    else if (c == 'a') ask(ALL);
    else if (c == 's') stopReq = true;
    else if (c == '+') setVolume(volume + 10);
    else if (c == '-') setVolume(volume - 10);
    else if (c == 'h') help();
  }
  delay(5);
}
