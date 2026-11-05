from tony.usb_bridge import BEAT, BINARY, TEXT, FrameReader, frame


def test_frames_and_log_lines_come_apart():
    data = b"camera: on\r\n" + frame(TEXT, b'{"type":"hello"}') + b"audio: ok\n" + frame(BINARY, b"\x01pcm")
    assert FrameReader().feed(data) == [
        ("log", "camera: on"),
        ("frame", TEXT, b'{"type":"hello"}'),
        ("log", "audio: ok"),
        ("frame", BINARY, b"\x01pcm"),
    ]


def test_a_frame_split_across_reads_waits_for_the_rest():
    reader = FrameReader()
    data = frame(BEAT, b"")
    assert reader.feed(data[:4]) == []
    assert reader.feed(data[4:]) == [("frame", BEAT, b"")]


def test_a_damaged_frame_is_dropped_and_the_next_one_still_arrives():
    bad = bytearray(frame(TEXT, b"hello"))
    bad[-1] ^= 0xFF
    events = FrameReader().feed(bytes(bad) + frame(TEXT, b"ok"))
    assert events[-1] == ("frame", TEXT, b"ok")
    assert all(e[0] == "log" for e in events[:-1])
