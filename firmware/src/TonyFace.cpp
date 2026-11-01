#include "TonyFace.h"

namespace {

constexpr int DESIGN_W = 128;
constexpr int DESIGN_H = 64;
constexpr float FRAME_MS = 1000.0f / 30.0f;  // Rocky's tuning is per frame at ~30 FPS

const char* kEmotionNames[] = {
    "neutral", "happy", "sad", "angry", "surprised", "sleepy", "thinking",
    "curious", "skeptical", "excited", "love", "confused",
};
static_assert(sizeof(kEmotionNames) / sizeof(kEmotionNames[0]) == static_cast<int>(Emotion::COUNT), "emotion names");

// Small pictures, a row per string ('#' = lit).
const char* const kBang[] = {"###", "###", "###", "###", "###", "###", "###", "...", "###", "###"};
const char* const kQuestion[] = {".#####.", "##...##", "##...##", ".....##", "....##.", "...##..",
                                 "..##...", "..##...", ".......", "..##...", "..##..."};
const char* const kHeart[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};

// Per-frame easing / decay factors, stretched to however many frames `t` actually passed.
float ease(float f, float t) { return 1.0f - powf(1.0f - f, t); }
float approach(float cur, float target, float k) { return cur + (target - cur) * k; }

void morph(EyeShape& c, const EyeShape& t, float k) {
  c.w = approach(c.w, t.w, k);
  c.h = approach(c.h, t.h, k);
  c.rt = approach(c.rt, t.rt, k);
  c.rb = approach(c.rb, t.rb, k);
  c.st = approach(c.st, t.st, k);
  c.sb = approach(c.sb, t.sb, k);
  c.lid = approach(c.lid, t.lid, k);
  c.cheek = approach(c.cheek, t.cheek, k);
  c.dx = approach(c.dx, t.dx, k);
  c.dy = approach(c.dy, t.dy, k);
  c.bh = approach(c.bh, t.bh, k);
  c.bt = approach(c.bt, t.bt, k);
}

EyeShape eye(float w, float h, float rt, float rb, float st = 0, float sb = 0, float lid = 0, float cheek = 0,
             float dx = 0, float dy = 0) {
  EyeShape e;
  e.w = w; e.h = h; e.rt = rt; e.rb = rb; e.st = st; e.sb = sb; e.lid = lid; e.cheek = cheek; e.dx = dx; e.dy = dy;
  return e;
}

EyeShape brow(EyeShape e, float bh, float bt) {
  e.bh = bh;
  e.bt = bt;
  return e;
}

EmotionLook both(const EyeShape& e, uint16_t fx = 0, float pop = 0.12f, float gx = 0, float gy = 0) {
  EmotionLook l;
  l.l = l.r = e;
  l.fx = fx;
  l.pop = pop;
  l.gazeX = gx;
  l.gazeY = gy;
  return l;
}

// Rocky's eyes (server/faces/rocky.json in desk-robot), plus Tony's brows and mouth.
void tonyPack(FacePack& p) {
  p = FacePack();
  using E = Emotion;
  auto& L = p.looks;
  L[(int)E::Neutral] = both(eye(34, 34, 12, 12));
  L[(int)E::Neutral].smile = 1.0f;

  L[(int)E::Happy] = both(brow(eye(36, 36, 12, 12, 0, 0, 0, 16), 2, -1), FX_BLUSH);
  L[(int)E::Happy].smile = 3.0f;

  L[(int)E::Sad] = both(brow(eye(32, 26, 10, 10, -9, 0, 4), 1, -6), FX_TEAR);
  L[(int)E::Sad].smile = -2.5f;

  L[(int)E::Angry] = both(brow(eye(34, 24, 8, 8, 10), -2, 8), FX_STEAM | FX_SHAKE);
  L[(int)E::Angry].smile = -2.0f;

  L[(int)E::Surprised] = both(brow(eye(38, 44, 19, 19), 6, 0), FX_POP, 0.3f);

  L[(int)E::Sleepy] = both(brow(eye(34, 26, 10, 10, 0, 0, 14), -2, 0));

  L[(int)E::Thinking] = both(brow(eye(30, 30, 11, 11, 0, 0, 5), 1, 1), FX_DOTS, 0.12f, 7, -5);
  L[(int)E::Thinking].r = brow(eye(30, 30, 11, 11, 0, 0, 5), 5, -2);
  L[(int)E::Thinking].smile = -0.5f;

  L[(int)E::Curious] = both(eye(30, 30, 11, 11), 0, 0.15f, -5, -3);
  L[(int)E::Curious].r = brow(eye(38, 40, 15, 15, 0, 0, 0, 0, 0, -2), 5, -1);
  L[(int)E::Curious].smile = 0.5f;

  // "You talkin' to me?": one eye wide under a raised brow, the other narrowed under a low one.
  L[(int)E::Skeptical] = both(brow(eye(34, 38, 13, 12, -3), 6, -2), 0, 0.12f, 4, 0);
  L[(int)E::Skeptical].r = brow(eye(34, 34, 12, 12, 0, 0, 17), -3, 4);
  L[(int)E::Skeptical].smile = -1.0f;

  L[(int)E::Excited] = both(brow(eye(38, 40, 16, 16, 0, 0, 0, 10), 5, -1), FX_SPARKLE | FX_BOUNCE, 0.25f);
  L[(int)E::Excited].smile = 3.5f;

  L[(int)E::Love] = both(brow(eye(36, 34, 14, 14, 0, 0, 0, 12), 3, -2), FX_HEARTS | FX_BLUSH, 0.2f);
  L[(int)E::Love].smile = 3.0f;

  L[(int)E::Confused] = both(brow(eye(32, 36, 13, 12, -5), 5, -3), FX_QUESTION | FX_WOBBLE);
  L[(int)E::Confused].r = brow(eye(28, 24, 10, 10, 4, 0, 0, 0, 0, 4), -1, 4);
  L[(int)E::Confused].smile = -1.0f;

  p.asleep = brow(eye(30, 3, 1, 1), -1, 0);
}

// Where one eye is, worked out once per frame (design pixels).
struct Geo {
  float cx, w, left, right, top, bot, rt, rb, st, sb, lid, cheek;
  int inner;  // +1: the inner edge is to the right (the left eye), -1: to the left
};

bool inEye(const Geo& g, float x, float y) {
  if (x < g.left || x >= g.right || y < g.top || y >= g.bot) return false;
  float u = (x - g.cx) / (g.w * 0.5f) * g.inner;  // -1 outer edge .. +1 inner edge
  float topCut = g.st >= 0 ? g.st * (u + 1) * 0.5f : -g.st * (1 - u) * 0.5f;
  float botCut = g.sb >= 0 ? g.sb * (u + 1) * 0.5f : -g.sb * (1 - u) * 0.5f;
  // Slants shear the whole rounded eye, so slanted corners stay round: look the point up in the unslanted eye.
  float hh = g.bot - g.top;
  float yy = y - topCut * (g.bot - y) / hh + botCut * (y - g.top) / hh;
  if (yy < g.top + g.lid || yy >= g.bot) return false;
  if (yy < g.top + g.rt) {
    float qx = constrain(x, g.left + g.rt, g.right - g.rt), dx = x - qx, dy = yy - (g.top + g.rt);
    if (dx * dx + dy * dy > g.rt * g.rt) return false;
  }
  if (yy > g.bot - g.rb) {
    float qx = constrain(x, g.left + g.rb, g.right - g.rb), dx = x - qx, dy = yy - (g.bot - g.rb);
    if (dx * dx + dy * dy > g.rb * g.rb) return false;
  }
  if (g.cheek > 0.5f) {  // a round cheek pushing up from below
    float rr = g.w, ccy = g.bot + rr - g.cheek, dx = x - g.cx, dy = y - ccy;
    if (dx * dx + dy * dy < rr * rr) return false;
  }
  return true;
}

}  // namespace

