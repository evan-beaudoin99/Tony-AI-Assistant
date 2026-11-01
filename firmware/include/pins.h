#pragma once

// Every wire on Tony, in one place. Matches schematic/WIRING.md.
//
// Board: ESP32-S3 N16R8 CAM (the Freenove ESP32-S3-WROOM CAM layout). Already spoken for on this board:
//   4-13, 15-18  camera          35-37  octal PSRAM       19, 20  USB        43, 44  USB-serial chip
//   45, 46       boot straps     38-40  microSD slot (Tony borrows these for the amplifier: leave the slot empty)

// ST7789 2" TFT, 240x320, SPI
#define PIN_TFT_SCLK 21
#define PIN_TFT_MOSI 47
#define PIN_TFT_CS 14
#define PIN_TFT_DC 2
#define PIN_TFT_RST -1  // RES is wired to 3V3: the panel resets itself at power-up. BLK is wired to 3V3 too.

// INMP441 microphone, I2S in. L/R is wired to GND, so it talks on the left slot.
#define PIN_MIC_SCK 41
#define PIN_MIC_WS 42
#define PIN_MIC_SD 3

// MAX98357A amplifier -> 8 ohm speaker, I2S out
#define PIN_AMP_BCLK 38
#define PIN_AMP_LRC 39
#define PIN_AMP_DIN 40

// The board's BOOT button. An external button for the stand can go from GPIO0 to GND, in parallel.
#define PIN_BUTTON 0

// Onboard WS2812 RGB LED: glows red whenever the camera is on.
#define PIN_RGB_LED 48

// Battery + through a 100k / 100k divider. Set BATTERY_SENSE to 0 if you skip the divider.
#define PIN_BATTERY 1
#define BATTERY_SENSE 1
#define BATTERY_DIVIDER 2.0f

// OV3660 camera on the board's ribbon connector (fixed by the board)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_Y9 16
#define CAM_PIN_Y8 17
#define CAM_PIN_Y7 18
#define CAM_PIN_Y6 12
#define CAM_PIN_Y5 10
#define CAM_PIN_Y4 8
#define CAM_PIN_Y3 9
#define CAM_PIN_Y2 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13
