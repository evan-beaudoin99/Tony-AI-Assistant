"""Tony's ears: cuts the device's endless mic stream into utterances, one per thing somebody said."""

from __future__ import annotations

import collections
import logging

import numpy as np

from .protocol import MIC_RATE

log = logging.getLogger(__name__)

FRAME_MS = 30
FRAME_BYTES = MIC_RATE * 2 * FRAME_MS // 1000
PREROLL_FRAMES = 10  # keep 300 ms from before the speech started, so the first word isn't clipped


class EnergyVad:
    """Fallback when webrtcvad isn't installed: loud compared to the room = speech."""

    def __init__(self) -> None:
        self.noise = 200.0

    def is_speech(self, frame: bytes, rate: int) -> bool:
        rms = float(np.sqrt(np.mean(np.frombuffer(frame, np.int16).astype(np.float32) ** 2)))
        speech = rms > max(self.noise * 3.0, 300.0)
        if not speech:
            self.noise = 0.95 * self.noise + 0.05 * rms
        return speech


def make_vad(aggressiveness: int):
    try:
        import webrtcvad

        return webrtcvad.Vad(max(0, min(3, aggressiveness)))
    except ImportError:
        log.warning("webrtcvad isn't installed; using a simple loudness detector instead")
        return EnergyVad()


class Utterances:
    """Feed mic audio in; get ("start", b"") when someone starts talking and ("end", pcm) when they stop."""

    def __init__(self, vad, end_silence_ms: int = 700, min_speech_ms: int = 300, max_seconds: float = 15):
        self.vad = vad
        self.end_silence_frames = max(1, end_silence_ms // FRAME_MS)
        self.min_speech_frames = max(1, min_speech_ms // FRAME_MS)
        self.max_frames = int(max_seconds * 1000 / FRAME_MS)
        self._pending = b""
        self._recent: collections.deque[bool] = collections.deque(maxlen=8)
        self._preroll: collections.deque[bytes] = collections.deque(maxlen=PREROLL_FRAMES)
        self.reset()

    def reset(self) -> None:
        self._pending = b""
        self._recent.clear()
        self._preroll.clear()
        self._frames: list[bytes] = []
        self._in_speech = False
        self._silent = 0
        self._voiced = 0

    def feed(self, pcm: bytes) -> list[tuple[str, bytes]]:
        events: list[tuple[str, bytes]] = []
        self._pending += pcm
        while len(self._pending) >= FRAME_BYTES:
            frame, self._pending = self._pending[:FRAME_BYTES], self._pending[FRAME_BYTES:]
            self._step(frame, events)
        return events

    def _step(self, frame: bytes, events: list[tuple[str, bytes]]) -> None:
        speech = self.vad.is_speech(frame, MIC_RATE)
        if not self._in_speech:
            self._recent.append(speech)
            self._preroll.append(frame)
            if sum(self._recent) >= 5:  # 5 of the last 8 frames: someone's really talking
                self._in_speech = True
                self._frames = list(self._preroll)
                self._voiced = sum(self._recent)
                self._silent = 0
                self._recent.clear()
                self._preroll.clear()
                events.append(("start", b""))
            return

        self._frames.append(frame)
        if speech:
            self._voiced += 1
            self._silent = 0
        else:
            self._silent += 1
        if self._silent >= self.end_silence_frames or len(self._frames) >= self.max_frames:
            keep = len(self._frames) - max(0, self._silent - 5)  # trim the long tail of silence
            audio = b"".join(self._frames[:keep])
            voiced = self._voiced
            self._in_speech = False
            self._frames = []
            if voiced >= self.min_speech_frames:
                events.append(("end", audio))
            else:
                events.append(("end", b""))
