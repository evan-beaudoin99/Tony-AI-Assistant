#pragma once

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"  // WiFi name and password, from firmware/include/secrets.h

// The bits every bring-up test shares: PASS / FAIL lines on the serial monitor, and joining WiFi.
namespace check {

inline int passed = 0, failed = 0, warned = 0;

inline void title(const char* name) {
  Serial.printf("\n\n========== Tony bring-up test: %s ==========\n", name);
}

// Prints PASS or FAIL, plus a hint about what to look at when it fails.
inline bool expect(bool ok, const char* what, const char* hint = nullptr) {
  Serial.printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok && hint) Serial.printf("         -> %s\n", hint);
  ok ? ++passed : ++failed;
  return ok;
}

// Something odd but not broken.
inline void warn(const char* what, const char* hint = nullptr) {
  Serial.printf("  [WARN] %s\n", what);
  if (hint) Serial.printf("         -> %s\n", hint);
  ++warned;
}

inline void info(const char* what) { Serial.printf("  [info] %s\n", what); }

inline bool summary() {
  Serial.printf("\n  %d passed, %d failed, %d warnings: %s\n\n", passed, failed, warned,
                failed == 0 ? "ALL GOOD" : "SEE THE FAILS ABOVE");
  return failed == 0;
}

// A speaker peak that sags the 5 V rail shows up as a brownout reset. Worth knowing before blaming the code.
inline void lastReset() {
  esp_reset_reason_t r = esp_reset_reason();
  if (r == ESP_RST_BROWNOUT) {
    warn("The last reset was a BROWNOUT (the supply voltage dipped).",
         "Usually the amplifier. Put a 470 uF capacitor across the amp's VIN and GND, right at the amp.");
  } else if (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT) {
    warn("The last reset was a crash or watchdog. Scroll up in the monitor for the backtrace.");
  }
}

}  // namespace check

namespace testwifi {

// Joins the WiFi from secrets.h and answers to http://<host>.local as well as the IP address.
inline bool connect(const char* host, uint32_t timeoutMs = 20000) {
  Serial.printf("  wifi: joining \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save adds ~100 ms of lag to every packet: bad for video
  WiFi.setHostname(host);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (!check::expect(WiFi.status() == WL_CONNECTED, "WiFi connected",
                     "Check WIFI_SSID / WIFI_PASSWORD in firmware/include/secrets.h. The ESP32 only sees 2.4 GHz.")) {
    return false;
  }
  if (WiFi.RSSI() < -75) check::warn("Weak WiFi signal. Expect a choppy stream; move closer to the router.");
  MDNS.begin(host);
  MDNS.addService("http", "tcp", 80);
  return true;
}

inline void printAddress(const char* host) {
  Serial.println("  ------------------------------------------------------------");
  Serial.printf("  Open this in your browser:  http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.printf("                         or:  http://%s.local/\n", host);
  Serial.println("  (same WiFi network as the ESP32)");
  Serial.println("  ------------------------------------------------------------");
}

}  // namespace testwifi
