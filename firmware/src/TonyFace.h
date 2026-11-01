#pragma once

// Tony's face. The eye engine is Rocky's (github.com/cgro00/desk-robot, MIT, (c) 2026 Casey) by way of the
// Rocky_Face TFT port: every shape is worked out in Rocky's 128x64 "design" pixels and scaled to the screen.
// Tony adds eyebrows (a New Yorker talks with his eyebrows), a resting smirk, and taxi-yellow eyes.

#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

enum class Emotion : uint8_t {
  Neutral,
  Happy,
  Sad,
  Angry,
  Surprised,
  Sleepy,
  Thinking,
  Curious,
  Skeptical,
  Excited,
  Love,
  Confused,
  COUNT,
};

const char* emotionName(Emotion e);
bool emotionFromName(const char* name, Emotion& out);

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// One eye and its brow, in design pixels. "Inner" is the side toward the other eye.
struct EyeShape {
  float w = 34, h = 34;    // size
  float rt = 12, rb = 12;  // corner rounding, top / bottom
  float st = 0, sb = 0;    // edge slant, top / bottom: > 0 inner corner drops (angry), < 0 outer corner does (sad)
  float lid = 0;           // upper lid: a flat droop from the top (sleepy)
  float cheek = 0;         // lower lid: a round cheek pushing up from below (happy crescent)
  float dx = 0, dy = 0;    // offset: + = outward / down
  float bh = 0;            // brow height: + = raised
  float bt = 0;            // brow tilt: + = inner end down (angry), - = inner end up (worried)
};

enum : uint16_t {
  FX_BLUSH = 1 << 0,     // slashes under the outer corners
  FX_TEAR = 1 << 1,      // a tear falls from the right eye
  FX_DOTS = 1 << 2,      // thinking dots, top right
  FX_POP = 1 << 3,       // "!" for a moment after the change
  FX_STEAM = 1 << 4,     // flickering strokes above the outer corners
  FX_SHAKE = 1 << 5,     // jitters side to side
  FX_HEARTS = 1 << 6,    // hearts float up
  FX_QUESTION = 1 << 7,  // a bobbing "?"
  FX_SPARKLE = 1 << 8,   // twinkles beside the eyes
  FX_BOUNCE = 1 << 9,    // eyes bounce
  FX_WOBBLE = 1 << 10,   // eyes sway
};

struct EmotionLook {
  EyeShape l, r;               // left / right eye as you look at the screen
  float gazeX = 0, gazeY = 0;  // where the eyes rest (thinking looks up and to the side)
  float pop = 0.12f;           // how much the eyes overshoot when this emotion arrives
  float smile = 0;             // the resting mouth: + smile, - frown
  uint16_t fx = 0;
};

struct FacePack {
  float gap = 16;      // between the eyes
  float y = 0;         // eye height: + = lower
  bool glint = true;   // a dark pupil with a highlight that looks around on its own
  bool brows = true;
  uint8_t mouth = 1;   // 0 = only while talking, 1 = always (a resting smirk), 2 = never
  float smirk = 1.5f;  // how far the right corner of the resting mouth rides up
  EmotionLook looks[static_cast<int>(Emotion::COUNT)];
  EyeShape asleep;     // the shut eyes while sleeping
};

struct FaceColors {
  uint16_t bg = rgb565(0, 0, 0);
  uint16_t eye = rgb565(255, 204, 0);  // taxi yellow
  uint16_t pupil = rgb565(0, 0, 0);
  uint16_t glint = rgb565(255, 255, 255);
  uint16_t mouth = rgb565(255, 204, 0);
  uint16_t brow = rgb565(235, 235, 235);
  uint16_t blush = rgb565(255, 100, 100);
  uint16_t tear = rgb565(90, 185, 255);
  uint16_t steam = rgb565(255, 136, 0);
  uint16_t heart = rgb565(255, 20, 80);
  uint16_t pop = rgb565(255, 255, 255);
  uint16_t question = rgb565(255, 255, 255);
  uint16_t sparkle = rgb565(255, 255, 255);
  uint16_t dots = rgb565(255, 255, 255);
  uint16_t zeds = rgb565(150, 205, 255);
};

class TonyFace {
 public:
  // The face draws into the (x, y, w, h) part of the screen and never outside it.
  TonyFace(lgfx::LGFX_Device& lcd, int x, int y, int w, int h)
      : lcd_(lcd), canvas_(&lcd), x_(x), y_(y), screenW_(w), screenH_(h) {}

  // Call after lcd.init() / setRotation(). False if the frame buffer couldn't be allocated.
  bool begin();
  void setColors(const FaceColors& c) { colors_ = c; }

  // Repaint the whole face region next frame (after something else drew over it).
  void forceRedraw();

