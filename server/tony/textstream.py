"""Turns Claude's streamed text into things Tony can do right away: change his face, say a sentence.

Claude writes mood tags like [happy] inline. They come out as ("mood", "happy"); the speech between them comes out
a sentence or so at a time as ("say", text), so the voice can start on the first sentence while the rest is still
being written.

Every reply is supposed to open with a mood tag. Text before the first one is held back until the reply ends, because
that's where the model occasionally writes its reasoning ("The user is asking... </thinking>") instead of talking.
A leading [thinking] counts as "before the first one": it's the tag the model reaches for when it reasons out loud.
Anything up to a </thinking> is dropped, and <thinking>...</thinking> blocks are never spoken.
"""

from __future__ import annotations

import re

from .protocol import EMOTIONS

Event = tuple[str, str]

_SENTENCE_END = re.compile(r"[.!?]+[\"')\]]*\s+|\n+")
_MARKDOWN = re.compile(r"[*_#`>|~]+")
_URL = re.compile(r"https?://\S+")
_SPACES = re.compile(r"\s+")
_ANGLE_TAG = re.compile(r"</?[a-zA-Z_]+>?")
_THINK_OPEN = "<thinking>"
_THINK_CLOSE = re.compile(r"</thinking\b>?", re.IGNORECASE)
_MAX_TAG = 20


def tidy(text: str) -> str:
    """What the voice should actually say: no markdown, links, tags, or stray whitespace."""
    text = _URL.sub("", text)
    text = _ANGLE_TAG.sub("", text)
    text = _MARKDOWN.sub("", text)
    return _SPACES.sub(" ", text).strip()


class SpeechChunker:
    def __init__(self, first_min_chars: int = 12, min_chars: int = 40):
        self.first_min_chars = first_min_chars
        self.min_chars = min_chars
        self._raw = ""
        self._clean = ""
        self._first = True
        self._tagged = False  # seen a mood tag yet: until then, nothing is spoken early
        self._thinking = False

    def feed(self, text: str) -> list[Event]:
        self._raw += text
        events = self._pull_tags()
        if self._tagged:
            events += self._pull_sentences(final=False)
        return events

    def flush(self) -> list[Event]:
        """Call when the stream ends: whatever's left gets said."""
        if not self._thinking:
            self._clean += self._raw
        self._raw = ""
        self._thinking = False
        if not self._tagged:
            closes = list(_THINK_CLOSE.finditer(self._clean))
            if closes:
                self._clean = self._clean[closes[-1].end():]
        return self._pull_sentences(final=True)

    def _pull_tags(self) -> list[Event]:
        events: list[Event] = []
        while True:
            if self._thinking:
                close = _THINK_CLOSE.search(self._raw)
                if not close or (close.end() == len(self._raw) and not self._raw.endswith(">")):
                    return events  # still inside the reasoning (or its closing tag is still arriving)
                self._raw = self._raw[close.end():]
                self._thinking = False
                continue
            start = self._raw.find("[")
            think = self._raw.lower().find(_THINK_OPEN)
            if think != -1 and (start == -1 or think < start):
                self._clean += self._raw[:think]
                self._raw = self._raw[think + len(_THINK_OPEN):]
                self._thinking = True
                continue
            if start == -1:
                lt = self._raw.rfind("<")
                if lt != -1 and len(self._raw) - lt < len(_THINK_OPEN):  # might be the start of <thinking>
                    self._clean += self._raw[:lt]
                    self._raw = self._raw[lt:]
                    return events
                self._clean += self._raw
                self._raw = ""
                return events
            self._clean += self._raw[:start]
            self._raw = self._raw[start:]
            end = self._raw.find("]")
            if end == -1:
                if len(self._raw) > _MAX_TAG:  # a lone "[", not a tag
                    self._clean += self._raw[0]
                    self._raw = self._raw[1:]
                    continue
                return events  # the rest of the tag is still on its way
            word = self._raw[1:end].strip().lower()
            if word in EMOTIONS:
                if not self._tagged and word == "thinking" and self._first:
                    pass  # "[thinking]" up front is usually the model reasoning: keep holding until a real mood
                elif not self._tagged:
                    self._tagged = True
                    self._clean = ""  # whatever came before his first mood tag was the model talking to itself
                events.append(("mood", word))
            # Anything else in brackets ([laughs], [pause]) is a stage direction: never read it out.
            self._raw = self._raw[end + 1 :]

    def _pull_sentences(self, final: bool) -> list[Event]:
        events: list[Event] = []
        while True:
            limit = self.first_min_chars if self._first else self.min_chars
            cut = next((m.end() for m in _SENTENCE_END.finditer(self._clean) if m.end() >= limit), None)
            if cut is None:
                break
            self._say(self._clean[:cut], events)
            self._clean = self._clean[cut:]
        if final:
            self._say(self._clean, events)
            self._clean = ""
        return events

    def _say(self, text: str, events: list[Event]) -> None:
        text = tidy(text)
        if any(c.isalnum() for c in text):
            events.append(("say", text))
            self._first = False
