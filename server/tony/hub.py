"""Tony himself: where the ears, brain, voice, body and dashboards meet.

A conversation goes:  mic audio -> utterance -> transcript -> (wake word?) -> Claude -> sentences -> voice -> speaker.
His state, shown on his face:
    idle       waiting for "Tony"
    listening  a button tap or a follow-up window: no wake word needed
    thinking   transcript in, waiting on Claude
    speaking   audio's going out
"""

from __future__ import annotations

import asyncio
import base64
import contextlib
import logging
import re
import time
from collections import deque
from datetime import datetime

from . import tts
from .brain import Brain
from .config import Settings
from .device import DashboardLink, DeviceLink
from .listener import Utterances, make_vad
from .personality import GREETINGS, OOPS, pick
from .protocol import SPEAKER_RATE
from .speech import SpeechOut
from .stt import is_noise, make_transcriber
from .tools import Toolbox

log = logging.getLogger(__name__)

_PLAIN = str.maketrans({"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2014": "-", "\u2013": "-",
                        "\u2026": "...", "\u00b0": " deg"})


def mentions(text: str, words: list[str]) -> bool:
    pattern = r"\b(" + "|".join(re.escape(w) for w in words) + r")\b"
    return re.search(pattern, text, re.IGNORECASE) is not None


