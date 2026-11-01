"""Tony's voice. Every voice streams 24 kHz, 16-bit mono PCM, so the device never has to decode anything."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import AsyncIterator

import numpy as np

from .config import Settings
from .protocol import SPEAKER_RATE

log = logging.getLogger(__name__)


async def _aligned(chunks: AsyncIterator[bytes]) -> AsyncIterator[bytes]:
    """HTTP chunks can split a sample in half; keep every chunk a whole number of samples."""
    carry = b""
    async for chunk in chunks:
        data = carry + chunk
        cut = len(data) - len(data) % 2
        carry = data[cut:]
        if cut:
            yield data[:cut]


class OpenAIVoice:
    """gpt-4o-mini-tts takes acting directions, which is how Tony gets his accent."""

    def __init__(self, api_key: str, model: str, voice: str, instructions: str):
        from openai import AsyncOpenAI

        self.client = AsyncOpenAI(api_key=api_key)
        self.model = model
        self.voice = voice
        self.instructions = instructions.strip()
        self.name = f"OpenAI {model} ({voice})"

    async def stream(self, text: str) -> AsyncIterator[bytes]:
        async with self.client.audio.speech.with_streaming_response.create(
            model=self.model,
            voice=self.voice,
            input=text,
            instructions=self.instructions or None,
            response_format="pcm",
        ) as response:
            async for chunk in _aligned(response.iter_bytes(4800)):
                yield chunk


class ElevenLabsVoice:
    def __init__(self, api_key: str, voice_id: str, model: str, settings: dict | None = None):
        import httpx

        self.client = httpx.AsyncClient(timeout=30)
        self.api_key = api_key
        self.voice_id = voice_id
        self.model = model
        self.settings = settings or {}
        self.name = f"ElevenLabs {model}"

    async def stream(self, text: str) -> AsyncIterator[bytes]:
        async with self.client.stream(
            "POST",
            f"https://api.elevenlabs.io/v1/text-to-speech/{self.voice_id}/stream",
            params={"output_format": f"pcm_{SPEAKER_RATE}"},
            headers={"xi-api-key": self.api_key},
            json={"text": text, "model_id": self.model, **({"voice_settings": self.settings} if self.settings else {})},
        ) as response:
            response.raise_for_status()
            async for chunk in _aligned(response.aiter_bytes(4800)):
                yield chunk


class EdgeVoice:
    """Microsoft Edge's online voices: free, no key, no accent."""

    # The free service now and then answers with no audio; trying again a moment later almost always works.
    RETRY_DELAYS = (0.3, 0.6, 1.0)

    def __init__(self, voice: str, rate: str):
        self.voice = voice
        self.rate = rate
        self.name = f"Edge {voice}"

    async def _mp3(self, text: str) -> bytes:
        import edge_tts

        mp3 = b""
        async for chunk in edge_tts.Communicate(text, self.voice, rate=self.rate).stream():
            if chunk["type"] == "audio":
                mp3 += chunk["data"]
        return mp3

    async def stream(self, text: str) -> AsyncIterator[bytes]:
        import miniaudio

        for attempt, delay in enumerate((*self.RETRY_DELAYS, None), start=1):
            try:
                mp3 = await self._mp3(text)
                break
            except Exception as e:
                if delay is None:
                    raise RuntimeError(f"Edge voice failed {attempt} times in a row ({e})") from None
                log.info("Edge voice hiccup (%s), retrying", type(e).__name__)
                await asyncio.sleep(delay)
        if not mp3:
            return
        decoded = miniaudio.decode(
            mp3, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=SPEAKER_RATE
        )
        pcm = decoded.samples.tobytes()
        for i in range(0, len(pcm), 4800):
            yield pcm[i : i + 4800]


class FallbackVoice:
    """Tries the good voice; if it fails before saying anything, says it with the free one instead."""

    def __init__(self, primary, backup):
        self.primary = primary
        self.backup = backup
        self.name = primary.name

    async def stream(self, text: str) -> AsyncIterator[bytes]:
        started = False
        try:
            async for chunk in self.primary.stream(text):
                started = True
                yield chunk
            return
        except Exception as e:
            if started:
                raise
            log.warning("%s failed (%s); using %s for this one", self.primary.name, e, self.backup.name)
        async for chunk in self.backup.stream(text):
            yield chunk


def make_voice(settings: Settings):
    tts, secrets = settings.tts, settings.secrets
    edge = EdgeVoice(tts.edge_voice, tts.edge_rate)
    provider = tts.provider
    if provider == "auto":
        if secrets.openai_api_key:
            provider = "openai"
        elif secrets.elevenlabs_api_key and tts.elevenlabs_voice_id:
            provider = "elevenlabs"
        else:
            provider = "edge"
    if provider == "openai":
        if not secrets.openai_api_key:
            raise SystemExit("tts.provider is openai, but OPENAI_API_KEY isn't set in .env")
        voice = OpenAIVoice(secrets.openai_api_key, tts.openai_model, tts.openai_voice, tts.openai_instructions)
        return FallbackVoice(voice, edge)
    if provider == "elevenlabs":
        if not (secrets.elevenlabs_api_key and tts.elevenlabs_voice_id):
            raise SystemExit("tts.provider is elevenlabs: set ELEVENLABS_API_KEY and tts.elevenlabs_voice_id")
        knobs = {
            "stability": tts.elevenlabs_stability,
            "similarity_boost": tts.elevenlabs_similarity,
            "style": tts.elevenlabs_style,
            "speed": tts.elevenlabs_speed,
        }
        voice = ElevenLabsVoice(secrets.elevenlabs_api_key, tts.elevenlabs_voice_id, tts.elevenlabs_model,
                                {k: v for k, v in knobs.items() if v is not None})
        return FallbackVoice(voice, edge)
    return edge


def chime() -> bytes:
    """Ding-ding: the timer bell, made here so it needs no sound files."""
    parts = []
    for freq in (1318.5, 1046.5, 1318.5, 1046.5):
        t = np.arange(int(SPEAKER_RATE * 0.18)) / SPEAKER_RATE
        tone = np.sin(2 * np.pi * freq * t) * np.exp(-t * 14)
        parts.append(tone)
        parts.append(np.zeros(int(SPEAKER_RATE * 0.05)))
    wave = np.concatenate(parts) * 0.5
    return (wave * 32767).astype("<i2").tobytes()
