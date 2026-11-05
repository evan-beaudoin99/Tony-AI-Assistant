// The USB transport (TONY_USE_USB true): the same net:: API as Net.cpp, over the USB-UART serial port instead of
// a WebSocket. On the laptop, `python -m tony.usb_bridge` relays these frames to the server's /ws/device.
//
// Each frame: A5 5A, kind ('T' JSON text, 'B' binary frame, 'H' heartbeat), u32 little-endian payload length,
// the payload, then the low byte of the payload's sum. Anything else on the port is plain log text, which the
// bridge prints. Both sides send a heartbeat every second; the bridge's carries one byte, 1 when it's connected
// to the server.
#include "config.h"

#if TONY_USE_USB

#include <esp_heap_caps.h>

#include "Net.h"

namespace {

constexpr uint8_t SYNC0 = 0xA5;
constexpr uint8_t SYNC1 = 0x5A;
constexpr uint8_t KIND_TEXT = 'T';
constexpr uint8_t KIND_BIN = 'B';
constexpr uint8_t KIND_BEAT = 'H';
constexpr size_t MAX_FRAME = 32 * 1024;  // the server never sends more than ~15 KB at once
constexpr uint32_t BEAT_EVERY_MS = 1000;
constexpr uint32_t BRIDGE_TIMEOUT_MS = 3500;
constexpr uint32_t HINT_EVERY_MS = 10000;

net::Handlers handlers;
bool up = false;
bool linkUp = false;  // the bridge's last word on its server connection
bool heardBridge = false;
uint32_t lastHeardMs = 0;
uint32_t lastBeatMs = 0;
uint32_t lastHintMs = 0;

enum class Rx { Sync0, Sync1, Kind, Len, Body, Sum };
Rx rx = Rx::Sync0;
uint8_t rxKind = 0;
uint32_t rxLen = 0;
int rxLenBytes = 0;
uint32_t rxGot = 0;
uint8_t rxSum = 0;
uint8_t* rxBuf = nullptr;

void writeFrame(uint8_t kind, const uint8_t* a, size_t alen, const uint8_t* b = nullptr, size_t blen = 0) {
  uint32_t len = alen + blen;
  uint8_t head[7] = {SYNC0, SYNC1, kind, uint8_t(len), uint8_t(len >> 8), uint8_t(len >> 16), uint8_t(len >> 24)};
  uint8_t sum = 0;
  for (size_t i = 0; i < alen; ++i) sum += a[i];
  for (size_t i = 0; i < blen; ++i) sum += b[i];
  Serial.write(head, sizeof(head));
  if (alen) Serial.write(a, alen);
  if (blen) Serial.write(b, blen);
  Serial.write(sum);
}

void updateUp() {
  bool now = linkUp && heardBridge && millis() - lastHeardMs < BRIDGE_TIMEOUT_MS;
  if (now == up) return;
  up = now;
  if (up) {
    Serial.println("usb: connected to the server through the bridge");
    if (handlers.connected) handlers.connected();
  } else {
    Serial.println(linkUp ? "usb: lost the bridge" : "usb: the bridge lost the server");
    if (handlers.disconnected) handlers.disconnected();
  }
}

void dispatch() {
  heardBridge = true;
  lastHeardMs = millis();
  switch (rxKind) {
    case KIND_BEAT:
      linkUp = rxLen >= 1 && rxBuf[0];
      updateUp();
      break;
    case KIND_TEXT: {
      if (!up || !handlers.message) break;
      JsonDocument doc;
      if (deserializeJson(doc, reinterpret_cast<const char*>(rxBuf), rxLen)) {
        Serial.println("usb: bad JSON from the bridge");
        break;
      }
      handlers.message(doc);
      break;
    }
    case KIND_BIN:
      if (up && rxLen >= 1 && handlers.binary) handlers.binary(rxBuf[0], rxBuf + 1, rxLen - 1);
      break;
  }
}

void feed(uint8_t c) {
  switch (rx) {
    case Rx::Sync0:
      if (c == SYNC0) rx = Rx::Sync1;
      break;
    case Rx::Sync1:
      rx = c == SYNC1 ? Rx::Kind : c == SYNC0 ? Rx::Sync1 : Rx::Sync0;
      break;
    case Rx::Kind:
      rxKind = c;
      rxLen = 0;
      rxLenBytes = 0;
      rx = (c == KIND_TEXT || c == KIND_BIN || c == KIND_BEAT) ? Rx::Len : Rx::Sync0;
      break;
    case Rx::Len:
      rxLen |= uint32_t(c) << (8 * rxLenBytes++);
      if (rxLenBytes == 4) {
        rxGot = 0;
        rxSum = 0;
        rx = rxLen > MAX_FRAME ? Rx::Sync0 : rxLen ? Rx::Body : Rx::Sum;
      }
      break;
    case Rx::Body:
      rxBuf[rxGot++] = c;
      rxSum += c;
      if (rxGot == rxLen) rx = Rx::Sum;
      break;
    case Rx::Sum:
      if (c == rxSum) {
        dispatch();
      } else {
        Serial.println("usb: damaged frame from the bridge, dropped");
      }
      rx = Rx::Sync0;
      break;
  }
}

}  // namespace

namespace net {

void begin(const Server*, int, const char*, const Handlers& h) {
  handlers = h;
  rxBuf = static_cast<uint8_t*>(heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rxBuf) rxBuf = static_cast<uint8_t*>(malloc(MAX_FRAME));
  lastHintMs = millis() - HINT_EVERY_MS + 3000;
  Serial.printf("usb: talking to the server over this cable at %d baud\n", TONY_USB_BAUD);
}

void loop() {
  uint8_t chunk[256];
  int avail;
  while (rxBuf && (avail = Serial.available()) > 0) {
    size_t n = Serial.read(chunk, min(avail, int(sizeof(chunk))));
    for (size_t i = 0; i < n; ++i) feed(chunk[i]);
  }
  uint32_t now = millis();
  if (now - lastBeatMs >= BEAT_EVERY_MS) {
    lastBeatMs = now;
    writeFrame(KIND_BEAT, nullptr, 0);
  }
  updateUp();
  if (!up && now - lastHintMs >= HINT_EVERY_MS) {
    lastHintMs = now;
    Serial.println(!heardBridge || now - lastHeardMs >= BRIDGE_TIMEOUT_MS
                       ? "usb: no bridge. On the laptop, run `python -m tony.usb_bridge` (and close any serial monitor)."
                       : "usb: the bridge is here but can't reach the server. Is `python -m tony` running?");
  }
}

bool connected() { return up; }

bool sendJson(const JsonDocument& doc) {
  if (!up) return false;
  String s;
  serializeJson(doc, s);
  writeFrame(KIND_TEXT, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
  return true;
}

bool sendBinary(uint8_t kind, const uint8_t* data, size_t len) {
  if (!up) return false;
  writeFrame(KIND_BIN, &kind, 1, data, len);
  return true;
}

}  // namespace net

#endif  // TONY_USE_USB
