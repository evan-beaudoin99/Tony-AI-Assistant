from tony.textstream import SpeechChunker


def run(pieces, **kw):
    chunker = SpeechChunker(**kw)
    events = []
    for p in pieces:
        events += chunker.feed(p)
    return events + chunker.flush()


def test_mood_tag_split_across_deltas():
    events = run(["[hap", "py] Ay, how you doin'? ", "Not bad for a Tuesday, right?"])
    assert events[0] == ("mood", "happy")
    assert ("say", "Ay, how you doin'?") in events
    assert events[-1] == ("say", "Not bad for a Tuesday, right?")


def test_first_sentence_goes_out_early_then_bigger_chunks():
    events = run(["[neutral] Sure thing. It's sunny. It's warm. Go outside, ya bum!"], min_chars=30)
    says = [e[1] for e in events if e[0] == "say"]
    assert says[0] == "Sure thing."
    assert all(len(s) >= 11 for s in says)
    assert "".join(says).replace(" ", "") == "Surething.It'ssunny.It'swarm.Gooutside,yabum!"


def test_mid_reply_mood_and_stage_directions():
    events = run(["[excited] Yankees won! [sad] [sighs] But the Mets lost again."])
    assert [e for e in events if e[0] == "mood"] == [("mood", "excited"), ("mood", "sad")]
    assert all("sighs" not in e[1] for e in events)


def test_decimals_and_markdown():
    events = run(["[neutral] It's **3.5 degrees** out there. Brr."])
    assert ("say", "It's 3.5 degrees out there.") in events


def test_lone_bracket_is_spoken():
    events = run(["[neutral] That's " + "[" + "a" * 30 + " weird."])
    assert any("[" in e[1] for e in events if e[0] == "say")


def says(events):
    return " ".join(e[1] for e in events if e[0] == "say")


def test_reasoning_before_a_closing_think_tag_is_never_spoken():
    leaked = ["The user is asking for the weather. I should use the get_weather ", "function first. </thinking",
              " Lemme check, hold on."]
    assert says(run(leaked)) == "Lemme check, hold on."


def test_thinking_blocks_are_never_spoken_even_split_across_deltas():
    events = run(["[neutral] Lemme check. <thi", "nking>they want the weather, call the ", "tool</think", "ing> Hold on."])
    assert says(events) == "Lemme check. Hold on."


def test_text_before_the_first_mood_tag_is_dropped():
    events = run(["They're askin' about the weather, so it's New York. ", "[curious] Lemme check, hold on."])
    assert events[0] == ("mood", "curious")
    assert says(events) == "Lemme check, hold on."


def test_reasoning_under_a_leading_thinking_tag_is_dropped():
    events = run(["[thinking] The user is asking about the weather. I should call get_weather.\n\n",
                  "[neutral] Lemme check, hold on."])
    assert says(events) == "Lemme check, hold on."
    assert ("mood", "neutral") in events


def test_a_real_thinking_mood_is_still_said_at_the_end():
    assert says(run(["[thinking] Hmm. Good question, lemme think."])) == "Hmm. Good question, lemme think."


def test_a_reply_with_no_mood_tag_still_gets_said():
    assert says(run(["Ay, how ya doin'? ", "Good to see ya."])) == "Ay, how ya doin'? Good to see ya."


def test_a_less_than_sign_is_still_spoken():
    assert says(run(["[neutral] It's 5 < 7. Simple math."])) == "It's 5 < 7. Simple math."


def test_empty_and_punctuation_only_are_dropped():
    assert run(["[happy]"]) == [("mood", "happy")]
    assert run(["[happy] ..."]) == [("mood", "happy")]
