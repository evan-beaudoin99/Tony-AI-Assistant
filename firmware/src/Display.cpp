#include "Display.h"

#include "TonyFace.h"

namespace {
constexpr uint16_t BLACK = rgb565(0, 0, 0);
constexpr uint16_t TAXI = rgb565(255, 204, 0);
constexpr uint16_t WHITE = rgb565(255, 255, 255);
constexpr uint16_t GRAY = rgb565(110, 110, 110);
constexpr uint16_t SOFT = rgb565(170, 170, 170);
constexpr uint16_t GREEN = rgb565(60, 220, 90);
constexpr uint16_t ORANGE = rgb565(255, 150, 0);
constexpr uint16_t RED = rgb565(255, 40, 40);
}  // namespace

void Screen::begin() {
  lcd_.init();
  lcd_.setRotation(1);
  lcd_.fillScreen(BLACK);
}

void Screen::splash(const char* line) {
  lcd_.fillScreen(BLACK);
  lcd_.setTextDatum(textdatum_t::middle_center);
  lcd_.setFont(&fonts::FreeSansBold24pt7b);
  lcd_.setTextColor(TAXI, BLACK);
  lcd_.drawString("TONY", ui::W / 2, 95);
  // A taxi checker stripe under the name.
  for (int i = 0; i < 12; ++i) {
    lcd_.fillRect(64 + i * 16, 128, 16, 8, (i % 2) ? BLACK : TAXI);
    lcd_.fillRect(64 + i * 16, 136, 16, 8, (i % 2) ? TAXI : BLACK);
  }
  lcd_.setFont(&fonts::FreeSans9pt7b);
  lcd_.setTextColor(SOFT, BLACK);
  lcd_.drawString(line, ui::W / 2, 175);
  haveShown_ = false;
  haveCaption_ = false;
}

void Screen::status(const StatusInfo& s, bool force) {
  if (!force && haveShown_ && s == shown_) return;
  shown_ = s;
  haveShown_ = true;

  lcd_.fillRect(0, 0, ui::W, ui::BAR_H, BLACK);
  lcd_.setTextDatum(textdatum_t::middle_left);
  lcd_.setFont(&fonts::FreeSansBold9pt7b);
  lcd_.setTextColor(TAXI, BLACK);
  lcd_.drawString("TONY", 10, 20);

  // Green: talking to the server. Orange: WiFi but no server. Red: no WiFi.
  uint16_t link = s.server ? GREEN : s.wifi ? ORANGE : RED;
  lcd_.fillCircle(72, 20, 5, link);

  // Right to left: WiFi in the corner, then battery, camera, mic.
  int x = ui::W - 10 - 22;
  drawWifi(x, 10, s.wifi);
  x -= 6 + 29;
  if (s.battery >= 0) {
    drawBattery(x, 13, s.battery);
  } else {
    lcd_.setFont(&fonts::Font2);
    lcd_.setTextColor(SOFT, BLACK);
    lcd_.setTextDatum(textdatum_t::middle_right);
    lcd_.drawString("USB", x + 29, 20);
  }
  x -= 42;
  drawCamera(x, 12, s.camera);
  if (s.listening && !s.asleep) {
    x -= 26;
    drawMic(x, 10);
  }
}

// A WiFi fan in a 22x18 box: green when joined, gray with a red slash when not.
void Screen::drawWifi(int x, int y, bool on) {
  uint16_t c = on ? GREEN : GRAY;
  int cx = x + 11, cy = y + 17;
  lcd_.fillArc(cx, cy, 15, 13, 225, 315, c);
  lcd_.fillArc(cx, cy, 10, 8, 225, 315, c);
  lcd_.fillCircle(cx, cy - 1, 2, c);
  if (!on) {
    lcd_.drawLine(x + 1, y + 18, x + 21, y + 1, RED);
    lcd_.drawLine(x + 2, y + 18, x + 22, y + 1, RED);
  }
}

void Screen::drawBattery(int x, int y, int percent) {
  uint16_t c = percent > 40 ? GREEN : percent > 15 ? ORANGE : RED;
  lcd_.drawRoundRect(x, y, 26, 14, 3, WHITE);
  lcd_.fillRect(x + 26, y + 4, 3, 6, WHITE);
  int w = constrain(percent, 0, 100) * 22 / 100;
  if (w > 0) lcd_.fillRect(x + 2, y + 2, w, 10, c);
}

// A little camera: red with "REC" dot when on, gray and slashed when off.
void Screen::drawCamera(int x, int y, bool on) {
  uint16_t c = on ? RED : GRAY;
  lcd_.fillRoundRect(x, y + 3, 26, 14, 3, c);
  lcd_.fillRect(x + 8, y, 10, 4, c);
  lcd_.fillCircle(x + 13, y + 10, 5, BLACK);
  lcd_.fillCircle(x + 13, y + 10, 3, c);
  if (on) {
    lcd_.fillCircle(x + 34, y + 10, 4, RED);
  } else {
    lcd_.drawLine(x - 2, y + 19, x + 28, y - 1, BLACK);
    lcd_.drawLine(x - 1, y + 19, x + 29, y - 1, GRAY);
    lcd_.drawLine(x, y + 19, x + 30, y - 1, BLACK);
  }
}

void Screen::drawMic(int x, int y) {
  lcd_.fillRoundRect(x + 5, y, 10, 14, 5, GREEN);
  lcd_.drawArc(x + 10, y + 9, 9, 8, 0, 180, GREEN);
  lcd_.fillRect(x + 9, y + 18, 2, 3, GREEN);
}

void Screen::caption(const char* text, bool force) {
  if (!force && haveCaption_ && caption_ == text) return;
  caption_ = text;
  haveCaption_ = true;
  lcd_.fillRect(0, ui::CAPTION_Y, ui::W, ui::H - ui::CAPTION_Y, BLACK);
  if (!*text) return;
  lcd_.setTextDatum(textdatum_t::middle_center);
  lcd_.setFont(&fonts::FreeSans9pt7b);
  lcd_.setTextColor(SOFT, BLACK);
  String line = text;
  const int maxWidth = ui::W - 16;
  if (lcd_.textWidth(line) > maxWidth) {
    while (line.length() > 1 && lcd_.textWidth(line + "...") > maxWidth) line.remove(line.length() - 1);
    line += "...";
  }
  lcd_.drawString(line, ui::W / 2, ui::CAPTION_Y + 20);
}

bool Screen::showJpeg(const uint8_t* data, size_t len) {
  haveShown_ = false;
  haveCaption_ = false;
  return lcd_.drawJpg(data, len, 0, 0);
}

void Screen::clear() {
  lcd_.fillScreen(BLACK);
  haveShown_ = false;
  haveCaption_ = false;
}
