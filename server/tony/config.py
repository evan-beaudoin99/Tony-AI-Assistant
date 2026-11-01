"""Settings: config.yaml for how Tony behaves, .env for the keys."""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass, field, fields, is_dataclass
from pathlib import Path

import yaml
from dotenv import load_dotenv

log = logging.getLogger(__name__)

SERVER_DIR = Path(__file__).resolve().parent.parent


@dataclass
class ServerSettings:
    host: str = "0.0.0.0"
    port: int = 8765
    dashboard_lan_access: bool = False


@dataclass
class YouSettings:
    name: str = ""
    home: str = "New York, NY"
    units: str = "imperial"


@dataclass
class ClaudeSettings:
    model: str = "claude-haiku-4-5"
    max_tokens: int = 350
    memory_turns: int = 10
    forget_after_minutes: float = 10
    web_search: bool = False


@dataclass
class WakeSettings:
    words: list[str] = field(default_factory=lambda: ["tony", "toni", "tonny", "tonie"])
    follow_up_seconds: float = 8
    listen_after_tap_seconds: float = 8


@dataclass
class ListeningSettings:
    vad_aggressiveness: int = 2
    end_silence_ms: int = 700
    min_speech_ms: int = 300
    max_utterance_seconds: float = 15


@dataclass
class SttSettings:
    provider: str = "local"
    local_model: str = "base.en"
    local_device: str = "cpu"
    openai_model: str = "gpt-4o-mini-transcribe"


@dataclass
class TtsSettings:
    provider: str = "auto"
    openai_model: str = "gpt-4o-mini-tts"
    openai_voice: str = "ash"
    openai_instructions: str = ""
    elevenlabs_voice_id: str = ""
    elevenlabs_model: str = "eleven_flash_v2_5"
    # Delivery knobs. Blank (None) keeps whatever the voice is saved with in ElevenLabs.
    elevenlabs_stability: float | None = None
    elevenlabs_similarity: float | None = None
    elevenlabs_style: float | None = None
    elevenlabs_speed: float | None = None
    edge_voice: str = "en-US-GuyNeural"
    edge_rate: str = "+8%"
    volume: int = 70


@dataclass
class CameraSettings:
    attach_when_on: bool = True


@dataclass
class Secrets:
    anthropic_api_key: str = ""
    openai_api_key: str = ""
    elevenlabs_api_key: str = ""
    device_token: str = ""


@dataclass
class Settings:
    server: ServerSettings = field(default_factory=ServerSettings)
    you: YouSettings = field(default_factory=YouSettings)
    claude: ClaudeSettings = field(default_factory=ClaudeSettings)
    wake: WakeSettings = field(default_factory=WakeSettings)
    listening: ListeningSettings = field(default_factory=ListeningSettings)
    stt: SttSettings = field(default_factory=SttSettings)
    tts: TtsSettings = field(default_factory=TtsSettings)
    camera: CameraSettings = field(default_factory=CameraSettings)
    secrets: Secrets = field(default_factory=Secrets)


def _fill(target, values: dict, where: str) -> None:
    known = {f.name: f for f in fields(target)}
    for key, value in (values or {}).items():
        if key not in known:
            log.warning("config.yaml: ignoring unknown setting %s.%s", where, key)
            continue
        current = getattr(target, key)
        if is_dataclass(current):
            _fill(current, value, f"{where}.{key}")
        else:
            setattr(target, key, value)


def load_settings(config_path: Path | None = None, env_path: Path | None = None) -> Settings:
    load_dotenv(env_path or SERVER_DIR / ".env")
    settings = Settings()
    path = config_path or SERVER_DIR / "config.yaml"
    if path.exists():
        with path.open(encoding="utf-8") as f:
            raw = yaml.safe_load(f) or {}
        for section, values in raw.items():
            if not hasattr(settings, section) or section == "secrets":
                log.warning("config.yaml: ignoring unknown section %s", section)
                continue
            _fill(getattr(settings, section), values, section)
    settings.secrets = Secrets(
        anthropic_api_key=os.getenv("ANTHROPIC_API_KEY", ""),
        openai_api_key=os.getenv("OPENAI_API_KEY", ""),
        elevenlabs_api_key=os.getenv("ELEVENLABS_API_KEY", ""),
        device_token=os.getenv("DEVICE_TOKEN", ""),
    )
    return settings
