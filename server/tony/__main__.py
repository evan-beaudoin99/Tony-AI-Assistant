"""python -m tony"""

from __future__ import annotations

import logging
import os
import socket
import warnings

import uvicorn

from .config import load_settings
from .web import create_app


def lan_ip() -> str:
    """This computer's address on the local network (what the ESP32 should connect to)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("10.255.255.255", 1))  # no packet is sent; this just picks the outgoing interface
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def port_free(host: str, port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind((host, port))
            return True
        except OSError:
            return False


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
                        datefmt="%H:%M:%S")
    for noisy in ("httpx", "httpx2", "httpcore", "anthropic", "openai", "faster_whisper", "huggingface_hub"):
        logging.getLogger(noisy).setLevel(logging.WARNING)
    os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
    warnings.filterwarnings("ignore", module="huggingface_hub")

    settings = load_settings()
    if not settings.secrets.anthropic_api_key:
        raise SystemExit("Tony needs a brain: put ANTHROPIC_API_KEY in server/.env (copy .env.example to start).")
    if not settings.secrets.device_token:
        logging.warning("DEVICE_TOKEN isn't set in .env, so any device on your network can connect as Tony.")

    port = settings.server.port
    if not port_free(settings.server.host, port):
        raise SystemExit(f"Port {port} is already in use. Is Tony already running in another terminal? "
                         f"Close that one, or change server.port in config.yaml.")
    print(f"""
  ┌──────────────────────────────────────────────────────────┐
    Ay! Tony's server is up.
    Dashboard:      http://localhost:{port}
    For secrets.h:  TONY_SERVER_HOST "{lan_ip()}"   TONY_SERVER_PORT {port}
  └──────────────────────────────────────────────────────────┘
""", flush=True)
    uvicorn.run(create_app(settings), host=settings.server.host, port=port, log_level="warning")


if __name__ == "__main__":
    main()
