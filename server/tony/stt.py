"""Speech to text: faster-whisper on this computer (free), or OpenAI's transcription API."""

from __future__ import annotations

import asyncio
import io
import logging
import re
import threading
import wave

import numpy as np

from .config import Settings
from .protocol import MIC_RATE

log = logging.getLogger(__name__)

# Whisper "hears" these in silence and noise. On their own they're never a real request.
_HALLUCINATIONS = {
    "you",
    "thank you",
    "thanks",
    "thanks for watching",
    "thank you for watching",
    "bye",
    "okay",
    "so",
    "uh",
    "um",
}

PROMPT = "Hey Tony."  # nudges Whisper to spell his name right


def is_noise(text: str) -> bool:
    words = re.sub(r"[^a-z' ]", "", text.lower()).strip()
    return len(words) < 2 or words in _HALLUCINATIONS


def pcm_to_wav(pcm: bytes, rate: int = MIC_RATE) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return buf.getvalue()


class LocalWhisper:
    def __init__(self, model: str, device: str):
        self.model_name = model
        self.device = device
        self.name = f"faster-whisper {model}"
        self._model = None
        self._loading = threading.Lock()

    def warm_up(self) -> None:
        with self._loading:
            if self._model is None:
                from faster_whisper import WhisperModel

                log.info("Loading Whisper %s on %s (the first run downloads it)...", self.model_name, self.device)
                compute = "int8" if self.device == "cpu" else "float16"
                self._model = WhisperModel(self.model_name, device=self.device, compute_type=compute)
                log.info("Whisper is ready.")

    async def transcribe(self, pcm: bytes) -> str:
        return await asyncio.to_thread(self._run, pcm)

    def _run(self, pcm: bytes) -> str:
        self.warm_up()
        audio = np.frombuffer(pcm, np.int16).astype(np.float32) / 32768.0
        segments, _ = self._model.transcribe(
            audio,
            language="en",
            beam_size=1,
            initial_prompt=PROMPT,
            condition_on_previous_text=False,
            without_timestamps=True,
            vad_filter=True,
        )
        return " ".join(s.text.strip() for s in segments if s.no_speech_prob < 0.6).strip()


class OpenAITranscriber:
    def __init__(self, api_key: str, model: str):
        from openai import AsyncOpenAI

        self.client = AsyncOpenAI(api_key=api_key)
        self.model = model
        self.name = f"OpenAI {model}"

    def warm_up(self) -> None:
        pass

    async def transcribe(self, pcm: bytes) -> str:
        result = await self.client.audio.transcriptions.create(
            model=self.model,
            file=("speech.wav", pcm_to_wav(pcm), "audio/wav"),
            language="en",
            prompt=PROMPT,
        )
        return result.text.strip()


def make_transcriber(settings: Settings):
    stt = settings.stt
    if stt.provider == "openai":
        if not settings.secrets.openai_api_key:
            raise SystemExit("stt.provider is openai, but OPENAI_API_KEY isn't set in .env")
        return OpenAITranscriber(settings.secrets.openai_api_key, stt.openai_model)
    return LocalWhisper(stt.local_model, stt.local_device)
