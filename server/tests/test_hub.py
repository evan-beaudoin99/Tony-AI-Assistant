"""A whole conversation through Tony, with a fake brain, voice and body."""

import asyncio

import pytest

from tony.config import Settings
from tony.hub import Tony


class FakeVoice:
    name = "fake"

    def __init__(self):
        self.said = []

    async def stream(self, text):
        self.said.append(text)
        yield b"\x01\x00" * 2400  # 100 ms
        yield b"\x01\x00" * 2400


class FakeBody:
    def __init__(self):
        self.camera_on = False
        self.asleep = False
        self.battery = None
        self.firmware = "test"
        self.json = []
        self.audio = 0
        self.cards = []

    async def send_json(self, obj):
        self.json.append(obj)

    async def send_audio(self, pcm):
        self.audio += len(pcm)

    async def send_card(self, jpeg, seconds):
        self.cards.append(seconds)

    async def close(self):
        pass


def make_tony():
    s = Settings()
    s.secrets.anthropic_api_key = "test"
    tony = Tony(s)
    tony.voice = FakeVoice()
    return tony


async def fake_respond(text, image=None, **kw):
    yield ("mood", "happy")
    yield ("say", "Ay, it's sunny.")
    yield ("say", "Go get a slice.")


@pytest.mark.asyncio
async def test_typed_question_reaches_the_body():
    tony = make_tony()
    body = FakeBody()
    tony.device = body
    tony.brain.respond = fake_respond

    await tony.typed("Tony, what's the weather?")
    await tony.turn

    types = [m["type"] for m in body.json]
    states = [m["state"] for m in body.json if m["type"] == "state"]
    assert states[:2] == ["thinking", "speaking"]
    assert {"type": "emotion", "name": "happy"} in body.json
    assert types.index("audio_start") < types.index("audio_end")
    assert body.audio == 4 * 4800
    assert tony.voice.said == ["Ay, it's sunny.", "Go get a slice."]
    assert [e["who"] for e in tony.history] == ["you", "tony"]

    await tony.on_device_message(body, {"type": "playback_done"})
    assert tony.state == "listening"  # the follow-up window


@pytest.mark.asyncio
async def test_tap_cuts_tony_off():
    tony = make_tony()
    body = FakeBody()
    tony.device = body

    async def slow(text, image=None, **kw):
        yield ("say", "Lemme tell ya a long story.")
        await asyncio.sleep(10)
        yield ("say", "never said")

    tony.brain.respond = slow
    await tony.typed("Tony, tell me a story")
    await asyncio.sleep(0.2)
    await tony.on_device_message(body, {"type": "button", "event": "tap"})
    await asyncio.sleep(0)
    assert not tony.busy
    assert {"type": "audio_stop"} in body.json
    assert tony.state == "listening"
    assert "never said" not in tony.voice.said


@pytest.mark.asyncio
async def test_brain_failure_still_answers():
    tony = make_tony()
    body = FakeBody()
    tony.device = body

    async def broken(text, image=None, **kw):
        raise RuntimeError("no internet")
        yield  # pragma: no cover

    tony.brain.respond = broken
    await tony.typed("Tony?")
    await tony.turn
    assert len(tony.voice.said) == 1  # one of the "my brain hit a pothole" lines
    assert {"type": "emotion", "name": "confused"} in body.json
