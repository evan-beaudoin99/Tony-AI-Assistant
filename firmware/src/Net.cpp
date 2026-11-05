#include "config.h"

#if !TONY_USE_USB  // the USB transport is in UsbLink.cpp

#include "Net.h"

#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>

namespace {

WebSocketsClient ws;
net::Handlers handlers;
bool up = false;

uint8_t* scratch = nullptr;  // kind byte + payload, reused between sends
size_t scratchCap = 0;

// While the server is unreachable, explain why on the serial monitor this often.
constexpr uint32_t DIAGNOSE_EVERY_MS = 10000;
constexpr uint32_t PROBE_TIMEOUT_MS = 2000;
String serverHost;
uint16_t serverPort = 0;
uint32_t downSinceMs = 0;
uint32_t lastDiagnoseMs = 0;

// Failover between the main and backup WiFi networks and servers.
constexpr int MAX_CHOICES = 4;
constexpr uint32_t WIFI_SWITCH_MS = 10000;
constexpr uint32_t SERVER_SWITCH_MS = 12000;  // a few reconnect attempts (each can take ~5 s to time out)
net::WifiNet wifiNets[MAX_CHOICES];
int wifiCount = 0;
int wifiIdx = 0;
uint32_t wifiOkMs = 0;  // last time WiFi was up, or we started joining
net::Server servers[MAX_CHOICES];
int serverCount = 0;
int serverIdx = 0;
uint32_t serverTryMs = 0;  // start of the current server's turn (only counts while WiFi is up)
String wsPath;

void joinWifi(int i) {
  wifiIdx = i;
  wifiOkMs = millis();
  Serial.printf("wifi: joining \"%s\"%s\n", wifiNets[i].ssid, i ? " (backup)" : "");
  WiFi.begin(wifiNets[i].ssid, wifiNets[i].password);
}

void useServer(int i) {
  serverIdx = i;
  serverTryMs = millis();
  serverHost = servers[i].host;
  serverPort = servers[i].port;
  Serial.printf("net: trying to connect to ws://%s:%u/ws/device%s\n", serverHost.c_str(), serverPort,
                i ? " (backup)" : "");
  ws.disconnect();
  ws.begin(serverHost.c_str(), serverPort, wsPath.c_str());
}

void onEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      up = true;
      Serial.printf("net: connected to server (after %lu s)\n", (unsigned long)((millis() - downSinceMs) / 1000));
      if (handlers.connected) handlers.connected();
      break;
    case WStype_DISCONNECTED:
      // A reason means TCP worked but the WebSocket itself failed or closed (bad HTTP reply, timeout...).
      if (length) Serial.printf("net: WebSocket closed: %.*s\n", static_cast<int>(length), payload);
      if (up) {
        up = false;
        downSinceMs = millis();
        lastDiagnoseMs = millis();  // give it one quiet retry cycle before explaining
        Serial.println("net: lost the server");
        if (handlers.disconnected) handlers.disconnected();
      }
      break;
    case WStype_ERROR:
      Serial.printf("net: WebSocket error: %.*s\n", static_cast<int>(length), payload);
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

const char* wifiTrouble(wl_status_t s) {
  switch (s) {
    case WL_NO_SSID_AVAIL: return "can't find that network. Check WIFI_SSID, and that it's a 2.4 GHz network";
    case WL_CONNECT_FAILED: return "the network turned us away. Check WIFI_PASSWORD";
    case WL_CONNECTION_LOST: return "lost the network. Weak signal?";
    case WL_DISCONNECTED: return "not connected (still trying)";
    default: return "not connected";
  }
}

bool sameSubnet(IPAddress a, IPAddress b, IPAddress mask) {
  for (int i = 0; i < 4; ++i) {
    if ((a[i] & mask[i]) != (b[i] & mask[i])) return false;
  }
  return true;
}

// A bare TCP connect to the server's port. Tells "nobody at that address" (timeout) apart from
// "the computer's there but nothing is listening" (refused). Returns 0 if the port is open, else an errno.
int probeTcp(IPAddress ip, uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return errno;
  fcntl(fd, F_SETFL, O_NONBLOCK);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = static_cast<uint32_t>(ip);
  int err = 0;
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    if (errno != EINPROGRESS) {
      err = errno;
    } else {
      fd_set w;
      FD_ZERO(&w);
      FD_SET(fd, &w);
      timeval tv = {PROBE_TIMEOUT_MS / 1000, (PROBE_TIMEOUT_MS % 1000) * 1000};
      int r = select(fd + 1, nullptr, &w, nullptr, &tv);
      if (r == 0) {
        err = ETIMEDOUT;
      } else if (r < 0) {
        err = errno;
      } else {
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
      }
    }
  }
  close(fd);
  return err;
}

