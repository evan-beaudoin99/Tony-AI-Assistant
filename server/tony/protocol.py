"""The wire protocol between Tony's body (ESP32) and this server, over one WebSocket at /ws/device?token=...

Binary frames start with a one-byte kind:
    MIC_AUDIO      device -> server   16 kHz, 16-bit little-endian mono PCM, ~40 ms per frame
    CAMERA_FRAME   device -> server   one JPEG, sent in answer to {"type": "snap"}
    SPEAKER_AUDIO  server -> device   24 kHz, 16-bit little-endian mono PCM, at most AUDIO_CHUNK bytes
    CARD_CHUNK     server -> device   the next piece of the JPEG announced by {"type": "card"}

The device can't take frames over 15 KB, so audio and cards go out in chunks.

Text frames are JSON objects with a "type".
    device -> server
        hello          {"fw", "camera": bool, "asleep": bool, "battery"?: {"volts", "percent"}}
        button         {"event": "tap"}          talk without the wake word / cut Tony off
        camera         {"on": bool}              the camera's actual state, after any change
        sleep          {"on": bool}
        battery        {"volts", "percent"}
        playback_done  {}                        the reply finished playing out of the speaker
        snap_failed    {"reason"}
    server -> device
        state          {"state": "idle" | "listening" | "thinking" | "speaking"}
        emotion        {"name": one of EMOTIONS}
        audio_start    {"rate": 24000}  ...SPEAKER_AUDIO frames...  audio_end {}
        audio_stop     {}                        drop whatever's buffered, now
        card           {"bytes": n, "seconds": s}  ...CARD_CHUNK frames...
        face           {}                        back to the face from a card
        camera         {"on": bool}              please turn the camera on / off
        snap           {}                        please send a CAMERA_FRAME
        sleep          {"on": bool}
        look           {"x", "y", "ms"}          glance toward x, y in -1..1
        volume         {"value": 0..100}
        caption        {"text", "ms"}            a short line under the face
"""

MIC_AUDIO = 0x01
CAMERA_FRAME = 0x02
SPEAKER_AUDIO = 0x10
CARD_CHUNK = 0x11

MIC_RATE = 16000
SPEAKER_RATE = 24000
AUDIO_CHUNK = 4800  # 100 ms at 24 kHz
CARD_CHUNK_SIZE = 8192

EMOTIONS = (
    "neutral",
    "happy",
    "sad",
    "angry",
    "surprised",
    "sleepy",
    "thinking",
    "curious",
    "skeptical",
    "excited",
    "love",
    "confused",
)
