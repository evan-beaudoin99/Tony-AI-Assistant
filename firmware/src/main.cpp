/*
  Tony: a wisecracking desktop assistant from Brooklyn.

  This is his body. It streams the mic to the Tony server, plays back what the server says, shows his face (or a
  card, like the weather), and sends camera frames when asked, but only while you've got the camera switched on.

  The button (BOOT, or one wired from GPIO0 to GND):
    tap         talk to Tony without saying his name (or cut him off mid-sentence)
    double tap  camera on / off (remembered across restarts; the LED glows red while it's on)
    hold        sleep / wake (asleep, the mic and camera are both off)
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "Audio.h"
#include "Battery.h"
#include "Button.h"
#include "Camera.h"
#include "Display.h"
#include "Net.h"
#include "TonyFace.h"
#include "config.h"
#include "pins.h"

namespace {

TonyDisplay lcd;
Screen screen(lcd);
TonyFace face(lcd, 0, ui::FACE_Y, ui::W, ui::FACE_H);
Button button;
Preferences prefs;

enum class View { Face, Card };
View view = View::Face;
uint32_t cardUntilMs = 0;

struct IncomingCard {
  uint8_t* buf = nullptr;
  size_t size = 0;
  size_t got = 0;
  uint16_t seconds = 0;
} card;

bool asleep = false;
bool cameraWanted = false;  // your choice; the camera itself is also off while Tony sleeps
bool listening = false;
battery::Reading lastBattery;
uint32_t lastBatteryMs = 0;
uint32_t lastFrameMs = 0;

String stateCaption;
String flashCaption;
uint32_t flashUntilMs = 0;

// ── helpers ─────────────────────────────────────────────────────────────────

void flash(const char* text, uint32_t ms = 2500) {
  flashCaption = text;
  flashUntilMs = millis() + ms;
}

void send(const char* type, const char* key = nullptr, bool value = false) {
  JsonDocument d;
  d["type"] = type;
  if (key) d[key] = value;
  net::sendJson(d);
}

void updateLed() { rgbLedWrite(PIN_RGB_LED, cam::isOn() ? 40 : 0, 0, 0); }

void reportCamera() { send("camera", "on", cam::isOn()); }

void showFace() {
  if (view == View::Face) return;
  view = View::Face;
  screen.clear();
  face.forceRedraw();
}

void showCard() {
  view = View::Card;
  if (!screen.showJpeg(card.buf, card.size)) {
    Serial.println("card: couldn't decode JPEG");
    showFace();
    return;
  }
  cardUntilMs = millis() + (card.seconds ? card.seconds : 60) * 1000UL;
}

void setCamera(bool on) {
  cameraWanted = on;
  prefs.putBool("camera", on);
  if (on && !asleep) {
    if (!cam::begin()) flash("Camera's on the fritz. Check the ribbon.", 4000);
  } else {
    cam::end();
  }
  updateLed();
  reportCamera();
}

void setSleep(bool on) {
  if (on == asleep) return;
  asleep = on;
  face.setAsleep(on);
  if (on) {
    audio::playStop();
    cam::end();
    showFace();
  } else if (cameraWanted) {
    cam::begin();
  }
  updateLed();
  send("sleep", "on", on);
  reportCamera();
}

void applyServerState(const char* s) {
  listening = strcmp(s, "listening") == 0;
  if (asleep) return;
  if (listening) {
    face.setEmotion(Emotion::Curious);
    stateCaption = "I'm listenin'...";
  } else if (strcmp(s, "thinking") == 0) {
    face.setEmotion(Emotion::Thinking);
    stateCaption = "Lemme think...";
  } else if (strcmp(s, "speaking") == 0) {
    stateCaption = "";
  } else {
    face.setEmotion(Emotion::Neutral);
    stateCaption = "";
  }
}

void sendSnapshot() {
  camera_fb_t* fb = cam::grab();
  if (!fb) {
    JsonDocument d;
    d["type"] = "snap_failed";
    d["reason"] = cam::isOn() ? "capture failed" : "camera off";
    net::sendJson(d);
    return;
  }
  net::sendBinary(net::CAMERA_FRAME, fb->buf, fb->len);
  cam::release(fb);
}

void sendHello() {
  JsonDocument d;
  d["type"] = "hello";
  d["fw"] = TONY_FW_VERSION;
  d["camera"] = cam::isOn();
  d["asleep"] = asleep;
  if (lastBattery.present) {
    d["battery"]["volts"] = lastBattery.volts;
    d["battery"]["percent"] = lastBattery.percent;
  }
  net::sendJson(d);
}

void sendBattery() {
  if (!lastBattery.present) return;
  JsonDocument d;
  d["type"] = "battery";
  d["volts"] = lastBattery.volts;
  d["percent"] = lastBattery.percent;
  net::sendJson(d);
}

// ── server events ───────────────────────────────────────────────────────────

void onConnected() {
  sendHello();
  if (!asleep) {
    face.setEmotion(Emotion::Happy);
    stateCaption = "";
  }
}

void onDisconnected() {
  audio::playStop();
  listening = false;
  if (!asleep) {
    face.setEmotion(Emotion::Confused);
    stateCaption = "Lookin' for my server...";
  }
}

void onMessage(JsonDocument& doc) {
  const char* type = doc["type"] | "";
  if (!strcmp(type, "state")) {
    applyServerState(doc["state"] | "idle");
  } else if (!strcmp(type, "emotion")) {
    Emotion e;
    if (!asleep && emotionFromName(doc["name"] | "", e)) face.setEmotion(e);
  } else if (!strcmp(type, "audio_start")) {
    if (asleep) setSleep(false);
    audio::playBegin();
  } else if (!strcmp(type, "audio_end")) {
    audio::playEnd();
  } else if (!strcmp(type, "audio_stop")) {
    audio::playStop();
  } else if (!strcmp(type, "card")) {
    heap_caps_free(card.buf);
    card.size = doc["bytes"] | 0;
    card.seconds = doc["seconds"] | 15;
    card.got = 0;
    card.buf = card.size ? static_cast<uint8_t*>(heap_caps_malloc(card.size, MALLOC_CAP_SPIRAM)) : nullptr;
  } else if (!strcmp(type, "face")) {
    showFace();
  } else if (!strcmp(type, "camera")) {
    setCamera(doc["on"] | false);
  } else if (!strcmp(type, "snap")) {
    sendSnapshot();
  } else if (!strcmp(type, "sleep")) {
    setSleep(doc["on"] | false);
  } else if (!strcmp(type, "look")) {
    face.lookToward(doc["x"] | 0.0f, doc["y"] | 0.0f, doc["ms"] | 2000);
  } else if (!strcmp(type, "volume")) {
    uint8_t v = doc["value"] | 70;
    audio::setVolume(v);
    prefs.putUChar("volume", v);
  } else if (!strcmp(type, "caption")) {
    flash(doc["text"] | "", doc["ms"] | 3000);
  }
}

void onBinary(uint8_t kind, const uint8_t* data, size_t len) {
  if (kind == net::SPEAKER_AUDIO) {
    audio::playFeed(data, len);
  } else if (kind == net::CARD_CHUNK && card.buf) {
    size_t n = min(len, card.size - card.got);
    memcpy(card.buf + card.got, data, n);
    card.got += n;
    if (card.got >= card.size) showCard();
  }
}

// ── loop pieces ─────────────────────────────────────────────────────────────

void handleButton(uint32_t now) {
  switch (button.poll(now)) {
    case Button::Tap:
      if (asleep) {
        setSleep(false);
        break;
      }
      audio::playStop();
      audio::chirp();
      showFace();
      applyServerState("listening");
      {
        JsonDocument d;
        d["type"] = "button";
        d["event"] = "tap";
        net::sendJson(d);
      }
      break;
    case Button::DoubleTap:
      setCamera(!cameraWanted);
      if (!asleep) {
        face.setEmotion(cameraWanted ? Emotion::Surprised : Emotion::Sleepy);
        flash(cameraWanted ? "Camera's on. I see ya!" : "Camera's off. I ain't lookin'.");
      }
      break;
    case Button::Hold:
      setSleep(!asleep);
      break;
    default:
      break;
  }
}

void pumpMic() {
  static uint8_t frame[1280];  // 40 ms
  while (net::connected() && audio::micAvailable() >= sizeof(frame)) {
    audio::readMic(frame, sizeof(frame));
    net::sendBinary(net::MIC_AUDIO, frame, sizeof(frame));
  }
}

void updateScreen(uint32_t now) {
  if (view == View::Card) {
    if (now >= cardUntilMs) showFace();
    return;
  }
  StatusInfo s;
  s.wifi = WiFi.isConnected();
  s.server = net::connected();
  s.listening = listening;
  s.camera = cam::isOn();
  s.asleep = asleep;
  s.battery = lastBattery.present ? lastBattery.percent : -1;
  screen.status(s);

  const char* text = asleep ? "Zzz... hold the button to wake me."
                     : now < flashUntilMs ? flashCaption.c_str()
                     : !TONY_USE_USB && !WiFi.isConnected() ? "No WiFi. Check secrets.h."
                     : stateCaption.c_str();
  screen.caption(text);

  if (now - lastFrameMs >= 25) {
    lastFrameMs = now;
    bool talking = audio::playing() && !asleep;
    face.setTalking(talking, talking ? audio::level() : 0.0f);
    face.update(now);
  }
}

}  // namespace

void setup() {
#if TONY_USE_USB
  // The port carries the server link too: big buffers so audio doesn't stall the loop or get dropped.
  Serial.setRxBufferSize(16384);
  Serial.setTxBufferSize(8192);
  Serial.begin(TONY_USB_BAUD);
#else
  Serial.begin(115200);
#endif
  updateLed();
  screen.begin();
  screen.splash("Ay! Gimme a sec...");

  prefs.begin("tony", false);
  cameraWanted = prefs.getBool("camera", false);
  audio::setVolume(prefs.getUChar("volume", 70));

  if (!audio::begin()) screen.splash("Audio trouble. Check the wiring.");
  button.begin(PIN_BUTTON);
  lastBattery = battery::read();

#if TONY_USE_USB
  WiFi.mode(WIFI_OFF);
  screen.splash("Usin' the USB cable...");
#else
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("tony");
  WiFi.setSleep(false);  // power save adds lag to every packet
  net::watchWifi();
  static const net::WifiNet nets[] = {{WIFI_SSID, WIFI_PASSWORD}, {WIFI_SSID_BACKUP, WIFI_PASSWORD_BACKUP}};
  net::beginWifi(nets, 2);
  bool unset = strcmp(WIFI_SSID, "your-wifi-name") == 0;
  // Give each network (main, then backup if there is one) its full turn before carrying on.
  const char* trying = nullptr;
  int waitMs = (*WIFI_SSID_BACKUP ? 2 : 1) * 10000 + 500;
  for (int t = 0; t < waitMs && !WiFi.isConnected(); t += 100) {
    if (trying != net::wifiName()) {
      trying = net::wifiName();
      String line = String("Connectin' to ") + trying + "...";
      screen.splash(unset ? "Fill in include/secrets.h!" : line.c_str());
    }
    delay(100);
    net::loopWifi();
  }
  Serial.printf("wifi: %s \"%s\" %s\n", WiFi.isConnected() ? "connected to" : "not yet on", net::wifiName(),
                WiFi.localIP().toString().c_str());
#endif

  if (cameraWanted) cam::begin();
  updateLed();

  screen.clear();
  if (!face.begin()) {
    screen.splash("Not enough memory for my face!");
    while (true) delay(1000);
  }
  stateCaption = "Lookin' for my server...";

  net::Handlers h;
  h.connected = onConnected;
  h.disconnected = onDisconnected;
  h.message = onMessage;
  h.binary = onBinary;
  static const net::Server servers[] = {{TONY_SERVER_HOST, TONY_SERVER_PORT},
                                        {TONY_SERVER_HOST_BACKUP, TONY_SERVER_PORT_BACKUP}};
  net::begin(servers, 2, TONY_DEVICE_TOKEN, h);
}

void loop() {
  uint32_t now = millis();
  net::loop();
  handleButton(now);

  audio::setMicEnabled(net::connected() && !asleep);
  pumpMic();
  if (audio::takePlaybackDone()) send("playback_done");

  if (now - lastBatteryMs >= 30000) {
    lastBatteryMs = now;
    lastBattery = battery::read();
    sendBattery();
  }

  updateScreen(now);
}
