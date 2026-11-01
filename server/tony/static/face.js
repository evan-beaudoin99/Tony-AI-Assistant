// Tony's 320x240 screen, in the browser: the same face, status bar and caption the ESP32 draws.
// The face is a straight port of firmware/src/TonyFace.cpp (Rocky's eye engine, github.com/cgro00/desk-robot, MIT),
// and the layout follows firmware/src/Display.cpp, so what you see here is what the hardware shows.

(() => {
  const DESIGN_W = 128, DESIGN_H = 64;
  const FRAME_MS = 1000 / 30;
  const UI = { W: 320, H: 240, BAR_H: 40, FACE_Y: 40, FACE_H: 160, CAPTION_Y: 200 };

  const FX = {
    BLUSH: 1 << 0, TEAR: 1 << 1, DOTS: 1 << 2, POP: 1 << 3, STEAM: 1 << 4, SHAKE: 1 << 5,
    HEARTS: 1 << 6, QUESTION: 1 << 7, SPARKLE: 1 << 8, BOUNCE: 1 << 9, WOBBLE: 1 << 10,
  };
  const C = {
    bg: "#000", eye: "#ffcc00", pupil: "#000", glint: "#fff", mouth: "#ffcc00", brow: "#ebebeb",
    blush: "#ff6464", tear: "#5ab9ff", steam: "#ff8800", heart: "#ff1450", pop: "#fff", question: "#fff",
    sparkle: "#fff", dots: "#fff", zeds: "#96cdff",
    taxi: "#ffcc00", white: "#fff", gray: "#6e6e6e", soft: "#aaa", green: "#3cdc5a", orange: "#ff9600", red: "#ff2828",
  };

  const BANG = ["###", "###", "###", "###", "###", "###", "###", "...", "###", "###"];
  const QUESTION = [".#####.", "##...##", "##...##", ".....##", "....##.", "...##..", "..##...", "..##...", ".......",
                    "..##...", "..##..."];
  const HEART = [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."];

  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const ease = (f, t) => 1 - Math.pow(1 - f, t);
  const approach = (cur, target, k) => cur + (target - cur) * k;
  const random = (a, b) => (b === undefined ? Math.floor(Math.random() * a) : a + Math.floor(Math.random() * (b - a)));
  const KEYS = ["w", "h", "rt", "rb", "st", "sb", "lid", "cheek", "dx", "dy", "bh", "bt"];

  function eye(w, h, rt, rb, st = 0, sb = 0, lid = 0, cheek = 0, dx = 0, dy = 0) {
    return { w, h, rt, rb, st, sb, lid, cheek, dx, dy, bh: 0, bt: 0 };
  }
  const brow = (e, bh, bt) => ({ ...e, bh, bt });
  const both = (e, fx = 0, pop = 0.12, gazeX = 0, gazeY = 0) => ({ l: { ...e }, r: { ...e }, fx, pop, gazeX, gazeY, smile: 0 });
  const morph = (c, t, k) => { for (const key of KEYS) c[key] = approach(c[key], t[key], k); };

  function tonyPack() {
    const L = {};
    L.neutral = both(eye(34, 34, 12, 12)); L.neutral.smile = 1.0;
    L.happy = both(brow(eye(36, 36, 12, 12, 0, 0, 0, 16), 2, -1), FX.BLUSH); L.happy.smile = 3.0;
    L.sad = both(brow(eye(32, 26, 10, 10, -9, 0, 4), 1, -6), FX.TEAR); L.sad.smile = -2.5;
    L.angry = both(brow(eye(34, 24, 8, 8, 10), -2, 8), FX.STEAM | FX.SHAKE); L.angry.smile = -2.0;
    L.surprised = both(brow(eye(38, 44, 19, 19), 6, 0), FX.POP, 0.3);
    L.sleepy = both(brow(eye(34, 26, 10, 10, 0, 0, 14), -2, 0));
    L.thinking = both(brow(eye(30, 30, 11, 11, 0, 0, 5), 1, 1), FX.DOTS, 0.12, 7, -5);
    L.thinking.r = brow(eye(30, 30, 11, 11, 0, 0, 5), 5, -2); L.thinking.smile = -0.5;
    L.curious = both(eye(30, 30, 11, 11), 0, 0.15, -5, -3);
    L.curious.r = brow(eye(38, 40, 15, 15, 0, 0, 0, 0, 0, -2), 5, -1); L.curious.smile = 0.5;
    L.skeptical = both(brow(eye(34, 38, 13, 12, -3), 6, -2), 0, 0.12, 4, 0);
    L.skeptical.r = brow(eye(34, 34, 12, 12, 0, 0, 17), -3, 4); L.skeptical.smile = -1.0;
    L.excited = both(brow(eye(38, 40, 16, 16, 0, 0, 0, 10), 5, -1), FX.SPARKLE | FX.BOUNCE, 0.25); L.excited.smile = 3.5;
    L.love = both(brow(eye(36, 34, 14, 14, 0, 0, 0, 12), 3, -2), FX.HEARTS | FX.BLUSH, 0.2); L.love.smile = 3.0;
    L.confused = both(brow(eye(32, 36, 13, 12, -5), 5, -3), FX.WOBBLE);
    L.confused.r = brow(eye(28, 24, 10, 10, 4, 0, 0, 0, 0, 4), -1, 4); L.confused.smile = -1.0;
    return { gap: 16, y: 0, glint: true, brows: true, mouth: 1, smirk: 1.5, looks: L, asleep: brow(eye(30, 3, 1, 1), -1, 0) };
  }

  function inEye(g, x, y) {
    if (x < g.left || x >= g.right || y < g.top || y >= g.bot) return false;
    const u = ((x - g.cx) / (g.w * 0.5)) * g.inner;
    const topCut = g.st >= 0 ? g.st * (u + 1) * 0.5 : -g.st * (1 - u) * 0.5;
    const botCut = g.sb >= 0 ? g.sb * (u + 1) * 0.5 : -g.sb * (1 - u) * 0.5;
    const hh = g.bot - g.top;
    const yy = y - (topCut * (g.bot - y)) / hh + (botCut * (y - g.top)) / hh;
    if (yy < g.top + g.lid || yy >= g.bot) return false;
    if (yy < g.top + g.rt) {
      const qx = clamp(x, g.left + g.rt, g.right - g.rt), dx = x - qx, dy = yy - (g.top + g.rt);
      if (dx * dx + dy * dy > g.rt * g.rt) return false;
    }
    if (yy > g.bot - g.rb) {
      const qx = clamp(x, g.left + g.rb, g.right - g.rb), dx = x - qx, dy = yy - (g.bot - g.rb);
      if (dx * dx + dy * dy > g.rb * g.rb) return false;
    }
    if (g.cheek > 0.5) {
      const rr = g.w, ccy = g.bot + rr - g.cheek, dx = x - g.cx, dy = y - ccy;
      if (dx * dx + dy * dy < rr * rr) return false;
    }
    return true;
  }

  class TonyFace {
    constructor(w, h) {
      this.w = w; this.h = h;
      this.s = Math.min(w / DESIGN_W, h / DESIGN_H);
      this.ox = (w - DESIGN_W * this.s) * 0.5;
      this.oy = (h - DESIGN_H * this.s) * 0.5;
      this.pack = tonyPack();
      this.emotion = "neutral";
      this.curL = { ...this.pack.looks.neutral.l };
      this.curR = { ...this.pack.looks.neutral.r };
      this.smile = this.pack.looks.neutral.smile;
      const now = performance.now();
      Object.assign(this, {
        emotionSince: 0, blinkAmount: 1, blinkClosing: false, nextBlink: now + 3000,
        gazeX: 0, gazeY: 0, gazeTargetX: 0, gazeTargetY: 0, nextSaccade: now + 1500,
        pupilX: 0, pupilY: 0, pupilTargetX: 0, pupilTargetY: 0, nextPupil: 0,
        pop: 0, squint: 0, nextSquint: now + 9000, tearY: -1, nextTear: 0, thinkDots: 0, nextDot: 0,
        jitterX: 0, lastJitterFrame: -1, hearts: [{}, {}, {}], nextHeart: 0, heartSide: false,
        asleep: false, breath: 0, zeds: [{}, {}, {}], nextZed: 0, nextTwitch: 0, twitch: 0,
        talking: false, mouthLevel: 0, mouth: 0, idle: true, lastFrame: now, frames: 0,
      });
    }

    look() { return this.pack.looks[this.emotion]; }

    setEmotion(name) {
      if (!this.pack.looks[name]) return;
      const now = performance.now();
      if (name !== this.emotion) {
        this.pop = 1;
        this.emotionSince = now;
        this.tearY = -1;
        this.nextTear = now + 1500;
        this.thinkDots = 0;
        this.hearts.forEach((h) => (h.alive = false));
        this.nextHeart = now + 200;
      }
      this.emotion = name;
      this.gazeTargetX = this.look().gazeX;
      this.gazeTargetY = this.look().gazeY;
    }

    blink() { this.blinkClosing = true; }

    setAsleep(on) {
      if (on === this.asleep) return;
      this.asleep = on;
      const now = performance.now();
      if (on) {
        this.zeds.forEach((z) => (z.alive = false));
        this.nextZed = now + 800;
        this.nextTwitch = now + random(5000, 9000);
        this.gazeTargetX = this.gazeTargetY = 0;
      } else {
        this.pop = 1;
        this.blinkAmount = 0;
        this.setEmotion(this.emotion);
      }
    }

    setTalking(on, level = 0) {
      this.talking = on;
      this.mouthLevel = clamp(level, 0, 1);
    }

    update(now, ctx) {
      const t = clamp((now - this.lastFrame) / FRAME_MS, 0, 3);
      this.lastFrame = now;
      this.step(now, t);
      this.render(now, ctx);
    }

    step(now, t) {
      this.frames += t;
      const look = this.look();
      const asleep = this.asleep;
      const k = ease(asleep ? 0.12 : 0.25, t);
      morph(this.curL, asleep ? this.pack.asleep : look.l, k);
      morph(this.curR, asleep ? this.pack.asleep : look.r, k);
      this.smile = approach(this.smile, asleep ? 0 : look.smile, k);
      this.pop *= Math.pow(0.82, t);
      this.squint *= Math.pow(0.85, t);

      if (asleep) {
        this.breath += 0.035 * t;
        this.blinkAmount = 0;
        this.blinkClosing = false;
        this.twitch *= Math.pow(0.8, t);
        if (now >= this.nextTwitch) { this.twitch = 1; this.nextTwitch = now + random(6000, 12000); }
        if (now >= this.nextZed) {
          const z = this.zeds.find((z) => !z.alive);
          if (z) Object.assign(z, { alive: true, age: 0, x: Math.min(DESIGN_W / 2 + this.pack.gap / 2 + this.curL.w + 2, 108), y: DESIGN_H / 2 - 6 });
          this.nextZed = now + random(1100, 1700);
        }
        for (const z of this.zeds) {
          if (!z.alive) continue;
          z.age += 0.012 * t; z.y -= 0.32 * t; z.x += 0.22 * t;
          if (z.age >= 1 || z.y < -12) z.alive = false;
        }
        const g = ease(0.2, t);
        this.gazeX = approach(this.gazeX, 0, g);
        this.gazeY = approach(this.gazeY, 0, g);
        this.mouth = approach(this.mouth, 0, ease(0.3, t));
        return;
      }

      if (this.blinkClosing) {
        this.blinkAmount += 0.45 * t;
        if (this.blinkAmount >= 1) { this.blinkAmount = 1; this.blinkClosing = false; }
      } else if (this.blinkAmount > 0) {
        this.blinkAmount = Math.max(0, this.blinkAmount - 0.28 * t);
      }

      if (this.idle) {
        if (now >= this.nextBlink) {
          this.blinkClosing = true;
          this.nextBlink = now + random(2200, 6000);
          if (random(100) < 20) this.nextBlink = now + 400;
        }
        const looksSomewhere = look.gazeX !== 0 || look.gazeY !== 0;
        if (now >= this.nextSaccade && !looksSomewhere) {
          this.gazeTargetX = random(-8, 9);
          this.gazeTargetY = random(-4, 5);
          if (random(100) < 40) this.gazeTargetX = this.gazeTargetY = 0;
          this.nextSaccade = now + random(1200, 4000);
        }
        if (now >= this.nextPupil) {
          if (random(100) < 35) {
            this.pupilTargetX = random(-25, 26) / 100;
            this.pupilTargetY = random(-20, 21) / 100;
          } else {
            this.pupilTargetX = random(-90, 91) / 100;
            this.pupilTargetY = random(-60, 61) / 100;
          }
          this.nextPupil = now + random(600, 2600);
        }
        if (now >= this.nextSquint && this.emotion === "neutral") {
          this.squint = 1;
          this.nextSquint = now + random(8000, 16000);
        }
      }

      if (look.fx & FX.TEAR) {
        if (this.tearY < 0 && now >= this.nextTear) this.tearY = 0;
        if (this.tearY >= 0) {
          this.tearY += 0.7 * t;
          if (this.tearY > DESIGN_H) { this.tearY = -1; this.nextTear = now + random(2500, 5000); }
        }
      }
      if (look.fx & FX.DOTS && now >= this.nextDot) {
        this.thinkDots = (this.thinkDots + 1) % 4;
        this.nextDot = now + 420;
      }
      if (look.fx & FX.HEARTS && now >= this.nextHeart) {
        const h = this.hearts.find((h) => !h.alive);
        if (h) {
          this.heartSide = !this.heartSide;
          Object.assign(h, {
            alive: true, age: 0, y: DESIGN_H / 2 + 6,
            x: this.heartSide ? DESIGN_W / 2 + this.pack.gap / 2 + this.curR.w + 2 : DESIGN_W / 2 - this.pack.gap / 2 - this.curL.w - 9,
          });
        }
        this.nextHeart = now + random(600, 1000);
      }
      for (const h of this.hearts) {
        if (!h.alive) continue;
        h.age += 0.015 * t; h.y -= 0.45 * t;
        if (h.age >= 1 || h.y < -6) h.alive = false;
      }
      const frame = Math.floor(this.frames);
      if (frame !== this.lastJitterFrame) {
        this.lastJitterFrame = frame;
        this.jitterX = look.fx & FX.SHAKE && frame % 3 === 0 ? random(-1, 2) : 0;
      }

      const mouthGoal = this.talking ? clamp(0.15 + this.mouthLevel * 4, 0.15, 1) : 0;
      this.mouth = approach(this.mouth, mouthGoal, ease(this.talking ? 0.5 : 0.3, t));
      const g = ease(0.35, t);
      this.gazeX = approach(this.gazeX, this.gazeTargetX, g);
      this.gazeY = approach(this.gazeY, this.gazeTargetY, g);
      const p = ease(0.5, t);
      this.pupilX = approach(this.pupilX, this.pupilTargetX, p);
      this.pupilY = approach(this.pupilY, this.pupilTargetY, p);
    }

    // ── drawing (design pixels in, canvas pixels out) ──
    sx(x) { return Math.floor(this.ox + x * this.s); }
    sy(y) { return Math.floor(this.oy + y * this.s); }
    span(x, y, w, c) { this.ctx.fillStyle = c; this.ctx.fillRect(x, y, w, 1); }
    rect(x, y, w, h, c) {
      const x0 = this.sx(x), y0 = this.sy(y);
      this.ctx.fillStyle = c;
      this.ctx.fillRect(x0, y0, this.sx(x + w) - x0, this.sy(y + h) - y0);
    }
    roundBox(x, y, w, h, r, c) {
      const x0 = this.sx(x), y0 = this.sy(y), x1 = this.sx(x + w), y1 = this.sy(y + h);
      const rr = Math.min(r * this.s, Math.min(x1 - x0, y1 - y0) * 0.5);
      this.ctx.fillStyle = c;
      this.ctx.beginPath();
      this.ctx.roundRect(x0, y0, x1 - x0, y1 - y0, rr);
      this.ctx.fill();
    }
    line(x0, y0, x1, y1, c, bold = 0) {
      let ax = this.sx(x0 + 0.5), ay = this.sy(y0 + 0.5);
      const bx = this.sx(x1 + 0.5), by = this.sy(y1 + 0.5);
      const th = Math.max(1, Math.round(this.s)) + bold, half = Math.floor(th / 2);
      const dx = Math.abs(bx - ax), stepX = ax < bx ? 1 : -1;
      const dy = -Math.abs(by - ay), stepY = ay < by ? 1 : -1;
      let err = dx + dy;
      this.ctx.fillStyle = c;
      for (;;) {
        this.ctx.fillRect(ax - half, ay - half, th, th);
        if (ax === bx && ay === by) break;
        const e2 = 2 * err;
        if (e2 >= dy) { err += dy; ax += stepX; }
        if (e2 <= dx) { err += dx; ay += stepY; }
      }
    }
    disc(cx, cy, r, c) {
      const x = this.sx(cx + 0.5), y = this.sy(cy + 0.5), rr = Math.floor((r + 0.5) * this.s);
      this.ctx.fillStyle = c;
      this.ctx.beginPath();
      this.ctx.arc(x + 0.5, y + 0.5, rr + 0.5, 0, Math.PI * 2);
      this.ctx.fill();
    }
    glyph(rows, x, y, c) {
      rows.forEach((row, j) => [...row].forEach((ch, i) => ch === "#" && this.rect(x + i, y + j, 1, 1, c)));
    }

    drawEye(e, cx, cy, inner, hScale) {
      const w = e.w;
      let h = e.h * hScale * (1 - this.blinkAmount);
      if (h < 2) h = 2;
      const gcx = cx - inner * e.dx, ecy = cy + e.dy;
      const g = {
        cx: gcx, w, left: gcx - w * 0.5, right: gcx + w * 0.5, top: ecy - h * 0.5, bot: ecy + h * 0.5,
        rt: Math.max(0, Math.min(e.rt, Math.min(w, h) * 0.5)), rb: Math.max(0, Math.min(e.rb, Math.min(w, h) * 0.5)),
        st: e.st, sb: e.sb, lid: e.lid, cheek: e.cheek, inner,
      };
      const inv = 1 / this.s;
      const X0 = this.sx(g.left), X1 = this.sx(g.right) + 1, Y0 = this.sy(g.top), Y1 = this.sy(g.bot) + 1;
      for (let y = Y0; y < Y1; ++y) {
        const py = (y + 0.5 - this.oy) * inv;
        let run = -1;
        for (let x = X0; x <= X1; ++x) {
          const on = x < X1 && inEye(g, (x + 0.5 - this.ox) * inv, py);
          if (on && run < 0) run = x;
          if (!on && run >= 0) { this.span(run, y, x - run, C.eye); run = -1; }
        }
      }

      if (!this.pack.glint || h <= 12 || this.asleep) return;
      const r = clamp(g.w * 0.14, 3, 6);
      const y0 = Math.floor(g.top), y1 = Math.ceil(g.bot), x0 = Math.floor(g.left), x1 = Math.ceil(g.right);
      let top = -1, bot = -1;
      for (let y = y0; y < y1; ++y) if (inEye(g, g.cx, y + 0.5)) { if (top < 0) top = y; bot = y; }
      if (top < 0) return;
      const vcy = (top + bot + 1) * 0.5;
      let left = -1, right = -1;
      for (let x = x0; x < x1; ++x) if (inEye(g, x + 0.5, vcy)) { if (left < 0) left = x; right = x; }
      if (left < 0) return;
      const vcx = (left + right + 1) * 0.5;
      const roomX = Math.max(0, (right - left + 1) * 0.5 - r - 2), roomY = Math.max(0, (bot - top + 1) * 0.5 - r - 1.5);
      const px = clamp(this.pupilX + this.gazeX / 12, -1, 1), pyy = clamp(this.pupilY + this.gazeY / 8, -1, 1);
      const pcx = vcx + px * roomX, pcy = vcy + pyy * roomY;
      const PX0 = this.sx(pcx - r), PX1 = this.sx(pcx + r) + 1, PY0 = this.sy(pcy - r), PY1 = this.sy(pcy + r) + 1;
      for (let y = PY0; y < PY1; ++y) {
        const dyy = (y + 0.5 - this.oy) * inv;
        let run = -1;
        for (let x = PX0; x <= PX1; ++x) {
          const dxx = (x + 0.5 - this.ox) * inv, ddx = dxx - pcx, ddy = dyy - pcy;
          const on = x < PX1 && ddx * ddx + ddy * ddy <= r * r && inEye(g, dxx, dyy);
          if (on && run < 0) run = x;
          if (!on && run >= 0) { this.span(run, y, x - run, C.pupil); run = -1; }
        }
      }
      if (r >= 4) {
        const hx = Math.floor(pcx - r * 0.5), hy = Math.floor(pcy - r * 0.55);
        if (inEye(g, hx + 0.5, hy + 0.5)) this.rect(hx, hy, 2, 2, C.glint);
      }
    }

    drawBrow(e, cx, cy, inner, hScale) {
      const ecx = cx - inner * e.dx;
      const top = cy + e.dy - e.h * hScale * 0.5 + e.lid * 0.6;
      const y = top - 4.5 - e.bh, half = e.w * 0.42;
      const outerY = Math.max(y - e.bt * 0.5, 1.5), innerY = Math.max(y + e.bt * 0.5, 1.5);
      const ctx = this.ctx;
      ctx.strokeStyle = C.brow;
      ctx.lineWidth = 2.8 * this.s;
      ctx.lineCap = "round";
      ctx.beginPath();
      ctx.moveTo(this.ox + (ecx - inner * half) * this.s, this.oy + outerY * this.s);
      ctx.lineTo(this.ox + (ecx + inner * half) * this.s, this.oy + innerY * this.s);
      ctx.stroke();
    }

    drawMouth(cx, cy) {
      const my0 = Math.floor(cy + this.curL.h * 0.5);
      if (this.mouth >= 0.05 && this.pack.mouth !== 2) {
        const mh = 2 + Math.floor(this.mouth * 9), mw = 20 + Math.floor(this.mouth * 8);
        let my = my0 + 4;
        if (my + mh > DESIGN_H - 1) my = DESIGN_H - 1 - mh;
        this.roundBox(cx - Math.trunc(mw / 2), my, mw, mh, Math.min(Math.trunc(mh / 2), 4), C.mouth);
        if (mh > 6) this.roundBox(cx - Math.trunc(mw / 2) + 3, my + 2, mw - 6, mh - 4, 2, C.bg);
      } else if (this.pack.mouth === 1) {
        const mw = 14, my = Math.min(my0 + 6, DESIGN_H - 4);
        for (let i = 0; i <= mw; ++i) {
          const u = (i - mw * 0.5) / (mw * 0.5);
          const lift = this.smile > -0.5 ? (this.pack.smirk * (u + 1)) * 0.5 : 0;
          const yy = Math.floor(my + this.smile * (1 - u * u) - lift + 0.5);
          this.rect(cx - mw / 2 + i, yy, 1, 2, C.mouth);
        }
      }
    }

    drawFlourishes(now, leftOuter, rightOuter, eyeTop, eyeBottom) {
      const fx = this.look().fx, since = now - this.emotionSince, frame = Math.floor(this.frames);
      const lo = Math.floor(leftOuter), ro = Math.floor(rightOuter);
      if (fx & FX.POP && since < 900) this.glyph(BANG, 116, 4, C.pop);
      if (fx & FX.BLUSH) {
        for (let i = 0; i < 2; ++i) {
          const lx = lo - 8 + i * 3, rx = ro + 3 + i * 3;
          this.line(lx, eyeBottom + 1, lx + 3, eyeBottom - 3, C.blush);
          this.line(rx, eyeBottom + 1, rx + 3, eyeBottom - 3, C.blush);
        }
      }
      if (fx & FX.TEAR && this.tearY >= 0) {
        const tx = ro - 3, ty = eyeBottom + Math.floor(this.tearY);
        this.disc(tx, ty, 2, C.tear);
        this.line(tx, ty - 5, tx, ty - 2, C.tear);
      }
      if (fx & FX.DOTS) for (let i = 0; i < this.thinkDots; ++i) this.disc(DESIGN_W - 16 + i * 6, 5, 2, C.dots);
      if (fx & FX.STEAM && Math.floor(frame / 6) % 2 === 0) {
        this.line(lo - 4, eyeTop - 4, lo - 2, eyeTop - 9, C.steam);
        this.line(ro + 4, eyeTop - 4, ro + 2, eyeTop - 9, C.steam);
      }
      if (fx & FX.HEARTS) {
        for (const h of this.hearts) {
          if (!h.alive || (h.age > 0.8 && frame % 2 === 0)) continue;
          this.glyph(HEART, Math.floor(h.x), Math.floor(h.y), C.heart);
        }
      }
      if (fx & FX.QUESTION) this.glyph(QUESTION, 112, 3 + Math.floor(Math.sin(this.frames * 0.15) * 2), C.question);
      if (fx & FX.SPARKLE) {
        const a = Math.floor(frame / 5) % 2 === 0;
        const pts = [a ? lo - 6 : lo - 5, a ? eyeTop + 3 : eyeBottom - 5, a ? ro + 6 : ro + 5, a ? eyeBottom - 4 : eyeTop + 2];
        for (let k = 0; k < 2; ++k) {
          const x = pts[k * 2], y = pts[k * 2 + 1];
          this.line(x - 2, y, x + 2, y, C.sparkle);
          this.line(x, y - 2, x, y + 2, C.sparkle);
        }
      }
    }

    drawZeds() {
      const frame = Math.floor(this.frames);
      for (const z of this.zeds) {
        if (!z.alive || (z.age > 0.8 && frame % 2 === 0)) continue;
        const s = z.age < 0.33 ? 4 : z.age < 0.66 ? 6 : 8;
        const x = Math.floor(z.x), y = Math.floor(z.y) - s;
        if (x < 0 || x >= DESIGN_W - 4 || y < 0) continue;
        const bold = s >= 8 ? 1 : 0;
        this.line(x, y, x + s, y, C.zeds, bold);
        this.line(x + s, y, x, y + s, C.zeds, bold);
        this.line(x, y + s, x + s, y + s, C.zeds, bold);
      }
    }

    render(now, ctx) {
      this.ctx = ctx;
      ctx.fillStyle = C.bg;
      ctx.fillRect(0, 0, this.w, this.h);
      const look = this.look(), lively = !this.asleep;
      let bob = this.asleep ? Math.sin(this.breath) * 2 : -this.mouth * 2;
      if (lively && look.fx & FX.BOUNCE) bob -= Math.abs(Math.sin(this.frames * 0.18)) * 3;
      const sway = lively && look.fx & FX.WOBBLE ? Math.sin(this.frames * 0.08) * 3 : 0;
      let cy = DESIGN_H / 2 + this.pack.y + this.gazeY + bob;
      if (this.talking) cy -= 2;
      const gx = this.gazeX + this.jitterX + sway;

      let hScale = 1 + this.pop * look.pop;
      hScale *= 1 - this.squint * 0.4;
      if (this.asleep) hScale += this.twitch * 2.5;

      const lcx = DESIGN_W / 2 - this.pack.gap / 2 - this.curL.w / 2 + gx;
      const rcx = DESIGN_W / 2 + this.pack.gap / 2 + this.curR.w / 2 + gx;
      this.drawEye(this.curL, lcx, cy, 1, hScale);
      this.drawEye(this.curR, rcx, cy, -1, hScale);
      if (this.pack.brows) {
        const browScale = this.asleep ? 1 : hScale;
        this.drawBrow(this.curL, lcx, cy, 1, browScale);
        this.drawBrow(this.curR, rcx, cy, -1, browScale);
      }
      const leftOuter = lcx - this.curL.dx - this.curL.w / 2, rightOuter = rcx + this.curR.dx + this.curR.w / 2;
      const eyeTop = Math.floor(cy - (this.curL.h * hScale) / 2), eyeBottom = Math.floor(cy + (this.curL.h * hScale) / 2);
      if (this.asleep) {
        this.drawZeds();
      } else {
        this.drawFlourishes(now, leftOuter, rightOuter, eyeTop, eyeBottom);
        this.drawMouth(DESIGN_W / 2 + Math.floor(this.gazeX * 0.5), Math.floor(cy));
      }
    }
  }

  // The whole 320x240 screen: status bar, face, caption, and full-screen cards. Mirrors firmware/src/main.cpp.
  class TonyScreen {
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = canvas.getContext("2d");
      this.faceCanvas = document.createElement("canvas");
      this.faceCanvas.width = UI.W;
      this.faceCanvas.height = UI.FACE_H;
      this.faceCtx = this.faceCanvas.getContext("2d");
      this.face = new TonyFace(UI.W, UI.FACE_H);
      this.status = { wifi: true, server: false, listening: false, camera: false, asleep: false, battery: -1 };
      this.stateCaption = "Lookin' for my server...";
      this.flashCaption = "";
      this.flashUntil = 0;
      this.card = null;
      this.cardUntil = 0;
      this.level = () => 0;
      this.speaking = false;
      requestAnimationFrame(this.frame.bind(this));
    }

    connected(on) {
      this.status.server = on;
      if (on) {
        if (!this.status.asleep) { this.face.setEmotion("happy"); this.stateCaption = ""; }
      } else {
        this.speaking = false;
        this.status.listening = false;
        if (!this.status.asleep) { this.face.setEmotion("confused"); this.stateCaption = "Lookin' for my server..."; }
      }
    }

    setState(s) {
      this.speaking = s === "speaking";
      this.status.listening = s === "listening";
      if (this.status.asleep) return;
      if (s === "listening") { this.face.setEmotion("curious"); this.stateCaption = "I'm listenin'..."; }
      else if (s === "thinking") { this.face.setEmotion("thinking"); this.stateCaption = "Lemme think..."; }
      else if (s === "speaking") this.stateCaption = "";
      else { this.face.setEmotion("neutral"); this.stateCaption = ""; }
    }

    setAsleep(on) {
      if (on === this.status.asleep) return;
      this.status.asleep = on;
      this.face.setAsleep(on);
      if (on) this.showFace();
    }

    emotion(name) { if (!this.status.asleep) this.face.setEmotion(name); }
    caption(text, ms = 3000) { this.flashCaption = text; this.flashUntil = performance.now() + ms; }
    setCamera(on) { this.status.camera = on; }
    setBattery(percent) { this.status.battery = percent == null ? -1 : percent; }

    showCard(jpegB64, seconds = 15) {
      const img = new Image();
      img.onload = () => { this.card = img; this.cardUntil = performance.now() + (seconds || 60) * 1000; };
      img.src = "data:image/jpeg;base64," + jpegB64;
    }
    showFace() { this.card = null; }

    frame(now) {
      requestAnimationFrame(this.frame.bind(this));
      const ctx = this.ctx;
      if (this.card && now >= this.cardUntil) this.card = null;
      if (this.card) {
        ctx.drawImage(this.card, 0, 0, UI.W, UI.H);
        return;
      }
      ctx.fillStyle = C.bg;
      ctx.fillRect(0, 0, UI.W, UI.H);
      this.drawStatus(ctx);
      const talking = this.speaking && !this.status.asleep;
      this.face.setTalking(talking, talking ? this.level() : 0);
      this.face.update(now, this.faceCtx);
      ctx.drawImage(this.faceCanvas, 0, UI.FACE_Y);
      const text = this.status.asleep ? "Zzz... hold the button to wake me."
        : now < this.flashUntil ? this.flashCaption
        : !this.status.wifi ? "No WiFi. Check secrets.h."
        : this.stateCaption;
      this.drawCaption(ctx, text);
    }

    drawStatus(ctx) {
      const s = this.status;
      ctx.textAlign = "left";
      ctx.textBaseline = "middle";
      ctx.font = "bold 15px Arial, Helvetica, sans-serif";
      ctx.fillStyle = C.taxi;
      ctx.fillText("TONY", 10, 21);
      this.circle(ctx, 72, 20, 5, s.server ? C.green : s.wifi ? C.orange : C.red);

      let x = UI.W - 10 - 30;
      if (s.battery >= 0) this.drawBattery(ctx, x, 13, s.battery);
      else {
        ctx.font = "13px Arial, Helvetica, sans-serif";
        ctx.fillStyle = C.soft;
        ctx.textAlign = "right";
        ctx.fillText("USB", UI.W - 10, 21);
      }
      x -= 42;
      this.drawCamera(ctx, x, 12, s.camera);
      if (s.listening && !s.asleep) { x -= 26; this.drawMic(ctx, x, 10); }
    }

    circle(ctx, x, y, r, c) {
      ctx.fillStyle = c;
      ctx.beginPath();
      ctx.arc(x + 0.5, y + 0.5, r + 0.5, 0, Math.PI * 2);
      ctx.fill();
    }

    roundRect(ctx, x, y, w, h, r, c, stroke = false) {
      ctx.beginPath();
      if (stroke) { ctx.strokeStyle = c; ctx.lineWidth = 1; ctx.roundRect(x + 0.5, y + 0.5, w - 1, h - 1, r); ctx.stroke(); }
      else { ctx.fillStyle = c; ctx.roundRect(x, y, w, h, r); ctx.fill(); }
    }

    drawBattery(ctx, x, y, percent) {
      const c = percent > 40 ? C.green : percent > 15 ? C.orange : C.red;
      this.roundRect(ctx, x, y, 26, 14, 3, C.white, true);
      ctx.fillStyle = C.white;
      ctx.fillRect(x + 26, y + 4, 3, 6);
      const w = Math.floor((clamp(percent, 0, 100) * 22) / 100);
      if (w > 0) { ctx.fillStyle = c; ctx.fillRect(x + 2, y + 2, w, 10); }
    }

    drawCamera(ctx, x, y, on) {
      const c = on ? C.red : C.gray;
      this.roundRect(ctx, x, y + 3, 26, 14, 3, c);
      ctx.fillStyle = c;
      ctx.fillRect(x + 8, y, 10, 4);
      this.circle(ctx, x + 13, y + 10, 5, C.bg);
      this.circle(ctx, x + 13, y + 10, 3, c);
      if (on) this.circle(ctx, x + 34, y + 10, 4, C.red);
      else {
        ctx.lineWidth = 1;
        [[-2, C.bg], [-1, C.gray], [0, C.bg]].forEach(([d, col]) => {
          ctx.strokeStyle = col;
          ctx.beginPath();
          ctx.moveTo(x + d + 0.5, y + 19.5);
          ctx.lineTo(x + d + 30.5, y - 0.5);
          ctx.stroke();
        });
      }
    }

    drawMic(ctx, x, y) {
      this.roundRect(ctx, x + 5, y, 10, 14, 5, C.green);
      ctx.strokeStyle = C.green;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(x + 10.5, y + 9.5, 8.5, 0, Math.PI);
      ctx.stroke();
      ctx.fillStyle = C.green;
      ctx.fillRect(x + 9, y + 18, 2, 3);
    }

    drawCaption(ctx, text) {
      if (!text) return;
      ctx.font = "15px Arial, Helvetica, sans-serif";
      ctx.fillStyle = C.soft;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      const maxWidth = UI.W - 16;
      let line = text;
      if (ctx.measureText(line).width > maxWidth) {
        while (line.length > 1 && ctx.measureText(line + "...").width > maxWidth) line = line.slice(0, -1);
        line += "...";
      }
      ctx.fillText(line, UI.W / 2, UI.CAPTION_Y + 20);
    }
  }

  window.TonyScreen = TonyScreen;
})();