const char* emotionName(Emotion e) { return kEmotionNames[static_cast<uint8_t>(e)]; }

bool emotionFromName(const char* name, Emotion& out) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(Emotion::COUNT); i++) {
    if (strcasecmp(name, kEmotionNames[i]) == 0) {
      out = static_cast<Emotion>(i);
      return true;
    }
  }
  return false;
}

// ── state ───────────────────────────────────────────────────────────────────

bool TonyFace::begin() {
  s_ = fminf(static_cast<float>(screenW_) / DESIGN_W, static_cast<float>(screenH_) / DESIGN_H);
  ox_ = (screenW_ - DESIGN_W * s_) * 0.5f;
  oy_ = (screenH_ - DESIGN_H * s_) * 0.5f;

  // 320x160 at 16 bits is 100 KB: it lives in PSRAM so WiFi keeps the internal RAM.
  canvas_.setColorDepth(16);
  canvas_.setPsram(true);
  if (!canvas_.createSprite(screenW_, screenH_)) return false;
  canvas_.fillSprite(colors_.bg);
  lcd_.fillRect(x_, y_, screenW_, screenH_, colors_.bg);

  tonyPack(pack_);
  curL_ = pack_.looks[0].l;
  curR_ = pack_.looks[0].r;
  smile_ = pack_.looks[0].smile;
  // Wake-up: start with eyes shut, then open.
  uint32_t now = millis();
  blinkAmount_ = 1.0f;
  blinkClosing_ = false;
  nextBlinkMs_ = now + 3000;
  nextSaccadeMs_ = now + 1500;
  nextSquintMs_ = now + 9000;
  lastFrameMs_ = now;
  return true;
}