  void setEmotion(Emotion e);
  Emotion emotion() const { return emotion_; }
  void blink();

  // Asleep: eyes shut, breathing, Z's. Overrides the emotion while on.
  void setAsleep(bool on);
  bool asleep() const { return asleep_; }

  // Pupils (and the eyes a little) toward x, y (-1..1, + = screen right / down), held for holdMs.
  void lookToward(float x, float y, uint32_t holdMs);

  // Talking: a mouth that opens with `level` (0..1, current loudness). Call every frame while talking.
  void setTalking(bool on, float level = 0.0f);

  // When idle behavior is on, the face blinks and glances around on its own.
  void setIdle(bool on) { idle_ = on; }

  // Call every loop: advances animation by real elapsed time, then renders.
  void update(uint32_t nowMs);

 private:
  struct Zed {
    bool alive = false;
    float x = 0, y = 0, age = 0;
  };
  struct Heart {
    bool alive = false;
    float x = 0, y = 0, age = 0;
  };
  struct Box {
    int x0, y0, x1, y1;  // x1 / y1 exclusive
    bool empty() const { return x1 <= x0 || y1 <= y0; }
  };

  bool sleepLook() const { return asleep_; }
  void stepAnimation(uint32_t nowMs, float t);
  void render();
  void drawEye(const EyeShape& e, float cx, float cy, int inner, float hScale);
  void drawBrow(const EyeShape& e, float cx, float cy, int inner, float hScale);
  void drawMouth(int cx, int cy);
  void drawFlourishes(float leftOuter, float rightOuter, int eyeTop, int eyeBottom);
  void drawZeds();

  // Drawing in design pixels, scaled to the screen. Everything goes through these so the dirty box stays right.
  int sx(float x) const { return static_cast<int>(floorf(ox_ + x * s_)); }
  int sy(float y) const { return static_cast<int>(floorf(oy_ + y * s_)); }
  void mark(int x0, int y0, int x1, int y1);
  void span(int x, int y, int w, uint16_t c);
  void rect(float x, float y, float w, float h, uint16_t c);
  void roundBox(float x, float y, float w, float h, float r, uint16_t c);
  void line(int x0, int y0, int x1, int y1, uint16_t c, int bold = 0);
  void disc(int cx, int cy, int r, uint16_t c);
  void glyph(const char* const* rows, int n, int x, int y, uint16_t c);

  lgfx::LGFX_Device& lcd_;
  lgfx::LGFX_Sprite canvas_;
  int x_, y_;
  FaceColors colors_;
  float s_ = 1, ox_ = 0, oy_ = 0;  // design pixels -> canvas
  int screenW_ = 0, screenH_ = 0;
  Box drawn_ = {0, 0, 0, 0}, prevDrawn_ = {0, 0, 0, 0};

  FacePack pack_;
  Emotion emotion_ = Emotion::Neutral;
  EyeShape curL_, curR_;
  uint32_t emotionSinceMs_ = 0;

  // Blink state: 0 = open, 1 = fully closed.
  float blinkAmount_ = 0.0f;
  bool blinkClosing_ = false;
  uint32_t nextBlinkMs_ = 0;

  // Gaze offset from center, eased toward gazeTarget.
  float gazeX_ = 0, gazeY_ = 0;
  float gazeTargetX_ = 0, gazeTargetY_ = 0;
  uint32_t nextSaccadeMs_ = 0;

  // Pupils: where they look inside the eye, -1..1 of the room they have.
  float pupilX_ = 0, pupilY_ = 0;
  float pupilTargetX_ = 0, pupilTargetY_ = 0;
  uint32_t nextPupilMs_ = 0;

  // Flourish state.
  float pop_ = 0.0f;
  float squint_ = 0.0f;
  uint32_t nextSquintMs_ = 0;
  float tearY_ = -1.0f;
  uint32_t nextTearMs_ = 0;
  uint8_t thinkDots_ = 0;
  uint32_t nextDotMs_ = 0;
  int jitterX_ = 0;
  int lastJitterFrame_ = -1;
  Heart hearts_[3];
  uint32_t nextHeartMs_ = 0;
  bool heartSide_ = false;

  // Sleep.
  bool asleep_ = false;
  float breath_ = 0.0f;
  Zed zeds_[3];
  uint32_t nextZedMs_ = 0;
  uint32_t nextTwitchMs_ = 0;
  float twitch_ = 0.0f;

  // Mouth.
  bool talking_ = false;
  float mouthLevel_ = 0.0f;
  float mouth_ = 0.0f;
  float smile_ = 0.0f;

  bool idle_ = true;
  uint32_t lastFrameMs_ = 0;
  float frames_ = 0;  // animation time in Rocky's 30 FPS frames
};
