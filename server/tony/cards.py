"""Cards: full-screen pictures for Tony's 320x240 display, drawn here and sent as JPEGs.

Everything is drawn at 2x and shrunk at the end, which smooths the edges for free.
"""

from __future__ import annotations

import io
import math
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .weather import Forecast, condition

W, H = 320, 240
S = 2

_FONT_DIRS = [
    Path("C:/Windows/Fonts"),
    Path("/usr/share/fonts/truetype/dejavu"),
    Path("/usr/share/fonts/truetype/liberation"),
    Path("/Library/Fonts"),
    Path("/System/Library/Fonts/Supplemental"),
]
_BOLD = ["segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf", "Arial Bold.ttf"]
_REGULAR = ["segoeui.ttf", "arial.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf", "Arial.ttf"]

_THEMES = {
    "clear-day": ((40, 120, 220), (120, 186, 245)),
    "clear-night": ((10, 18, 48), (38, 54, 104)),
    "partly-day": ((62, 124, 196), (146, 182, 220)),
    "partly-night": ((18, 26, 56), (54, 66, 104)),
    "cloudy": ((74, 86, 104), (128, 140, 156)),
    "fog": ((92, 102, 112), (154, 162, 170)),
    "drizzle": ((54, 74, 98), (98, 120, 144)),
    "rain": ((36, 54, 80), (76, 98, 124)),
    "snow": ((100, 130, 168), (186, 202, 222)),
    "storm": ((28, 24, 46), (68, 58, 94)),
}

WHITE = (255, 255, 255, 255)
SOFT = (255, 255, 255, 200)
SUN = (255, 196, 54, 255)
MOON = (240, 234, 214, 255)
CLOUD = (244, 247, 251, 255)
DARK_CLOUD = (150, 160, 176, 255)
RAIN = (120, 196, 255, 255)
BOLT = (255, 214, 64, 255)
TEMP_LINE = (255, 213, 74, 255)


@lru_cache(maxsize=None)
def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    px = size * S
    for name in _BOLD if bold else _REGULAR:
        for folder in _FONT_DIRS:
            path = folder / name
            if path.exists():
                return ImageFont.truetype(str(path), px)
    return ImageFont.load_default(size=px)


def _gradient(top: tuple, bottom: tuple) -> Image.Image:
    t = np.linspace(0, 1, H * S)[:, None, None]
    rows = (np.array(top) * (1 - t) + np.array(bottom) * t).astype(np.uint8)
    rgb = np.repeat(rows, W * S, axis=1)
    return Image.fromarray(rgb, "RGB").convert("RGBA")


def _text(d: ImageDraw.ImageDraw, xy, text, fnt, fill=WHITE, anchor="la", shadow=True):
    x, y = xy[0] * S, xy[1] * S
    if shadow:
        d.text((x + S, y + S), text, font=fnt, fill=(0, 0, 0, 80), anchor=anchor)
    d.text((x, y), text, font=fnt, fill=fill, anchor=anchor)


def _finish(img: Image.Image) -> bytes:
    out = img.convert("RGB").resize((W, H), Image.LANCZOS)
    buf = io.BytesIO()
    out.save(buf, "JPEG", quality=88, subsampling=0)  # baseline: what the ESP32's decoder reads
    return buf.getvalue()


# ── icons (centre and size in 1x pixels) ───────────────────────────────────


def _sun(d, cx, cy, r):
    for i in range(8):
        a = i * math.pi / 4
        d.line(
            [(cx + math.cos(a) * r * 1.35, cy + math.sin(a) * r * 1.35),
             (cx + math.cos(a) * r * 1.75, cy + math.sin(a) * r * 1.75)],
            fill=SUN, width=max(2, int(r * 0.2)),
        )
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=SUN)


def _moon(img, cx, cy, r):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ld.ellipse([cx - r, cy - r, cx + r, cy + r], fill=MOON)
    ox, oy, orr = cx + r * 0.5, cy - r * 0.35, r * 0.85
    ld.ellipse([ox - orr, oy - orr, ox + orr, oy + orr], fill=(0, 0, 0, 0))  # carve the crescent
    img.alpha_composite(layer)


def _cloud(d, cx, cy, w, color):
    h = w * 0.42
    d.rounded_rectangle([cx - w / 2, cy, cx + w / 2, cy + h * 0.55], radius=h * 0.275, fill=color)
    for px, py, pr in ((-0.2, 0.05, 0.42), (0.08, -0.12, 0.58), (0.3, 0.1, 0.35)):
        x, y, r = cx + w * px, cy + h * py, h * pr
        d.ellipse([x - r, y - r, x + r, y + r], fill=color)


