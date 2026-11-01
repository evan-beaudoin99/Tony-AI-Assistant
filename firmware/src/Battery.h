#pragma once

#include <Arduino.h>

#include "pins.h"

namespace battery {

struct Reading {
  bool present = false;
  float volts = 0;
  int percent = -1;
};

// Resting Li-ion voltage -> charge, roughly. Good enough for a battery icon.
inline int percentFor(float v) {
  static const float kVolts[] = {3.30f, 3.40f, 3.50f, 3.60f, 3.70f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
  static const int kPercent[] = {0, 5, 10, 20, 35, 50, 65, 80, 90, 100};
  if (v <= kVolts[0]) return 0;
  for (int i = 1; i < 10; ++i) {
    if (v <= kVolts[i]) {
      float f = (v - kVolts[i - 1]) / (kVolts[i] - kVolts[i - 1]);
      return kPercent[i - 1] + static_cast<int>(f * (kPercent[i] - kPercent[i - 1]));
    }
  }
  return 100;
}

inline Reading read() {
  Reading r;
#if BATTERY_SENSE
  uint32_t mv = 0;
  for (int i = 0; i < 16; ++i) mv += analogReadMilliVolts(PIN_BATTERY);
  r.volts = mv / 16.0f * BATTERY_DIVIDER / 1000.0f;
  r.present = r.volts > 2.8f;  // below that there's no cell on the divider (running from USB)
  r.percent = r.present ? percentFor(r.volts) : -1;
#endif
  return r;
}

}  // namespace battery