void TonyFace::forceRedraw() {
  canvas_.fillSprite(colors_.bg);
  prevDrawn_ = {0, 0, screenW_, screenH_};
}

void TonyFace::setEmotion(Emotion e) {
  if (e >= Emotion::COUNT) return;
  if (e != emotion_) {
    pop_ = 1.0f;  // a little overshoot sells the change
    emotionSinceMs_ = millis();
    tearY_ = -1.0f;
    nextTearMs_ = millis() + 1500;
    thinkDots_ = 0;
    for (Heart& h : hearts_) h.alive = false;
    nextHeartMs_ = millis() + 200;
  }
  emotion_ = e;
  const EmotionLook& look = pack_.looks[static_cast<int>(e)];
  gazeTargetX_ = look.gazeX;
  gazeTargetY_ = look.gazeY;
}

void TonyFace::blink() { blinkClosing_ = true; }

void TonyFace::setAsleep(bool on) {
  if (on == asleep_) return;
  asleep_ = on;
  if (on) {
    for (Zed& z : zeds_) z.alive = false;
    nextZedMs_ = millis() + 800;
    nextTwitchMs_ = millis() + random(5000, 9000);
    gazeTargetX_ = gazeTargetY_ = 0;
  } else {
    pop_ = 1.0f;  // eyes spring open
    blinkAmount_ = 0.0f;
    setEmotion(emotion_);
  }
}

void TonyFace::lookToward(float x, float y, uint32_t holdMs) {
  pupilTargetX_ = constrain(x, -1.0f, 1.0f);
  pupilTargetY_ = constrain(y, -1.0f, 1.0f);
  gazeTargetX_ = pupilTargetX_ * 6.0f;  // the whole eye leans that way a little too
  gazeTargetY_ = pupilTargetY_ * 3.0f;
  nextPupilMs_ = nextSaccadeMs_ = millis() + holdMs;
}

void TonyFace::setTalking(bool on, float level) {
  talking_ = on;
  mouthLevel_ = constrain(level, 0.0f, 1.0f);
}

void TonyFace::update(uint32_t nowMs) {
  float t = constrain((nowMs - lastFrameMs_) / FRAME_MS, 0.0f, 3.0f);
  lastFrameMs_ = nowMs;
  stepAnimation(nowMs, t);
  render();
}

