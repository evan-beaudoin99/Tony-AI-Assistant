#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "pins.h"

// The 2" ST7789 panel on its own SPI bus. If colors come out inverted (black background shows white), flip
// `invert`; if red and blue are swapped, flip `rgb_order`.
class TonyDisplay : public lgfx::LGFX_Device {
 public:
  TonyDisplay() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_TFT_SCLK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_TFT_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }

 private:
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
};

// Screen layout, landscape 320x240:
//   0..39    status bar (connection, mic, camera, battery)
//   40..199  Tony's face (128x64 design pixels at 2.5x)
//   200..239 caption ("I'm listenin'...")
// Cards (weather etc.) take the whole screen while they're up.
namespace ui {
constexpr int W = 320;
constexpr int H = 240;
constexpr int BAR_H = 40;
constexpr int FACE_Y = 40;
constexpr int FACE_H = 160;
constexpr int CAPTION_Y = 200;
}  // namespace ui

struct StatusInfo {
  bool wifi = false;
  bool server = false;
  bool listening = false;
  bool camera = false;
  bool asleep = false;
  int8_t battery = -1;  // percent, or -1 when there's no battery reading (USB power)

  bool operator==(const StatusInfo& o) const {
    return wifi == o.wifi && server == o.server && listening == o.listening && camera == o.camera &&
           asleep == o.asleep && battery == o.battery;
  }
  bool operator!=(const StatusInfo& o) const { return !(*this == o); }
};

class Screen {
 public:
  explicit Screen(lgfx::LGFX_Device& lcd) : lcd_(lcd) {}

  void begin();
  void splash(const char* line);

  // Status bar and caption only redraw when something changed (or `force`).
  void status(const StatusInfo& s, bool force = false);
  void caption(const char* text, bool force = false);

  // A full-screen JPEG card. Baseline JPEGs only (the server sends 320x240 baseline).
  bool showJpeg(const uint8_t* data, size_t len);

  // Wipe everything; the face, bar and caption repaint on their next update.
  void clear();

 private:
  void drawCamera(int x, int y, bool on);
  void drawMic(int x, int y);
  void drawBattery(int x, int y, int percent);

  lgfx::LGFX_Device& lcd_;
  StatusInfo shown_;
  bool haveShown_ = false;
  String caption_;
  bool haveCaption_ = false;
};
