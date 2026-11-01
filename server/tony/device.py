"""The two kinds of connection: Tony's body (the ESP32) and dashboards (browsers)."""

from __future__ import annotations

import asyncio
import json
import logging

from fastapi import WebSocket

from .protocol import AUDIO_CHUNK, CARD_CHUNK, CARD_CHUNK_SIZE, SPEAKER_AUDIO

log = logging.getLogger(__name__)


class DeviceLink:
    def __init__(self, ws: WebSocket):
        self.ws = ws
        self.camera_on = False
        self.asleep = False
        self.battery: dict | None = None
        self.firmware = ""
        self._lock = asyncio.Lock()
        self._camera_changed = asyncio.Event()
        self._snap_lock = asyncio.Lock()
        self._frame: asyncio.Future | None = None

    async def send_json(self, obj: dict) -> None:
        async with self._lock:
            await self.ws.send_text(json.dumps(obj))

    async def send_binary(self, kind: int, payload: bytes) -> None:
        async with self._lock:
            await self.ws.send_bytes(bytes([kind]) + payload)

    async def send_audio(self, pcm: bytes) -> None:
        for i in range(0, len(pcm), AUDIO_CHUNK):
            await self.send_binary(SPEAKER_AUDIO, pcm[i : i + AUDIO_CHUNK])

    async def send_card(self, jpeg: bytes, seconds: int) -> None:
        async with self._lock:  # the header and every chunk together, so audio can't land in between
            await self.ws.send_text(json.dumps({"type": "card", "bytes": len(jpeg), "seconds": seconds}))
            for i in range(0, len(jpeg), CARD_CHUNK_SIZE):
                await self.ws.send_bytes(bytes([CARD_CHUNK]) + jpeg[i : i + CARD_CHUNK_SIZE])

    # Camera

    def camera_reported(self, on: bool) -> None:
        self.camera_on = on
        self._camera_changed.set()

    async def set_camera(self, on: bool, timeout: float = 4.0) -> bool:
        if self.camera_on == on:
            return on
        self._camera_changed.clear()
        await self.send_json({"type": "camera", "on": on})
        try:
            await asyncio.wait_for(self._camera_changed.wait(), timeout)
        except TimeoutError:
            log.warning("The device didn't confirm the camera change")
        return self.camera_on

    async def snapshot(self, timeout: float = 3.0) -> bytes | None:
        if not self.camera_on:
            return None
        async with self._snap_lock:
            self._frame = asyncio.get_running_loop().create_future()
            try:
                await self.send_json({"type": "snap"})
                return await asyncio.wait_for(self._frame, timeout)
            except TimeoutError:
                log.warning("No camera frame came back")
                return None
            finally:
                self._frame = None

    def got_frame(self, jpeg: bytes | None) -> None:
        if self._frame is not None and not self._frame.done():
            self._frame.set_result(jpeg)

    async def close(self) -> None:
        try:
            await self.ws.close()
        except Exception:
            pass


class DashboardLink:
    def __init__(self, ws: WebSocket):
        self.ws = ws
        self.hear = False  # play Tony's voice in the browser too
        self._lock = asyncio.Lock()

    async def send_json(self, obj: dict) -> None:
        async with self._lock:
            await self.ws.send_text(json.dumps(obj))

    async def send_bytes(self, data: bytes) -> None:
        async with self._lock:
            await self.ws.send_bytes(data)