void TonyFace::stepAnimation(uint32_t nowMs, float t) {
  frames_ += t;
  const EmotionLook& look = pack_.looks[static_cast<int>(emotion_)];

  // Morph the eye shapes toward the current emotion (or shut, if asleep). Eyes close slowly, change fast.
  float k = ease(sleepLook() ? 0.12f : 0.25f, t);
  morph(curL_, sleepLook() ? pack_.asleep : look.l, k);
  morph(curR_, sleepLook() ? pack_.asleep : look.r, k);
  smile_ = approach(smile_, sleepLook() ? 0 : look.smile, k);

  pop_ *= powf(0.82f, t);
  squint_ *= powf(0.85f, t);

  if (sleepLook()) {
    // Slow breathing; the odd dream twitch; Z's drifting up.
    breath_ += 0.035f * t;
    blinkAmount_ = 0.0f;
    blinkClosing_ = false;
    twitch_ *= powf(0.8f, t);
    if (nowMs >= nextTwitchMs_) {
      twitch_ = 1.0f;
      nextTwitchMs_ = nowMs + random(6000, 12000);
    }
    if (nowMs >= nextZedMs_) {
      for (Zed& z : zeds_) {
        if (!z.alive) {
          z.alive = true;
          z.age = 0;
          z.x = fminf(DESIGN_W / 2 + pack_.gap / 2 + curL_.w + 2, 108);
          z.y = DESIGN_H / 2 - 6;
          break;
        }
      }
      nextZedMs_ = nowMs + random(1100, 1700);
    }
    for (Zed& z : zeds_) {
      if (!z.alive) continue;
      z.age += 0.012f * t;
      z.y -= 0.32f * t;
      z.x += 0.22f * t;
      if (z.age >= 1.0f || z.y < -12) z.alive = false;
    }
    float g = ease(0.2f, t);
    gazeX_ = approach(gazeX_, 0, g);
    gazeY_ = approach(gazeY_, 0, g);
    mouth_ = approach(mouth_, 0, ease(0.3f, t));
    return;
  }

  // Blink: snap shut fast, reopen a little slower — reads as natural.
  if (blinkClosing_) {
    blinkAmount_ += 0.45f * t;
    if (blinkAmount_ >= 1.0f) {
      blinkAmount_ = 1.0f;
      blinkClosing_ = false;
    }
  } else if (blinkAmount_ > 0.0f) {
    blinkAmount_ = fmaxf(0.0f, blinkAmount_ - 0.28f * t);
  }

  if (idle_) {
    if (nowMs >= nextBlinkMs_) {
      blinkClosing_ = true;
      nextBlinkMs_ = nowMs + random(2200, 6000);
      if (random(100) < 20) nextBlinkMs_ = nowMs + 400;  // occasional double blink
    }
    bool looksSomewhere = look.gazeX != 0 || look.gazeY != 0;
    if (nowMs >= nextSaccadeMs_ && !looksSomewhere) {
      gazeTargetX_ = static_cast<float>(random(-8, 9));
      gazeTargetY_ = static_cast<float>(random(-4, 5));
      if (random(100) < 40) gazeTargetX_ = gazeTargetY_ = 0;  // mostly back to center so he doesn't look shifty
      nextSaccadeMs_ = nowMs + random(1200, 4000);
    }
    if (nowMs >= nextPupilMs_) {
      if (random(100) < 35) {
        pupilTargetX_ = random(-25, 26) / 100.0f;
        pupilTargetY_ = random(-20, 21) / 100.0f;
      } else {
        pupilTargetX_ = random(-90, 91) / 100.0f;
        pupilTargetY_ = random(-60, 61) / 100.0f;
      }
      nextPupilMs_ = nowMs + random(600, 2600);
    }
    if (nowMs >= nextSquintMs_ && emotion_ == Emotion::Neutral) {
      squint_ = 1.0f;
      nextSquintMs_ = nowMs + random(8000, 16000);
    }
  }

  // Flourishes.
  if (look.fx & FX_TEAR) {
    if (tearY_ < 0 && nowMs >= nextTearMs_) tearY_ = 0;
    if (tearY_ >= 0) {
      tearY_ += 0.7f * t;
      if (tearY_ > DESIGN_H) {
        tearY_ = -1;
        nextTearMs_ = nowMs + random(2500, 5000);
      }
    }
  }
  if ((look.fx & FX_DOTS) && nowMs >= nextDotMs_) {
    thinkDots_ = (thinkDots_ + 1) % 4;
    nextDotMs_ = nowMs + 420;
  }
  if ((look.fx & FX_HEARTS) && nowMs >= nextHeartMs_) {
    for (Heart& h : hearts_) {
      if (!h.alive) {
        h.alive = true;
        h.age = 0;
        heartSide_ = !heartSide_;
        h.x = heartSide_ ? DESIGN_W / 2 + pack_.gap / 2 + curR_.w + 2 : DESIGN_W / 2 - pack_.gap / 2 - curL_.w - 9;
        h.y = DESIGN_H / 2 + 6;
        break;
      }
    }
    nextHeartMs_ = nowMs + random(600, 1000);
  }
  for (Heart& h : hearts_) {
    if (!h.alive) continue;
    h.age += 0.015f * t;
    h.y -= 0.45f * t;
    if (h.age >= 1.0f || h.y < -6) h.alive = false;
  }
  int frame = static_cast<int>(frames_);
  if (frame != lastJitterFrame_) {
    lastJitterFrame_ = frame;
    jitterX_ = ((look.fx & FX_SHAKE) && (frame % 3 == 0)) ? random(-1, 2) : 0;
  }

  // Mouth follows the loudness while talking, closes otherwise.
  float mouthGoal = talking_ ? constrain(0.15f + mouthLevel_ * 4.0f, 0.15f, 1.0f) : 0.0f;
  mouth_ = approach(mouth_, mouthGoal, ease(talking_ ? 0.5f : 0.3f, t));

  float g = ease(0.35f, t);
  gazeX_ = approach(gazeX_, gazeTargetX_, g);
  gazeY_ = approach(gazeY_, gazeTargetY_, g);
  float p = ease(0.5f, t);  // pupils dart: there in a few frames
  pupilX_ = approach(pupilX_, pupilTargetX_, p);
  pupilY_ = approach(pupilY_, pupilTargetY_, p);
}

