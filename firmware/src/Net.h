#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// The WebSocket to Tony's server. Text frames are JSON; binary frames start with a one-byte kind.
// The full protocol is in server/tony/protocol.py.
namespace net {

// Binary frame kinds
constexpr uint8_t MIC_AUDIO = 0x01;      // device -> server: 16 kHz 16-bit mono PCM
constexpr uint8_t CAMERA_FRAME = 0x02;   // device -> server: one JPEG
constexpr uint8_t SPEAKER_AUDIO = 0x10;  // server -> device: 24 kHz 16-bit mono PCM
constexpr uint8_t CARD_CHUNK = 0x11;     // server -> device: part of a JPEG card

struct Handlers {
  void (*connected)() = nullptr;
  void (*disconnected)() = nullptr;
  void (*message)(JsonDocument& doc) = nullptr;
  void (*binary)(uint8_t kind, const uint8_t* data, size_t len) = nullptr;
};

void begin(const char* host, uint16_t port, const char* token, const Handlers& handlers);
void loop();
bool connected();
bool sendJson(const JsonDocument& doc);
bool sendBinary(uint8_t kind, const uint8_t* data, size_t len);

}  // namespace net
