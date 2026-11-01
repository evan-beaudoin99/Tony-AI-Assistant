import numpy as np

from tony.listener import FRAME_BYTES, EnergyVad, Utterances


def tone(seconds, amp=8000, freq=220):
    t = np.arange(int(16000 * seconds)) / 16000
    return (np.sin(2 * np.pi * freq * t) * amp).astype("<i2").tobytes()


def silence(seconds):
    return np.zeros(int(16000 * seconds), "<i2").tobytes()


def feed_in_chunks(ears, pcm, chunk=1280):
    events = []
    for i in range(0, len(pcm), chunk):
        events += ears.feed(pcm[i : i + chunk])
    return events


def test_one_utterance_with_preroll():
    ears = Utterances(EnergyVad(), end_silence_ms=600)
    events = feed_in_chunks(ears, silence(1) + tone(1.2) + silence(1))
    kinds = [k for k, _ in events]
    assert kinds == ["start", "end"]
    audio = events[1][1]
    assert 1.2 <= len(audio) / 32000 <= 1.9  # the speech, its pre-roll, and a little tail


def test_blip_is_too_short():
    ears = Utterances(EnergyVad(), end_silence_ms=300, min_speech_ms=400)
    events = feed_in_chunks(ears, silence(0.5) + tone(0.2) + silence(1))
    assert ("end", b"") in events or events == []


def test_long_talk_is_cut_at_the_limit():
    ears = Utterances(EnergyVad(), max_seconds=2)
    events = feed_in_chunks(ears, silence(0.5) + tone(5))
    ends = [a for k, a in events if k == "end"]
    assert ends and len(ends[0]) <= 2 * 32000 + FRAME_BYTES