// ── drawing primitives (design pixels in, canvas pixels out) ────────────────

void TonyFace::mark(int x0, int y0, int x1, int y1) {
  if (x1 <= x0 || y1 <= y0) return;
  if (drawn_.empty()) {
    drawn_ = {x0, y0, x1, y1};
    return;
  }
  drawn_.x0 = min(drawn_.x0, x0);
  drawn_.y0 = min(drawn_.y0, y0);
  drawn_.x1 = max(drawn_.x1, x1);
  drawn_.y1 = max(drawn_.y1, y1);
}

void TonyFace::span(int x, int y, int w, uint16_t c) {
  canvas_.drawFastHLine(x, y, w, c);
  mark(x, y, x + w, y + 1);
}

void TonyFace::rect(float x, float y, float w, float h, uint16_t c) {
  int x0 = sx(x), y0 = sy(y), x1 = sx(x + w), y1 = sy(y + h);
  canvas_.fillRect(x0, y0, x1 - x0, y1 - y0, c);
  mark(x0, y0, x1, y1);
}

void TonyFace::roundBox(float x, float y, float w, float h, float r, uint16_t c) {
  int x0 = sx(x), y0 = sy(y), x1 = sx(x + w), y1 = sy(y + h);
  int rr = static_cast<int>(fminf(r * s_, fminf(x1 - x0, y1 - y0) * 0.5f));
  canvas_.fillRoundRect(x0, y0, x1 - x0, y1 - y0, rr, c);
  mark(x0, y0, x1, y1);
}

// A 1-design-pixel-wide stroke between two design pixel centers.
void TonyFace::line(int x0, int y0, int x1, int y1, uint16_t c, int bold) {
  int ax = sx(x0 + 0.5f), ay = sy(y0 + 0.5f), bx = sx(x1 + 0.5f), by = sy(y1 + 0.5f);
  int th = max(1, static_cast<int>(roundf(s_))) + bold;
  int half = th / 2;
  int dx = abs(bx - ax), stepX = ax < bx ? 1 : -1;
  int dy = -abs(by - ay), stepY = ay < by ? 1 : -1;
  int err = dx + dy;
  while (true) {
    canvas_.fillRect(ax - half, ay - half, th, th, c);
    if (ax == bx && ay == by) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; ax += stepX; }
    if (e2 <= dx) { err += dx; ay += stepY; }
  }
  mark(min(sx(x0 + 0.5f), sx(x1 + 0.5f)) - half, min(sy(y0 + 0.5f), sy(y1 + 0.5f)) - half,
       max(sx(x0 + 0.5f), sx(x1 + 0.5f)) - half + th, max(sy(y0 + 0.5f), sy(y1 + 0.5f)) - half + th);
}

void TonyFace::disc(int cx, int cy, int r, uint16_t c) {
  int x = sx(cx + 0.5f), y = sy(cy + 0.5f), rr = static_cast<int>((r + 0.5f) * s_);
  canvas_.fillCircle(x, y, rr, c);
  mark(x - rr, y - rr, x + rr + 1, y + rr + 1);
}

void TonyFace::glyph(const char* const* rows, int n, int x, int y, uint16_t c) {
  for (int j = 0; j < n; ++j)
    for (int i = 0; rows[j][i]; ++i)
      if (rows[j][i] == '#') rect(x + i, y + j, 1, 1, c);
}

// ── face ────────────────────────────────────────────────────────────────────

