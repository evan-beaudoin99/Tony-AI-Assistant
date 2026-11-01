#include "Net.h"

#include <WebSocketsClient.h>
#include <esp_heap_caps.h>

namespace {

WebSocketsClient ws;
net::Handlers handlers;
bool up = false;

uint8_t* scratch = nullptr;  // kind byte + payload, reused between sends
size_t scratchCap = 0;

void onEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      up = true;
      Serial.println("net: connected to server");
      if (handlers.connected) handlers.connected();
      break;
    case WStype_DISCONNECTED:
      if (up) {
        up = false;
        Serial.println("net: lost the server");
        if (handlers.disconnected) handlers.disconnected();
      }
      break;
    case WStype_TEXT: {
      JsonDocument doc;
      if (deserializeJson(doc, payload, length)) {
        Serial.println("net: bad JSON from server");
        return;
      }
      if (handlers.message) handlers.message(doc);
      break;
    }
    case WStype_BIN:
      if (length >= 1 && handlers.binary) handlers.binary(payload[0], payload + 1, length - 1);
      break;
    default:
      break;
  }
}

}  // namespace

namespace net {

void begin(const char* host, uint16_t port, const char* token, const Handlers& h) {
  handlers = h;
  String path = String("/ws/device?token=") + token;
  ws.begin(host, port, path.c_str());
  ws.onEvent(onEvent);
  ws.setReconnectInterval(2000);
  ws.enableHeartbeat(15000, 4000, 2);
}

void loop() { ws.loop(); }

bool connected() { return up; }

bool sendJson(const JsonDocument& doc) {
  if (!up) return false;
  String s;
  serializeJson(doc, s);
  return ws.sendTXT(s);
}

bool sendBinary(uint8_t kind, const uint8_t* data, size_t len) {
  if (!up) return false;
  if (len + 1 > scratchCap) {
    heap_caps_free(scratch);
    scratchCap = len + 1;
    scratch = static_cast<uint8_t*>(heap_caps_malloc(scratchCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!scratch) {
      scratchCap = 0;
      return false;
    }
  }
  scratch[0] = kind;
  memcpy(scratch + 1, data, len);
  return ws.sendBIN(scratch, len + 1);
}

}  // namespace net