class Tony:
    def __init__(self, settings: Settings):
        self.s = settings
        self.device: DeviceLink | None = None
        self.dashboards: set[DashboardLink] = set()
        self.voice = tts.make_voice(settings)
        self.stt = make_transcriber(settings)
        lc = settings.listening
        self.ears = Utterances(make_vad(lc.vad_aggressiveness), lc.end_silence_ms, lc.min_speech_ms,
                               lc.max_utterance_seconds)
        self.toolbox = Toolbox(self)
        self.brain = Brain(settings, self.toolbox.definitions(), self.toolbox.run)

        self.state = "idle"
        self.turn: asyncio.Task | None = None
        self.listen_until = 0.0  # until then, no wake word needed
        self.sleep_after_reply = False
        self.last_card: bytes | None = None
        self.last_frame: bytes | None = None
        self.history: deque[dict] = deque(maxlen=200)
        self._greeted = False
        self._idle_timer: asyncio.Task | None = None
        self._watchdog: asyncio.Task | None = None
        self._background: set[asyncio.Task] = set()

    # ── lifecycle ───────────────────────────────────────────────────────────

    async def startup(self) -> None:
        log.info("Brain: %s | Ears: %s | Voice: %s", self.s.claude.model, self.stt.name, self.voice.name)
        self._spawn(asyncio.to_thread(self.stt.warm_up))

    async def shutdown(self) -> None:
        self.toolbox.timers.cancel_all()
        if self.turn:
            self.turn.cancel()

    def _spawn(self, coro) -> asyncio.Task:
        task = asyncio.create_task(coro)
        self._background.add(task)
        task.add_done_callback(self._background.discard)
        return task

    @property
    def busy(self) -> bool:
        return self.turn is not None and not self.turn.done()

    @property
    def camera_on(self) -> bool:
        return bool(self.device and self.device.camera_on)

    # ── talking to the body and the dashboards ──────────────────────────────

    async def _to_device(self, obj: dict) -> None:
        if self.device:
            try:
                await self.device.send_json(obj)
            except Exception as e:
                log.debug("device send failed: %s", e)

    async def broadcast(self, obj: dict) -> None:
        for dash in list(self.dashboards):
            try:
                await dash.send_json(obj)
            except Exception:
                self.dashboards.discard(dash)

    def status(self) -> dict:
        dev = self.device
        return {
            "type": "status",
            "device": dev is not None,
            "camera": bool(dev and dev.camera_on),
            "asleep": bool(dev and dev.asleep),
            "battery": dev.battery if dev else None,
            "firmware": dev.firmware if dev else "",
            "state": self.state,
            "model": self.s.claude.model,
            "voice": self.voice.name,
            "stt": self.stt.name,
            "volume": self.s.tts.volume,
        }

    async def broadcast_status(self) -> None:
        await self.broadcast(self.status())

    async def note(self, who: str, text: str) -> None:
        entry = {"type": "log", "who": who, "text": text, "time": datetime.now().strftime("%H:%M")}
        self.history.append(entry)
        log.info("%s: %s", who, text)
        await self.broadcast(entry)

    async def set_state(self, state: str) -> None:
        self.state = state
        await self._to_device({"type": "state", "state": state})
        await self.broadcast_status()

    async def caption(self, text: str, ms: int = 3000) -> None:
        """A line under Tony's face. The device font is plain ASCII, so smart punctuation gets flattened."""
        flat = text.translate(_PLAIN).encode("ascii", "ignore").decode()
        await self._to_device({"type": "caption", "text": flat, "ms": ms})
        await self.broadcast({"type": "caption", "text": flat, "ms": ms})

    async def emotion(self, name: str) -> None:
        await self._to_device({"type": "emotion", "name": name})
        await self.broadcast({"type": "emotion", "name": name})

    async def show_card(self, jpeg: bytes, seconds: int = 15) -> None:
        self.last_card = jpeg
        if self.device:
            try:
                await self.device.send_card(jpeg, seconds)
            except Exception as e:
                log.debug("card send failed: %s", e)
        await self.broadcast({"type": "card", "jpeg": base64.b64encode(jpeg).decode(), "seconds": seconds})

    async def audio_start(self) -> None:
        await self._to_device({"type": "audio_start", "rate": SPEAKER_RATE})
        await self.set_state("speaking")
        await self._to_hearers(json={"type": "audio_start", "rate": SPEAKER_RATE})

    async def audio_chunk(self, pcm: bytes) -> None:
        if self.device:
            try:
                await self.device.send_audio(pcm)
            except Exception as e:
                log.debug("audio send failed: %s", e)
        await self._to_hearers(data=pcm)

    async def audio_end(self) -> None:
        await self._to_device({"type": "audio_end"})
        await self._to_hearers(json={"type": "audio_end"})

    async def audio_stop(self) -> None:
        await self._to_device({"type": "audio_stop"})
        await self._to_hearers(json={"type": "audio_stop"})

    async def _to_hearers(self, json: dict | None = None, data: bytes | None = None) -> None:
        for dash in list(self.dashboards):
            if not dash.hear:
                continue
            try:
                if json is not None:
                    await dash.send_json(json)
                if data is not None:
                    await dash.send_bytes(data)
            except Exception:
                self.dashboards.discard(dash)

    async def snapshot(self) -> bytes | None:
        if not self.device:
            return None
        frame = await self.device.snapshot()
        if frame:
            self.last_frame = frame
            await self.broadcast({"type": "frame", "jpeg": base64.b64encode(frame).decode()})
        return frame

    async def set_camera(self, on: bool) -> bool | None:
        if not self.device:
            return None
        return await self.device.set_camera(on)

    async def set_sleep(self, on: bool) -> None:
        await self._to_device({"type": "sleep", "on": on})

    # ── the body ────────────────────────────────────────────────────────────

    async def attach_device(self, dev: DeviceLink) -> None:
        if self.device:
            await self.device.close()
        self.device = dev
        self.ears.reset()
        await self.note("system", "Tony's body connected.")

    async def detach_device(self, dev: DeviceLink) -> None:
        if self.device is not dev:
            return
        self.device = None
        self.ears.reset()
        self.state = "idle"
        await self.note("system", "Tony's body disconnected.")
        await self.broadcast_status()

    async def on_device_message(self, dev: DeviceLink, msg: dict) -> None:
        kind = msg.get("type")
        if kind == "hello":
            dev.firmware = str(msg.get("fw", ""))
            dev.camera_reported(bool(msg.get("camera")))
            dev.asleep = bool(msg.get("asleep"))
            dev.battery = msg.get("battery")
            await dev.send_json({"type": "volume", "value": self.s.tts.volume})
            await self.set_state("idle")
            if not self._greeted and not dev.asleep:
                self._greeted = True
                await self.announce(pick(GREETINGS), mood="happy")
        elif kind == "camera":
            on = bool(msg.get("on"))
            if on != dev.camera_on:
                await self.note("system", "Camera on." if on else "Camera off.")
            dev.camera_reported(on)
            await self.broadcast_status()
        elif kind == "sleep":
            dev.asleep = bool(msg.get("on"))
            if dev.asleep:
                if self.busy:
                    self.turn.cancel()
                self.ears.reset()
                self.state = "idle"
            await self.note("system", "Tony's asleep." if dev.asleep else "Tony's awake.")
            await self.broadcast_status()
        elif kind == "battery":
            dev.battery = {"volts": msg.get("volts"), "percent": msg.get("percent")}
            await self.broadcast_status()
        elif kind == "button" and msg.get("event") == "tap":
            await self.on_tap()
        elif kind == "playback_done":
            await self.after_speaking()
        elif kind == "snap_failed":
            log.warning("Tony couldn't take a photo: %s", msg.get("reason", "no reason given"))
            dev.got_frame(None)

    async def on_mic(self, pcm: bytes) -> None:
        if not self.device or self.device.asleep:
            return
        for kind, audio in self.ears.feed(pcm):
            if kind == "end" and audio and not self.busy:
                self._spawn(self._heard(audio))

    async def _heard(self, pcm: bytes) -> None:
        open_mic = time.monotonic() < self.listen_until
        frame_task = None
        if self.camera_on and self.s.camera.attach_when_on:
            frame_task = asyncio.create_task(self.snapshot())  # in parallel with the transcription
        try:
            text = await self.stt.transcribe(pcm)
        except Exception:
            log.exception("Transcription failed")
            text = ""
        addressed = text and not is_noise(text) and (open_mic or mentions(text, self.s.wake.words))
        if not addressed or self.busy:
            if frame_task:
                frame_task.cancel()
            if text and not is_noise(text):
                await self.note("overheard", text)
            return
        await self.note("you", text)
        self.start_turn(text, frame_task)

    async def interrupt(self) -> None:
        """Stop the current reply and wait until it has fully wound down."""
        if self.busy:
            self.turn.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.turn
        await self.audio_stop()

    async def on_tap(self) -> None:
        if self.busy:
            await self.interrupt()
        window = self.s.wake.listen_after_tap_seconds
        self.listen_until = time.monotonic() + window
        await self.set_state("listening")
        self._idle_after(window)

    # ── conversations ───────────────────────────────────────────────────────

    async def typed(self, text: str) -> None:
        await self.note("you", text)
        frame_task = asyncio.create_task(self.snapshot()) if self.camera_on and self.s.camera.attach_when_on else None
        if self.device and self.device.asleep:
            await self.set_sleep(False)
        self.start_turn(text, frame_task)

    def start_turn(self, text: str, frame_task: asyncio.Task | None = None) -> None:
        previous = self.turn if self.busy else None
        if previous:
            previous.cancel()
        self.turn = asyncio.create_task(self._turn(text, frame_task, previous))

    async def _turn(self, text: str, frame_task: asyncio.Task | None, previous: asyncio.Task | None = None) -> None:
        if previous:  # let the reply we cut off finish its cleanup first, so its "stop" can't land after our "start"
            with contextlib.suppress(asyncio.CancelledError):
                await previous
        self.listen_until = 0
        self._cancel_timers()
        await self.set_state("thinking")
        await self.emotion("thinking")
        await self.caption(f'"{text}"', ms=4000)
        frame = None
        if frame_task:
            try:
                frame = await asyncio.wait_for(frame_task, 3)
            except Exception:
                frame = None

        speech = SpeechOut(self, self.voice)
        said: list[str] = []
        try:
            async for kind, value in self.brain.respond(
                text, frame, camera_on=self.camera_on, body_connected=self.device is not None
            ):
                if kind == "mood":
                    await self.emotion(value)
                else:
                    said.append(value)
                    speech.say(value)
            await speech.finish()
        except asyncio.CancelledError:
            await speech.abort()
            if said:
                await self.note("tony", " ".join(said) + " (cut off)")
            raise
        except Exception as e:
            log.exception("Brain trouble")
            await speech.abort()
            await self.note("system", f"Claude error: {e}")
            await self.emotion("confused")
            speech = SpeechOut(self, self.voice)
            speech.say(pick(OOPS))
            await speech.finish()
        else:
            if said:
                await self.note("tony", " ".join(said))
        await self._await_playback(speech)

    async def announce(self, text: str, mood: str = "happy", card: bytes | None = None, chime: bool = False) -> None:
        """Say something on Tony's own initiative (a timer, a greeting), once he's free. Returns right away."""
        self._spawn(self._announce_when_free(text, mood, card, chime))

    async def _announce_when_free(self, text: str, mood: str, card: bytes | None, chime: bool) -> None:
        while self.busy:
            await asyncio.sleep(0.5)
        self.turn = asyncio.create_task(self._announce(text, mood, card, chime))

    async def _announce(self, text: str, mood: str, card: bytes | None, chime: bool) -> None:
        if self.device and self.device.asleep:
            await self.set_sleep(False)
        if card:
            await self.show_card(card, seconds=12)
        await self.emotion(mood)
        speech = SpeechOut(self, self.voice)
        if chime:
            speech.play(tts.chime())
        speech.say(text)
        try:
            await speech.finish()
        except asyncio.CancelledError:
            await speech.abort()
            raise
        await self.note("tony", text)
        await self._await_playback(speech)

    async def _await_playback(self, speech: SpeechOut) -> None:
        """The device says when it's done playing; without a body, or if that message never comes, guess."""
        if not speech.started:
            await self.after_speaking()
            return
        delay = speech.remaining() + (3.0 if self.device else 0.0)
        self._cancel_timers()
        self._watchdog = asyncio.create_task(self._after(delay))

    async def _after(self, delay: float) -> None:
        await asyncio.sleep(delay)
        if self.state == "speaking":
            await self.after_speaking()

    async def after_speaking(self) -> None:
        if self.busy and self.turn is not asyncio.current_task():
            return  # a newer reply is under way
        self._cancel_timers()
        if self.sleep_after_reply:
            self.sleep_after_reply = False
            await self.set_state("idle")
            await self.set_sleep(True)
            return
        window = self.s.wake.follow_up_seconds
        self.listen_until = time.monotonic() + window
        await self.set_state("listening")
        self._idle_after(window)

    def _idle_after(self, seconds: float) -> None:
        if self._idle_timer:
            self._idle_timer.cancel()
        self._idle_timer = asyncio.create_task(self._go_idle(seconds))

    async def _go_idle(self, seconds: float) -> None:
        await asyncio.sleep(seconds)
        if not self.busy and self.state == "listening":
            await self.set_state("idle")

    def _cancel_timers(self) -> None:
        for task in (self._watchdog, self._idle_timer):
            if task and task is not asyncio.current_task():
                task.cancel()