def draw_icon(img: Image.Image, kind: str, is_day: bool, cx: float, cy: float, size: float) -> None:
    cx, cy, size = cx * S, cy * S, size * S
    d = ImageDraw.Draw(img)
    if kind == "clear":
        if is_day:
            _sun(d, cx, cy, size * 0.24)
        else:
            _moon(img, cx, cy, size * 0.3)
        return
    if kind == "partly":
        if is_day:
            _sun(d, cx + size * 0.14, cy - size * 0.14, size * 0.17)
        else:
            _moon(img, cx + size * 0.14, cy - size * 0.14, size * 0.22)
        d = ImageDraw.Draw(img)
        _cloud(d, cx - size * 0.06, cy + size * 0.02, size * 0.72, CLOUD)
        return

    storm = kind == "storm"
    if kind in ("cloudy", "storm"):
        _cloud(d, cx + size * 0.14, cy - size * 0.16, size * 0.5, DARK_CLOUD)
    _cloud(d, cx - size * 0.03, cy - size * 0.08, size * 0.78, DARK_CLOUD if storm else CLOUD)
    below = cy + size * 0.22
    if kind in ("rain", "drizzle"):
        count, length = (4, 0.16) if kind == "rain" else (3, 0.08)
        for i in range(count):
            x = cx - size * 0.22 + i * size * 0.15
            d.line([(x, below), (x - size * 0.05, below + size * length)], fill=RAIN, width=max(2, int(size * 0.045)))
    elif kind == "snow":
        for i in range(3):
            x, y, r = cx - size * 0.18 + i * size * 0.18, below + size * 0.08 * (1 + i % 2), size * 0.05
            for a in range(3):
                ang = a * math.pi / 3
                d.line([(x - math.cos(ang) * r, y - math.sin(ang) * r), (x + math.cos(ang) * r, y + math.sin(ang) * r)],
                       fill=CLOUD, width=max(2, int(size * 0.03)))
    elif kind == "fog":
        for i in range(3):
            y = below + i * size * 0.08
            d.rounded_rectangle([cx - size * 0.32 + i * size * 0.05, y, cx + size * 0.3, y + size * 0.035],
                                radius=size * 0.02, fill=CLOUD)
    elif storm:
        x, y = cx, below - size * 0.04
        d.polygon([(x + size * 0.04, y), (x - size * 0.1, y + size * 0.18), (x, y + size * 0.18),
                   (x - size * 0.06, y + size * 0.34), (x + size * 0.12, y + size * 0.12), (x + size * 0.02, y + size * 0.12)],
                  fill=BOLT)


# ── weather ─────────────────────────────────────────────────────────────────


def _theme(kind: str, is_day: bool) -> tuple:
    key = f"{kind}-{'day' if is_day else 'night'}" if kind in ("clear", "partly") else kind
    return _THEMES.get(key, _THEMES["cloudy"])