void TonyFace::drawEye(const EyeShape& e, float cx, float cy, int inner, float hScale) {
  float w = e.w, h = e.h * hScale * (1.0f - blinkAmount_);
  if (h < 2) h = 2;
  Geo g;
  g.cx = cx - inner * e.dx;
  float ecy = cy + e.dy;
  g.w = w;
  g.left = g.cx - w * 0.5f;
  g.right = g.cx + w * 0.5f;
  g.top = ecy - h * 0.5f;
  g.bot = ecy + h * 0.5f;
  g.rt = fmaxf(0, fminf(e.rt, fminf(w, h) * 0.5f));
  g.rb = fmaxf(0, fminf(e.rb, fminf(w, h) * 0.5f));
  g.st = e.st;
  g.sb = e.sb;
  g.lid = e.lid;
  g.cheek = e.cheek;
  g.inner = inner;

  // The eye, tested at every screen pixel so the edges are smooth at any scale, drawn as horizontal runs.
  float inv = 1.0f / s_;
  int X0 = sx(g.left), X1 = sx(g.right) + 1, Y0 = sy(g.top), Y1 = sy(g.bot) + 1;
  for (int y = Y0; y < Y1; ++y) {
    float py = (y + 0.5f - oy_) * inv;
    int run = -1;
    for (int x = X0; x <= X1; ++x) {
      bool on = x < X1 && inEye(g, (x + 0.5f - ox_) * inv, py);
      if (on && run < 0) run = x;
      if (!on && run >= 0) {
        span(run, y, x - run, colors_.eye);
        run = -1;
      }
    }
  }

  // Pupil: a dark disc with a small highlight, somewhere in the part of the eye that shows, so lids and cheeks
  // cover it like real ones. It also leans the way the whole eye is glancing.
  if (!pack_.glint || h <= 12 || sleepLook()) return;
  float r = constrain(g.w * 0.14f, 3.0f, 6.0f);
  int y0 = static_cast<int>(floorf(g.top)), y1 = static_cast<int>(ceilf(g.bot));
  int x0 = static_cast<int>(floorf(g.left)), x1 = static_cast<int>(ceilf(g.right));
  int top = -1, bot = -1;
  for (int y = y0; y < y1; ++y) {
    if (inEye(g, g.cx, y + 0.5f)) {
      if (top < 0) top = y;
      bot = y;
    }
  }
  if (top < 0) return;
  float vcy = (top + bot + 1) * 0.5f;
  int left = -1, right = -1;
  for (int x = x0; x < x1; ++x) {
    if (inEye(g, x + 0.5f, vcy)) {
      if (left < 0) left = x;
      right = x;
    }
  }
  if (left < 0) return;
  float vcx = (left + right + 1) * 0.5f;
  float roomX = fmaxf(0, (right - left + 1) * 0.5f - r - 2.0f), roomY = fmaxf(0, (bot - top + 1) * 0.5f - r - 1.5f);
  float px = constrain(pupilX_ + gazeX_ / 12.0f, -1.0f, 1.0f), py = constrain(pupilY_ + gazeY_ / 8.0f, -1.0f, 1.0f);
  float pcx = vcx + px * roomX, pcy = vcy + py * roomY;

  int PX0 = sx(pcx - r), PX1 = sx(pcx + r) + 1, PY0 = sy(pcy - r), PY1 = sy(pcy + r) + 1;
  for (int y = PY0; y < PY1; ++y) {
    float dyy = (y + 0.5f - oy_) * inv;
    int run = -1;
    for (int x = PX0; x <= PX1; ++x) {
      float dxx = (x + 0.5f - ox_) * inv;
      float ddx = dxx - pcx, ddy = dyy - pcy;
      bool on = x < PX1 && ddx * ddx + ddy * ddy <= r * r && inEye(g, dxx, dyy);
      if (on && run < 0) run = x;
      if (!on && run >= 0) {
        span(run, y, x - run, colors_.pupil);
        run = -1;
      }
    }
  }
  if (r >= 4) {  // the highlight: up and to the left in the pupil
    int hx = static_cast<int>(floorf(pcx - r * 0.5f)), hy = static_cast<int>(floorf(pcy - r * 0.55f));
    if (inEye(g, hx + 0.5f, hy + 0.5f)) rect(hx, hy, 2, 2, colors_.glint);
  }
}

// A thick stroke above the eye. Brows ignore blinks, so they stay put while the eye shuts under them.
void TonyFace::drawBrow(const EyeShape& e, float cx, float cy, int inner, float hScale) {
  float ecx = cx - inner * e.dx;
  float top = cy + e.dy - e.h * hScale * 0.5f + e.lid * 0.6f;
  float y = top - 4.5f - e.bh;
  float half = e.w * 0.42f;
  float outerY = fmaxf(y - e.bt * 0.5f, 1.5f), innerY = fmaxf(y + e.bt * 0.5f, 1.5f);
  float X0 = ox_ + (ecx - inner * half) * s_, Y0 = oy_ + outerY * s_;
  float X1 = ox_ + (ecx + inner * half) * s_, Y1 = oy_ + innerY * s_;
  float r = 1.4f * s_;
  canvas_.drawWideLine(X0, Y0, X1, Y1, r, colors_.brow);
  mark(static_cast<int>(floorf(fminf(X0, X1) - r - 1)), static_cast<int>(floorf(fminf(Y0, Y1) - r - 1)),
       static_cast<int>(ceilf(fmaxf(X0, X1) + r + 2)), static_cast<int>(ceilf(fmaxf(Y0, Y1) + r + 2)));
}

