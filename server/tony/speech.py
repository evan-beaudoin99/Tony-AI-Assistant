"""One reply's worth of talking: sentences go in, paced audio comes out to Tony's speaker."""

from __future__ import annotations

import asyncio
import contextlib
import logging
from typing import TYPE_CHECKING

from .protocol import SPEAKER_RATE

if TYPE_CHECKING:
    from .hub import Tony

log = logging.getLogger(__name__)

# How far ahead of the speaker the server may get. The device buffers ~20 s; staying close keeps "stop" instant
# on the server side too.
LEAD_SECONDS = 4.0


class SpeechOut:
    def __init__(self, tony: Tony, voice):
        self.tony = tony
        self.voice = voice
        self.started = False
        self.play_end = 0.0
        self._queue: asyncio.Queue = asyncio.Queue()
        self._task = asyncio.create_task(self._run())

    def say(self, text: str) -> None:
        self._queue.put_nowait(("text", text))

    def play(self, pcm: bytes) -> None:
        self._queue.put_nowait(("pcm", pcm))

    async def finish(self) -> None:
        """Everything queued gets said; returns once the last of it is sent (not yet played)."""
        if not self._task.done():
            self._queue.put_nowait(None)
        await self._task

    async def abort(self) -> None:
        self._task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self._task
        if self.started:
            await self.tony.audio_stop()

    def remaining(self) -> float:
        """Seconds of audio still to play, roughly."""
        return max(0.0, self.play_end - asyncio.get_running_loop().time())

    async def _run(self) -> None:
        while (item := await self._queue.get()) is not None:
            kind, value = item
            if kind == "pcm":
                await self._send(value)
                continue
            try:
                async for pcm in self.voice.stream(value):
                    await self._send(pcm)
            except Exception as e:
                log.error("Couldn't say %r: %s", value, e)
                log.debug("voice failure", exc_info=True)
        if self.started:
            await self.tony.audio_end()

    async def _send(self, pcm: bytes) -> None:
        loop = asyncio.get_running_loop()
        if not self.started:
            self.started = True
            await self.tony.audio_start()
        now = loop.time()
        self.play_end = max(self.play_end, now) + len(pcm) / 2 / SPEAKER_RATE
        await self.tony.audio_chunk(pcm)
        ahead = self.play_end - loop.time()
        if ahead > LEAD_SECONDS:
            await asyncio.sleep(ahead - LEAD_SECONDS)
