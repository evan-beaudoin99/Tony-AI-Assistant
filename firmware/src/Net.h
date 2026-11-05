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

struct WifiNet {
  const char* ssid;
  const char* password;
};

struct Server {
  const char* host;
  uint16_t port;
};

// Logs why WiFi drops (wrong password, network not found...). Call before beginWifi().
void watchWifi();

// Joins nets[0]. If WiFi stays down for 10 s, moves on to the next network (wrapping around), so a backup
// network takes over when the main one is missing. Entries with an empty ssid are skipped. Keep `nets` alive.
void beginWifi(const WifiNet* nets, int count);
// Runs the WiFi failover. loop() calls it; setup() can call it while waiting for the first connection.
void loopWifi();
// The network currently being joined (or joined).
const char* wifiName();

// Connects to servers[0]. If it can't be reached for 12 s while WiFi is up, tries the next server (wrapping
// around). Entries with an empty host are skipped. Keep `servers` alive.
// While the server is unreachable, loop() prints the likely reason to the serial monitor every 10 s.
void begin(const Server* servers, int count, const char* token, const Handlers& handlers);
void loop();
bool connected();
bool sendJson(const JsonDocument& doc);
bool sendBinary(uint8_t kind, const uint8_t* data, size_t len);

}  // namespace net