void TonyFace::drawMouth(int cx, int cy) {
  int my0 = static_cast<int>(floorf(cy + curL_.h * 0.5f));
  if (mouth_ >= 0.05f && pack_.mouth != 2) {
    int mh = 2 + static_cast<int>(floorf(mouth_ * 9));
    int mw = 20 + static_cast<int>(floorf(mouth_ * 8));
    int my = my0 + 4;
    if (my + mh > DESIGN_H - 1) my = DESIGN_H - 1 - mh;
    roundBox(cx - mw / 2, my, mw, mh, fminf(mh / 2, 4), colors_.mouth);
    if (mh > 6) roundBox(cx - mw / 2 + 3, my + 2, mw - 6, mh - 4, 2, colors_.bg);  // darker inside when wide open
  } else if (pack_.mouth == 1) {
    // Resting mouth: a short curve, smiling or frowning, with the right corner hitched up into a smirk.
    const int mw = 14;
    int my = min(my0 + 6, DESIGN_H - 4);
    for (int i = 0; i <= mw; ++i) {
      float u = (i - mw * 0.5f) / (mw * 0.5f);
      float lift = smile_ > -0.5f ? pack_.smirk * (u + 1) * 0.5f : 0.0f;
      int yy = static_cast<int>(floorf(my + smile_ * (1 - u * u) - lift + 0.5f));
      rect(cx - mw / 2 + i, yy, 1, 2, colors_.mouth);
    }
  }
}

void TonyFace::drawFlourishes(float leftOuter, float rightOuter, int eyeTop, int eyeBottom) {
  const EmotionLook& look = pack_.looks[static_cast<int>(emotion_)];
  uint16_t fx = look.fx;
  uint32_t since = millis() - emotionSinceMs_;
  int frame = static_cast<int>(frames_);
  int lo = static_cast<int>(floorf(leftOuter)), ro = static_cast<int>(floorf(rightOuter));

  if ((fx & FX_POP) && since < 900) glyph(kBang, 10, 116, 4, colors_.pop);
  if (fx & FX_BLUSH) {
    for (int i = 0; i < 2; ++i) {  // two short slashes below the outer corner of each eye
      int lx = lo - 8 + i * 3, rx = ro + 3 + i * 3;
      line(lx, eyeBottom + 1, lx + 3, eyeBottom - 3, colors_.blush);
      line(rx, eyeBottom + 1, rx + 3, eyeBottom - 3, colors_.blush);
    }
  }
  if ((fx & FX_TEAR) && tearY_ >= 0) {
    int tx = ro - 3, ty = eyeBottom + static_cast<int>(floorf(tearY_));
    disc(tx, ty, 2, colors_.tear);
    line(tx, ty - 5, tx, ty - 2, colors_.tear);
  }
  if (fx & FX_DOTS) {
    for (int i = 0; i < thinkDots_; ++i) disc(DESIGN_W - 16 + i * 6, 5, 2, colors_.dots);  // clear of the raised brow
  }
  if ((fx & FX_STEAM) && (frame / 6) % 2 == 0) {  // two short strokes above the outer corners
    line(lo - 4, eyeTop - 4, lo - 2, eyeTop - 9, colors_.steam);
    line(ro + 4, eyeTop - 4, ro + 2, eyeTop - 9, colors_.steam);
  }
  if (fx & FX_HEARTS) {
    for (const Heart& h : hearts_) {
      if (!h.alive || (h.age > 0.8f && frame % 2 == 0)) continue;
      glyph(kHeart, 6, static_cast<int>(floorf(h.x)), static_cast<int>(floorf(h.y)), colors_.heart);
    }
  }
  if (fx & FX_QUESTION)
    glyph(kQuestion, 11, 112, 3 + static_cast<int>(floorf(sinf(frames_ * 0.15f) * 2)), colors_.question);
  if (fx & FX_SPARKLE) {
    bool a = (frame / 5) % 2 == 0;
    int pts[4] = {a ? lo - 6 : lo - 5, a ? eyeTop + 3 : eyeBottom - 5, a ? ro + 6 : ro + 5,
                  a ? eyeBottom - 4 : eyeTop + 2};
    for (int k = 0; k < 2; ++k) {
      int x = pts[k * 2], y = pts[k * 2 + 1];
      line(x - 2, y, x + 2, y, colors_.sparkle);
      line(x, y - 2, x, y + 2, colors_.sparkle);
    }
  }
}