// Prints the likely reason Tony can't reach his server. Blocks for up to PROBE_TIMEOUT_MS, only while offline.
void diagnose() {
  Serial.printf("net: can't reach the server at ws://%s:%u (offline %lu s). Checking why...\n", serverHost.c_str(),
                serverPort, (unsigned long)((millis() - downSinceMs) / 1000));

  if (!WiFi.isConnected()) {
    Serial.printf("net:   WiFi: %s (\"%s\")\n", wifiTrouble(WiFi.status()), WiFi.SSID().c_str());
    return;
  }
  IPAddress me = WiFi.localIP(), mask = WiFi.subnetMask();
  Serial.printf("net:   WiFi OK: \"%s\", signal %d dBm. This board is %s, router %s\n", WiFi.SSID().c_str(),
                WiFi.RSSI(), me.toString().c_str(), WiFi.gatewayIP().toString().c_str());

  if (serverHost == "192.168.1.50") {
    Serial.println("net:   PROBLEM: TONY_SERVER_HOST is still the example value from secrets.example.h.");
  }
  IPAddress server;
  if (!WiFi.hostByName(serverHost.c_str(), server)) {
    Serial.printf("net:   PROBLEM: \"%s\" isn't an address this board can look up. Use the IP that "
                  "`python -m tony` prints.\n", serverHost.c_str());
    return;
  }
  if (!sameSubnet(me, server, mask)) {
    Serial.printf("net:   PROBLEM: %s isn't on this network (the board is %s). Set TONY_SERVER_HOST in secrets.h to "
                  "the address `python -m tony` prints, then re-upload.\n", server.toString().c_str(),
                  me.toString().c_str());
  }

  int err = probeTcp(server, serverPort);
  switch (err) {
    case 0:
      Serial.printf("net:   Port %u on %s is open, but the WebSocket didn't come up. Is it really Tony's server on "
                    "that port? Look for a \"WebSocket closed\" reason above.\n", serverPort, server.toString().c_str());
      break;
    case ECONNREFUSED:
      Serial.printf("net:   %s answered, but nothing is listening on port %u. Is `python -m tony` running? Does "
                    "server.port in config.yaml match TONY_SERVER_PORT?\n", server.toString().c_str(), serverPort);
      break;
    case ETIMEDOUT:
      Serial.printf("net:   No answer from %s:%u after %lu s. Wrong address, the computer is asleep, or Windows "
                    "Firewall is blocking it (allow Python on Private networks, or open TCP port %u).\n",
                    server.toString().c_str(), serverPort, (unsigned long)(PROBE_TIMEOUT_MS / 1000), serverPort);
      break;
    case EHOSTUNREACH:
    case ENETUNREACH:
      Serial.printf("net:   Can't route to %s. It has to be on the same network as the board.\n",
                    server.toString().c_str());
      break;
    default:
      Serial.printf("net:   TCP connect to %s:%u failed: %s (errno %d)\n", server.toString().c_str(), serverPort,
                    strerror(err), err);
      break;
  }
}

void onWifiDisconnected(arduino_event_id_t, arduino_event_info_t info) {
  auto reason = static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
  Serial.printf("wifi: disconnected, reason %d (%s)\n", reason, WiFi.disconnectReasonName(reason));
}

}  // namespace

namespace net {

void watchWifi() { WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED); }

void beginWifi(const WifiNet* nets, int count) {
  wifiCount = 0;
  for (int i = 0; i < count && wifiCount < MAX_CHOICES; ++i) {
    if (nets[i].ssid && *nets[i].ssid) wifiNets[wifiCount++] = nets[i];
  }
  if (wifiCount) joinWifi(0);
}

void loopWifi() {
  if (WiFi.isConnected() || wifiCount == 0) {
    wifiOkMs = millis();
    return;
  }
  if (wifiCount > 1 && millis() - wifiOkMs >= WIFI_SWITCH_MS) {
    Serial.printf("wifi: \"%s\" isn't working, switching networks\n", wifiNets[wifiIdx].ssid);
    joinWifi((wifiIdx + 1) % wifiCount);
  }
}

const char* wifiName() { return wifiCount ? wifiNets[wifiIdx].ssid : ""; }

void begin(const Server* list, int count, const char* token, const Handlers& h) {
  handlers = h;
  serverCount = 0;
  for (int i = 0; i < count && serverCount < MAX_CHOICES; ++i) {
    if (list[i].host && *list[i].host) servers[serverCount++] = list[i];
  }
  if (!serverCount) {
    Serial.println("net: no server address set. Fill in TONY_SERVER_HOST in secrets.h.");
    return;
  }
  downSinceMs = millis();
  lastDiagnoseMs = millis() - DIAGNOSE_EVERY_MS + 5000;  // first explanation ~5 s in, if still not connected
  wsPath = String("/ws/device?token=") + token;
  ws.onEvent(onEvent);
  ws.setReconnectInterval(2000);
  ws.enableHeartbeat(15000, 4000, 2);
  useServer(0);
}

void loop() {
  loopWifi();
  ws.loop();
  // Only blame the server while WiFi is up; otherwise the WiFi failover is the one to act.
  if (up || !WiFi.isConnected()) {
    serverTryMs = millis();
  } else if (serverCount > 1 && millis() - serverTryMs >= SERVER_SWITCH_MS) {
    Serial.printf("net: can't reach %s, switching servers\n", serverHost.c_str());
    useServer((serverIdx + 1) % serverCount);
  }
  if (!up && millis() - lastDiagnoseMs >= DIAGNOSE_EVERY_MS) {
    diagnose();
    lastDiagnoseMs = millis();
  }
}

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

#endif  // !TONY_USE_USB
