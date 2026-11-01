from tony.brain import image_block, trim_history, without_old_images
from tony.hub import mentions
from tony.personality import system_prompt
from tony.stt import is_noise


def user(text):
    return {"role": "user", "content": [{"type": "text", "text": text}]}


def assistant(text):
    return {"role": "assistant", "content": [{"type": "text", "text": text}]}


TOOL_USE = {"role": "assistant", "content": [{"type": "tool_use", "id": "t1", "name": "get_weather", "input": {}}]}
TOOL_RESULT = {"role": "user", "content": [{"type": "tool_result", "tool_use_id": "t1", "content": "sunny"}]}


def test_trim_never_splits_a_tool_exchange():
    history = [user("a"), assistant("A"), user("b"), TOOL_USE, TOOL_RESULT, assistant("B"), user("c"), assistant("C")]
    trimmed = trim_history(history, 2)
    assert trimmed[0] == user("b")
    assert trimmed[1] == TOOL_USE and trimmed[2] == TOOL_RESULT
    assert trim_history(history, 10) == history


def test_only_the_newest_photo_is_kept():
    old = {"role": "user", "content": [image_block(b"old"), {"type": "text", "text": "how do I look"}]}
    new = {"role": "user", "content": [image_block(b"new"), {"type": "text", "text": "and now?"}]}
    out = without_old_images([old, assistant("sharp"), new])
    assert out[0]["content"][0]["type"] == "text"
    assert out[2]["content"][0]["type"] == "image"
    assert old["content"][0]["type"] == "image"  # the stored history isn't touched


def test_wake_words():
    words = ["tony", "toni"]
    assert mentions("Hey Tony, what's the weather?", words)
    assert mentions("toni what time is it", words)
    assert not mentions("I watched the Tonys last night", words)
    assert not mentions("what's the weather", words)


def test_noise_filter():
    assert is_noise("Thank you.")
    assert is_noise(" ")
    assert not is_noise("Tony, set a timer")


def test_system_prompt_reflects_the_camera():
    on = system_prompt("Evan", "New York, NY", camera_on=True, body_connected=True)
    off = system_prompt("", "New York, NY", camera_on=False, body_connected=True)
    away = system_prompt("", "New York, NY", camera_on=False, body_connected=False)
    assert "camera is ON" in on and "Evan" in on
    assert "camera is OFF" in off and "your buddy" in off
    assert "isn't connected" in away
    assert "[skeptical]" in on
