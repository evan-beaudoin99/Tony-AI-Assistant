// Test 2: the microphone, through the amplifier.
//
// Checks the INMP441 is really there and hears you (not just a floating wire), then turns into a loopback:
// tap the BOOT button (or press r) and Tony records 3 seconds, then plays them back on his speaker.
// The web page has a live level meter, record / play buttons, and plays the recording in your browser too,
// so you can tell a mic problem from a speaker problem.
//
// Same mic settings as Tony's firmware (firmware/src/Audio.cpp). Serial keys:
//   r  record 3 s and play back     p  play it again     m  level meter on / off
//   +  more mic gain   -  less mic gain   (tells you the MIC_SHIFT to put in Audio.cpp)

#include <Arduino.h>
#include <ESP_I2S.h>
#include <WebServer.h>

#include <atomic>

#include "Button.h"
#include "TestKit.h"
#include "pins.h"

namespace {

constexpr const char* HOST = "tony-mic";
constexpr uint32_t RATE = 16000;  // the mic's rate; the amp runs at it too here, so playback needs no resampling
constexpr uint32_t REC_SECONDS = 3;
constexpr size_t REC_SAMPLES = RATE * REC_SECONDS;
constexpr size_t BLOCK = 320;  // 20 ms

I2SClass micI2s;
I2SClass spkI2s;
WebServer web(80);
Button button;

int16_t* recBuf = nullptr;  // in PSRAM
std::atomic<int> micShift{12};  // the INMP441 sends 24 bits left-justified in 32; >> 16 is unity gain
std::atomic<size_t> recLen{0};
std::atomic<bool> recording{false};
std::atomic<bool> playAfterRecord{false};
std::atomic<bool> playReq{false};
std::atomic<bool> playing{false};
std::atomic<uint8_t> volume{70};
std::atomic<float> rmsDb{-120};
std::atomic<float> peakDb{-120};
bool meterOn = true;

float toDb(float v) { return v > 0 ? 20.0f * log10f(v / 32768.0f) : -120.0f; }

// Reads one block, DC-blocked and scaled exactly like Audio.cpp. Returns how many samples came out.
size_t readBlock(int16_t* pcm, int32_t* raw, float& prevIn, float& prevOut) {
  size_t n = micI2s.readBytes(reinterpret_cast<char*>(raw), BLOCK * sizeof(int32_t)) / sizeof(int32_t);
  int shift = micShift;
  for (size_t i = 0; i < n; ++i) {
    float in = static_cast<float>(raw[i] >> shift);
    float out = in - prevIn + 0.995f * prevOut;
    prevIn = in;
    prevOut = out;
    pcm[i] = static_cast<int16_t>(constrain(out, -32767.0f, 32767.0f));
  }
  return n;
}

void micTask(void*) {
  static int32_t raw[BLOCK];
  static int16_t pcm[BLOCK];
  float prevIn = 0, prevOut = 0;
  float rms = 0, peak = 0;
  for (;;) {
    size_t n = readBlock(pcm, raw, prevIn, prevOut);
    if (n == 0) {
      vTaskDelay(1);
      continue;
    }
    float sum = 0, blockPeak = 0;
    for (size_t i = 0; i < n; ++i) {
      float v = pcm[i];
      sum += v * v;
      blockPeak = max(blockPeak, fabsf(v));
    }
    rms = rms * 0.7f + sqrtf(sum / n) * 0.3f;
    peak = max(blockPeak, peak * 0.93f);  // falls ~25 dB a second, like a real meter
    rmsDb = toDb(rms);
    peakDb = toDb(peak);

    if (recording) {
      size_t at = recLen;
      size_t take = min(n, REC_SAMPLES - at);
      memcpy(recBuf + at, pcm, take * sizeof(int16_t));
      recLen = at + take;
      if (recLen >= REC_SAMPLES) {
        recording = false;
        Serial.println("mic: recorded 3 s");
        if (playAfterRecord.exchange(false)) playReq = true;
      }
    }
  }
}

// Keeps the amp fed: the recording when asked, silence otherwise (a starved amp clicks and buzzes).
void speakerTask(void*) {
  static int16_t buf[BLOCK];
  for (;;) {
    if (playReq.exchange(false)) {
      playing = true;
      size_t len = recLen;
      for (size_t at = 0; at < len; at += BLOCK) {
        size_t n = min(BLOCK, len - at);
        int32_t vol = volume;
        for (size_t i = 0; i < n; ++i) buf[i] = static_cast<int16_t>(recBuf[at + i] * vol / 100);
        spkI2s.write(reinterpret_cast<uint8_t*>(buf), n * sizeof(int16_t));
      }
      playing = false;
      continue;
    }
    memset(buf, 0, sizeof(buf));
    spkI2s.write(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
  }
}

bool startRecording(bool thenPlay) {
  if (recording || playing) return false;
  recLen = 0;
  playAfterRecord = thenPlay;
  recording = true;
  Serial.println("mic: recording 3 s... talk now!");
  return true;
}

bool replay() {
  if (recording || playing || recLen == 0) return false;
  Serial.println("speaker: playing the recording");
  playReq = true;
  return true;
}

void meterLine() {
  float r = rmsDb, p = peakDb;
  int bars = constrain(static_cast<int>((r + 90) / 2), 0, 45);  // -90 dBFS .. 0 dBFS
  char bar[46];
  memset(bar, '#', bars);
  memset(bar + bars, '.', 45 - bars);
  bar[45] = 0;
  Serial.printf("level %6.1f dBFS  peak %6.1f  |%s|%s%s\n", r, p, bar, p > -1.0f ? " CLIPPING" : "",
                recording ? "  REC" : (playing ? "  PLAY" : ""));
}

const char PAGE[] = R"html(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tony mic test</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font: 15px system-ui, sans-serif; background: #111; color: #eee; }
  main { max-width: 640px; margin: 0 auto; padding: 16px; }
  h1 { font-size: 20px; margin: 0 0 16px; }
  .meter { position: relative; height: 28px; background: #222; border-radius: 6px; overflow: hidden; }
  #rms { position: absolute; inset: 0 auto 0 0; background: linear-gradient(90deg, #2a6, #2a6 70%, #db3 85%, #d33); }
  #peak { position: absolute; top: 0; bottom: 0; width: 3px; background: #fff; }
  .scale { display: flex; justify-content: space-between; color: #888; font-size: 12px; margin: 4px 0 16px; }
  #nums { font-family: ui-monospace, monospace; color: #9a9; margin-bottom: 16px; }
  .row { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; margin-bottom: 16px; }
  button { font: inherit; padding: 8px 14px; border-radius: 6px; border: 1px solid #444; background: #222; color: #eee; }
  button:hover { background: #333; }
  #state { font-weight: 600; }
  audio { width: 100%; }
  p { color: #aaa; line-height: 1.5; }
</style></head><body><main>
<h1>Tony mic test</h1>
<div class="meter"><div id="rms"></div><div id="peak"></div></div>
<div class="scale"><span>-90 dB</span><span>-60</span><span>-30</span><span>0 dBFS</span></div>
<div id="nums">...</div>
<div class="row">
  <button id="rec">Record 3 s, play on Tony</button>
  <button id="play">Play again on Tony</button>
  <button id="listen">Play here in the browser</button>
  <span id="state"></span>
</div>
<div class="row">
  <label>Speaker volume <input id="vol" type="range" min="0" max="100"></label>
  <label>Mic gain <button id="gdown">-</button> <span id="gain"></span> <button id="gup">+</button></label>
</div>
<audio id="audio" controls></audio>
<p>Quiet room: roughly -70 to -45 dB. Talking at arm's length: -35 to -15 dB. If it never moves, the mic isn't
hearing anything. If the browser playback sounds right but Tony's speaker doesn't, the mic is fine and the
amp or speaker is the problem.</p>
<script>
const $ = id => document.getElementById(id);
const pct = db => Math.max(0, Math.min(100, (db + 90) / 90 * 100));
let volSet = false;
async function poll() {
  try {
    const s = await (await fetch('/level')).json();
    $('rms').style.width = pct(s.rms) + '%';
    $('peak').style.left = pct(s.peak) + '%';
    $('nums').textContent = `level ${s.rms.toFixed(1)} dBFS   peak ${s.peak.toFixed(1)} dBFS` +
      (s.peak > -1 ? '   CLIPPING' : '');
    $('state').textContent = s.recording ? 'Recording... talk!' : s.playing ? 'Playing on Tony' : '';
    $('gain').textContent = `MIC_SHIFT ${s.shift}`;
    if (!volSet) { $('vol').value = s.volume; volSet = true; }
  } catch { $('nums').textContent = 'lost the ESP32...'; }
  setTimeout(poll, 100);
}
poll();
const post = path => fetch(path, { method: 'POST' });
$('rec').onclick = () => post('/record');
$('play').onclick = () => post('/play');
$('listen').onclick = () => { $('audio').src = '/recording.wav?t=' + Date.now(); $('audio').play(); };
$('vol').onchange = e => post('/volume?val=' + e.target.value);
$('gup').onclick = () => post('/gain?val=-1');
$('gdown').onclick = () => post('/gain?val=1');
</script></main></body></html>)html";

void sendWav() {
  if (recording || recLen == 0) {
    web.send(409, "text/plain", "nothing recorded yet");
    return;
  }
  uint32_t bytes = recLen * sizeof(int16_t);
  uint8_t h[44];
  auto put32 = [&](int at, uint32_t v) { memcpy(h + at, &v, 4); };
  auto put16 = [&](int at, uint16_t v) { memcpy(h + at, &v, 2); };
  memcpy(h, "RIFF", 4);
  put32(4, 36 + bytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, 1);  // PCM
  put16(22, 1);  // mono
  put32(24, RATE);
  put32(28, RATE * 2);
  put16(32, 2);
  put16(34, 16);
  memcpy(h + 36, "data", 4);
  put32(40, bytes);

  web.setContentLength(sizeof(h) + bytes);
  web.send(200, "audio/wav", "");
  web.sendContent(reinterpret_cast<const char*>(h), sizeof(h));
  const char* data = reinterpret_cast<const char*>(recBuf);
  for (uint32_t at = 0; at < bytes; at += 4096) web.sendContent(data + at, min<uint32_t>(4096, bytes - at));
}

void changeGain(int delta) {
  micShift = constrain(micShift + delta, 8, 16);
  Serial.printf("mic: MIC_SHIFT is now %d (put this in firmware/src/Audio.cpp to keep it)\n", micShift.load());
}

void startWeb() {
  web.on("/", HTTP_GET, [] { web.send(200, "text/html", PAGE); });
  web.on("/level", HTTP_GET, [] {
    char json[160];
    snprintf(json, sizeof(json), "{\"rms\":%.1f,\"peak\":%.1f,\"recording\":%d,\"playing\":%d,\"shift\":%d,\"volume\":%d}",
             rmsDb.load(), peakDb.load(), recording.load(), playing.load(), micShift.load(), volume.load());
    web.send(200, "application/json", json);
  });
  web.on("/record", HTTP_POST, [] { web.send(startRecording(true) ? 200 : 409, "text/plain", ""); });
  web.on("/play", HTTP_POST, [] { web.send(replay() ? 200 : 409, "text/plain", ""); });
  web.on("/volume", HTTP_POST, [] {
    volume = constrain(web.arg("val").toInt(), 0, 100);
    web.send(200, "text/plain", "");
  });
  web.on("/gain", HTTP_POST, [] {
    changeGain(web.arg("val").toInt());
    web.send(200, "text/plain", "");
  });
  web.on("/recording.wav", HTTP_GET, sendWav);
  web.begin();
}

// Before the mic task starts: is anything actually talking on the data line, and does it react to sound?
void runMicChecks() {
  char msg[160];
  static int32_t raw[BLOCK];
  static int16_t pcm[BLOCK];
  float prevIn = 0, prevOut = 0;

  // Let the INMP441 wake up and the DC filter settle.
  uint32_t settle = millis();
  while (millis() - settle < 300) readBlock(pcm, raw, prevIn, prevOut);

  size_t total = 0, zeros = 0, ones = 0;
  int32_t lo = INT32_MAX, hi = INT32_MIN;
  double sum = 0;
  uint32_t start = millis();
  while (millis() - start < 1000) {
    size_t n = readBlock(pcm, raw, prevIn, prevOut);
    for (size_t i = 0; i < n; ++i) {
      if (raw[i] == 0) ++zeros;
      if (raw[i] == -1) ++ones;
      lo = min(lo, raw[i]);
      hi = max(hi, raw[i]);
      sum += static_cast<double>(pcm[i]) * pcm[i];
    }
    total += n;
  }
  float seconds = (millis() - start) / 1000.0f;

  snprintf(msg, sizeof(msg), "Mic I2S is clocking (%u samples in %.1f s, want ~%u)", (unsigned)total, seconds,
           (unsigned)(RATE * seconds));
  check::expect(total > RATE * seconds * 0.9f, msg, "Too few samples: the I2S clock isn't running right.");

  bool dead = total == 0 || zeros == total || ones == total || lo == hi;
  snprintf(msg, sizeof(msg), "Mic is sending data (raw %ld .. %ld)", (long)lo, (long)hi);
  if (!check::expect(!dead, msg,
                     "Data line stuck. Check SD -> GPIO 3, SCK -> GPIO 41, WS -> GPIO 42, VDD -> 3V3 (never 5 V), "
                     "GND. L/R must go to GND: tied to 3V3 the mic talks on the right channel, which nobody reads.")) {
    return;
  }

  float floorDb = toDb(sqrtf(sum / max<size_t>(total, 1)));
  snprintf(msg, sizeof(msg), "Quiet-room noise floor: %.1f dBFS", floorDb);
  check::info(msg);
  if (floorDb > -25) {
    check::warn("That's very loud for silence. A noisy room, or the mic is picking up hum: keep its wires away "
                "from the amp and speaker.");
  } else if (floorDb < -95) {
    check::warn("Suspiciously silent. The mic may be half-connected (check VDD and GND).");
  }

  Serial.println("\n  >>> CLAP, or say \"Hey Tony\", in the next 4 seconds <<<");
  float loudest = -120;
  start = millis();
  while (millis() - start < 4000) {
    size_t n = readBlock(pcm, raw, prevIn, prevOut);
    float s2 = 0;
    for (size_t i = 0; i < n; ++i) s2 += static_cast<float>(pcm[i]) * pcm[i];
    if (n) loudest = max(loudest, toDb(sqrtf(s2 / n)));
  }
  snprintf(msg, sizeof(msg), "Mic heard you: loudest %.1f dBFS, %.1f dB over the quiet", loudest, loudest - floorDb);
  check::expect(loudest - floorDb >= 15, msg,
                "No real jump. Either it was too quiet (try again, closer), or the mic's sound hole is blocked.");
  if (loudest > -1) check::warn("That clipped. If normal talking clips too, raise MIC_SHIFT (less gain).");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  check::title("MICROPHONE + AMPLIFIER");
  check::lastReset();
  button.begin(PIN_BUTTON);

  recBuf = static_cast<int16_t*>(ps_malloc(REC_SAMPLES * sizeof(int16_t)));
  check::expect(recBuf != nullptr, "Recording buffer in PSRAM", "No PSRAM? Check the board settings in platformio.ini.");

  micI2s.setPins(PIN_MIC_SCK, PIN_MIC_WS, -1, PIN_MIC_SD);
  bool micOk = micI2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  check::expect(micOk, "Mic I2S started (SCK 41, WS 42, SD 3)");
  micI2s.setTimeout(100);

  spkI2s.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  bool spkOk = spkI2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH);
  check::expect(spkOk, "Amp I2S started (BCLK 38, LRC 39, DIN 40)");

  if (micOk && recBuf) runMicChecks();
  if (spkOk && recBuf) xTaskCreatePinnedToCore(speakerTask, "speaker", 4096, nullptr, 6, nullptr, 0);
  if (micOk && recBuf) xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 5, nullptr, 0);

  bool netOk = testwifi::connect(HOST);
  if (netOk) startWeb();
  check::summary();

  Serial.println("  Now: tap BOOT (or press r) to record 3 s and hear it back on the speaker.");
  Serial.println("  Keys: r record+play, p play again, m meter on/off, + more gain, - less gain");
  if (netOk) testwifi::printAddress(HOST);
  else Serial.println("  No WiFi, so no web page: the button and serial keys still work.");
}

void loop() {
  web.handleClient();

  if (button.poll(millis()) == Button::Tap) startRecording(true);

  while (Serial.available()) {
    switch (Serial.read()) {
      case 'r': startRecording(true); break;
      case 'p': if (!replay()) Serial.println("nothing to play yet (press r first)"); break;
      case 'm':
        meterOn = !meterOn;
        Serial.printf("meter %s\n", meterOn ? "on" : "off");
        break;
      case '+': changeGain(-1); break;
      case '-': changeGain(1); break;
    }
  }

  static uint32_t lastMeterMs = 0;
  if (meterOn && millis() - lastMeterMs >= 250) {
    lastMeterMs = millis();
    meterLine();
  }
  delay(2);
}
