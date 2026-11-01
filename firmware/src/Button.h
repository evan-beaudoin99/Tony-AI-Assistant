#pragma once

#include <Arduino.h>

// One button, three gestures: tap, double tap, and hold.
class Button {
 public:
  enum Event { None, Tap, DoubleTap, Hold };

  void begin(int pin) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
  }

  Event poll(uint32_t now) {
    bool down = digitalRead(pin_) == LOW;
    if (down != raw_) {
      raw_ = down;
      changedMs_ = now;
    }
    if (now - changedMs_ >= DEBOUNCE_MS && down != pressed_) {
      pressed_ = down;
      if (pressed_) {
        pressMs_ = now;
        held_ = false;
      } else if (!held_) {
        if (++taps_ == 2) {
          taps_ = 0;
          return DoubleTap;
        }
        releaseMs_ = now;
      }
    }
    if (pressed_ && !held_ && now - pressMs_ >= HOLD_MS) {
      held_ = true;
      taps_ = 0;
      return Hold;
    }
    if (!pressed_ && taps_ == 1 && now - releaseMs_ >= DOUBLE_GAP_MS) {
      taps_ = 0;
      return Tap;
    }
    return None;
  }

 private:
  static constexpr uint32_t DEBOUNCE_MS = 25;
  static constexpr uint32_t DOUBLE_GAP_MS = 300;
  static constexpr uint32_t HOLD_MS = 1200;

  int pin_ = -1;
  bool raw_ = false, pressed_ = false, held_ = false;
  uint8_t taps_ = 0;
  uint32_t changedMs_ = 0, pressMs_ = 0, releaseMs_ = 0;
};
