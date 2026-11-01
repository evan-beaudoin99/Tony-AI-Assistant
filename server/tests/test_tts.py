import pytest

from tony.tts import EdgeVoice


class FlakyEdge(EdgeVoice):
    RETRY_DELAYS = (0, 0, 0)

    def __init__(self, failures: int):
        super().__init__("en-US-GuyNeural", "+0%")
        self.failures = failures
        self.calls = 0

    async def _mp3(self, text: str) -> bytes:
        self.calls += 1
        if self.calls <= self.failures:
            raise ConnectionError("no audio")
        return b""


async def collect(voice: EdgeVoice) -> list[bytes]:
    return [chunk async for chunk in voice.stream("Ay, how ya doin'?")]


@pytest.mark.asyncio
async def test_edge_retries_a_hiccup():
    voice = FlakyEdge(failures=3)
    assert await collect(voice) == []
    assert voice.calls == 4


@pytest.mark.asyncio
async def test_edge_gives_up_after_four_tries():
    voice = FlakyEdge(failures=9)
    with pytest.raises(RuntimeError, match="4 times"):
        await collect(voice)
    assert voice.calls == 4