def _smooth(points: list[tuple[float, float]], steps: int = 8) -> list[tuple[float, float]]:
    """Catmull-Rom through the points, so the temperature line curves instead of zig-zagging."""
    if len(points) < 3:
        return points
    pts = [points[0], *points, points[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for s in range(steps):
            t = s / steps
            t2, t3 = t * t, t * t * t
            out.append(tuple(
                0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                       + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3)
                for k in range(2)
            ))
    out.append(points[-1])
    return out


def _hour_label(i: int, t) -> str:
    return "Now" if i == 0 else t.strftime("%I%p").lstrip("0")


def weather_card(fc: Forecast) -> bytes:
    label, kind = condition(fc.code)
    img = _gradient(*_theme(kind, fc.is_day))
    d = ImageDraw.Draw(img)
    deg = "°"

    # Header: where and when.
    _text(d, (12, 8), fc.place.label[:30], font(14, True))
    _text(d, (308, 9), fc.time.strftime("%a %I:%M %p").replace(" 0", " "), font(12), SOFT, anchor="ra")

    # Now: icon, big temperature, conditions; the details down the right side.
    draw_icon(img, kind, fc.is_day, 56, 70, 84)
    d = ImageDraw.Draw(img)
    _text(d, (104, 88), f"{fc.temp:.0f}{deg}", font(50, True), anchor="ls")
    _text(d, (106, 96), label, font(14), anchor="la")
    today = fc.days[0] if fc.days else None
    details = [
        f"H {today.high:.0f}{deg}  L {today.low:.0f}{deg}" if today else "",
        f"Feels {fc.feels:.0f}{deg}",
        f"Wind {fc.wind:.0f} {fc.wind_unit}",
        f"Humidity {fc.humidity}%",
    ]
    for i, line in enumerate(details):
        _text(d, (308, 40 + i * 17), line, font(12, i == 0), WHITE if i == 0 else SOFT, anchor="rm")

    # The next 12 hours: rain chance as bars, temperature as a curve.
    panel = Image.new("RGBA", img.size, (0, 0, 0, 0))
    pd = ImageDraw.Draw(panel)
    pd.rounded_rectangle([8 * S, 114 * S, 312 * S, 200 * S], radius=10 * S, fill=(255, 255, 255, 34))
    hours = fc.hours
    if hours:
        x0, x1 = 24, 296
        step = (x1 - x0) / max(1, len(hours) - 1)
        xs = [x0 + i * step for i in range(len(hours))]
        base = 184
        for x, h in zip(xs, hours):
            if h.rain_chance > 0:
                top = base - 44 * h.rain_chance / 100
                pd.rounded_rectangle([(x - 5) * S, top * S, (x + 5) * S, base * S], radius=2 * S,
                                     fill=(110, 190, 255, 120))
        temps = [h.temp for h in hours]
        lo, hi = min(temps), max(temps)
        span = max(hi - lo, 4)
        mid = (hi + lo) / 2
        ys = [168 - (t - (mid - span / 2)) / span * 36 for t in temps]
        curve = [(x * S, y * S) for x, y in _smooth(list(zip(xs, ys)))]
        pd.polygon([*curve, (xs[-1] * S, base * S), (xs[0] * S, base * S)], fill=(255, 213, 74, 46))
        img.alpha_composite(panel)
        d = ImageDraw.Draw(img)
        d.line(curve, fill=TEMP_LINE, width=3 * S, joint="curve")
        for i, (x, y, h) in enumerate(zip(xs, ys, hours)):
            if i % 3:
                continue
            d.ellipse([(x - 3) * S, (y - 3) * S, (x + 3) * S, (y + 3) * S], fill=WHITE)
            _text(d, (x, y - 6), f"{h.temp:.0f}{deg}", font(11, True), anchor="ms")
            _text(d, (x, 193), _hour_label(i, h.time), font(10), SOFT, anchor="mm", shadow=False)
    else:
        img.alpha_composite(panel)
        d = ImageDraw.Draw(img)

    # Today and the next two days.
    for i, day in enumerate(fc.days[:3]):
        x = 12 + i * 102
        name = "Today" if i == 0 else day.date.strftime("%a")
        draw_icon(img, condition(day.code)[1], True, x + 13, 220, 26)
        d = ImageDraw.Draw(img)
        _text(d, (x + 30, 213), name, font(11, True), anchor="lm")
        rain = f"  {day.rain_chance}%" if day.rain_chance >= 30 else ""
        _text(d, (x + 30, 228), f"{day.high:.0f}{deg} / {day.low:.0f}{deg}{rain}", font(11), SOFT, anchor="lm")
        if i:
            d.line([(x - 6) * S, 208 * S, (x - 6) * S, 234 * S], fill=(255, 255, 255, 60), width=S)
    return _finish(img)


# ── timers ──────────────────────────────────────────────────────────────────


def _duration(seconds: int) -> str:
    h, rest = divmod(int(seconds), 3600)
    m, s = divmod(rest, 60)
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"


def timer_card(label: str, seconds: int, done: bool = False) -> bytes:
    img = _gradient((30, 26, 40), (8, 8, 14))
    d = ImageDraw.Draw(img)
    cx, cy, r = 160 * S, 104 * S, 72 * S
    ring = (255, 70, 70, 255) if done else (255, 170, 30, 255)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 255, 255, 40), width=10 * S)
    d.arc([cx - r, cy - r, cx + r, cy + r], start=-90, end=270, fill=ring, width=10 * S)
    if done:
        _text(d, (160, 100), "TIME'S", font(28, True), anchor="ms")
        _text(d, (160, 132), "UP!", font(34, True), ring, anchor="ms")
    else:
        _text(d, (160, 104), _duration(seconds), font(40 if seconds < 3600 else 30, True), anchor="mm")
    _text(d, (160, 200), (label or "Timer")[:28], font(18, True), anchor="mm")
    _text(d, (160, 223), "Ay, get over here!" if done else "Tony's on it.", font(12), SOFT, anchor="mm")
    return _finish(img)