void TonyFace::drawZeds() {
  int frame = static_cast<int>(frames_);
  for (const Zed& z : zeds_) {
    if (!z.alive) continue;
    if (z.age > 0.8f && (frame % 2 == 0)) continue;  // flicker out at the end
    int s = z.age < 0.33f ? 4 : z.age < 0.66f ? 6 : 8;
    int x = static_cast<int>(floorf(z.x)), y = static_cast<int>(floorf(z.y)) - s;
    if (x < 0 || x >= DESIGN_W - 4 || y < 0) continue;
    int bold = s >= 8 ? 1 : 0;  // the biggest Z is drawn bold
    line(x, y, x + s, y, colors_.zeds, bold);
    line(x + s, y, x, y + s, colors_.zeds, bold);
    line(x, y + s, x + s, y + s, colors_.zeds, bold);
  }
}

void TonyFace::render() {
  // Only clear and push what changed: last frame's drawing plus this frame's.
  if (!prevDrawn_.empty())
    canvas_.fillRect(prevDrawn_.x0, prevDrawn_.y0, prevDrawn_.x1 - prevDrawn_.x0, prevDrawn_.y1 - prevDrawn_.y0,
                     colors_.bg);
  drawn_ = {0, 0, 0, 0};

  const EmotionLook& look = pack_.looks[static_cast<int>(emotion_)];
  bool lively = !sleepLook();

  // Vertical bob: breathing while asleep, a bounce with the mouth while talking.
  float bob = sleepLook() ? sinf(breath_) * 2.0f : -mouth_ * 2.0f;
  if (lively && (look.fx & FX_BOUNCE)) bob -= fabsf(sinf(frames_ * 0.18f)) * 3.0f;
  float sway = (lively && (look.fx & FX_WOBBLE)) ? sinf(frames_ * 0.08f) * 3.0f : 0.0f;
  float cy = DESIGN_H / 2 + pack_.y + gazeY_ + bob;
  if (talking_) cy -= 2;  // make room for the mouth
  float gx = gazeX_ + jitterX_ + sway;

  // Height scale: emotion-change overshoot, idle squint, dream twitch.
  float hScale = 1.0f + pop_ * look.pop;
  hScale *= 1.0f - squint_ * 0.4f;
  if (sleepLook()) hScale += twitch_ * 2.5f;  // eyelids flutter open a crack

  float lcx = DESIGN_W / 2 - pack_.gap / 2 - curL_.w / 2 + gx;
  float rcx = DESIGN_W / 2 + pack_.gap / 2 + curR_.w / 2 + gx;
  drawEye(curL_, lcx, cy, 1, hScale);
  drawEye(curR_, rcx, cy, -1, hScale);
  if (pack_.brows) {
    float browScale = sleepLook() ? 1.0f : hScale;  // brows stay still through the dream twitch
    drawBrow(curL_, lcx, cy, 1, browScale);
    drawBrow(curR_, rcx, cy, -1, browScale);
  }
  float leftOuter = lcx - curL_.dx - curL_.w / 2;
  float rightOuter = rcx + curR_.dx + curR_.w / 2;

  int eyeTop = static_cast<int>(floorf(cy - curL_.h * hScale / 2));
  int eyeBottom = static_cast<int>(floorf(cy + curL_.h * hScale / 2));
  if (sleepLook()) {
    drawZeds();
  } else {
    drawFlourishes(leftOuter, rightOuter, eyeTop, eyeBottom);
    drawMouth(DESIGN_W / 2 + static_cast<int>(floorf(gazeX_ * 0.5f)), static_cast<int>(floorf(cy)));
  }

  Box push = drawn_;
  if (!prevDrawn_.empty()) {
    if (push.empty()) push = prevDrawn_;
    push.x0 = min(push.x0, prevDrawn_.x0);
    push.y0 = min(push.y0, prevDrawn_.y0);
    push.x1 = max(push.x1, prevDrawn_.x1);
    push.y1 = max(push.y1, prevDrawn_.y1);
  }
  push.x0 = max(push.x0, 0);
  push.y0 = max(push.y0, 0);
  push.x1 = min(push.x1, screenW_);
  push.y1 = min(push.y1, screenH_);
  if (!push.empty()) {
    // The clip keeps the transfer to the dirty box; the sprite's position on screen never changes.
    lcd_.setClipRect(x_ + push.x0, y_ + push.y0, push.x1 - push.x0, push.y1 - push.y0);
    canvas_.pushSprite(&lcd_, x_, y_);
    lcd_.clearClipRect();
  }
  prevDrawn_ = drawn_;
}
