"""Tony's brain: Claude Haiku, streamed, with tools.

respond() yields ("mood", name) and ("say", sentence) events as soon as Claude writes them, so Tony starts talking
on the first sentence. When Claude reaches for a tool, it usually says a filler line first ("Lemme check...");
that line is spoken while the tool runs.
"""

from __future__ import annotations

import base64
import logging
import time
from collections.abc import AsyncIterator, Awaitable, Callable
from typing import Any

from anthropic import AsyncAnthropic

from .config import Settings
from .personality import system_prompt
from .textstream import Event, SpeechChunker

log = logging.getLogger(__name__)

MAX_TOOL_ROUNDS = 4

ToolRunner = Callable[[str, dict], Awaitable[Any]]


def image_block(jpeg: bytes) -> dict:
    return {
        "type": "image",
        "source": {"type": "base64", "media_type": "image/jpeg", "data": base64.b64encode(jpeg).decode()},
    }


def _is_turn_start(message: dict) -> bool:
    """A user message that's something they said, not a tool result."""
    if message["role"] != "user":
        return False
    content = message["content"]
    if isinstance(content, str):
        return True
    return any(block.get("type") in ("text", "image") for block in content)


def trim_history(history: list[dict], keep_turns: int) -> list[dict]:
    starts = [i for i, m in enumerate(history) if _is_turn_start(m)]
    if len(starts) <= keep_turns:
        return history
    return history[starts[-keep_turns]:]


def without_old_images(history: list[dict]) -> list[dict]:
    """Only the newest photo is worth its tokens; older ones become a note."""
    last = max((i for i, m in enumerate(history) if _is_turn_start(m)), default=-1)
    out = []
    for i, m in enumerate(history):
        if i != last and m["role"] == "user" and isinstance(m["content"], list):
            blocks = [
                {"type": "text", "text": "(a camera photo from earlier)"} if b.get("type") == "image" else b
                for b in m["content"]
            ]
            m = {"role": "user", "content": blocks}
        out.append(m)
    return out


def _assistant_blocks(content) -> list[dict]:
    blocks = []
    for block in content:
        b = block.model_dump(exclude_none=True)
        if b.get("type") == "text" and not b.get("text", "").strip():
            continue
        blocks.append(b)
    return blocks or [{"type": "text", "text": "[neutral] ..."}]


class Brain:
    def __init__(self, settings: Settings, tools: list[dict], run_tool: ToolRunner):
        self.s = settings
        self.client = AsyncAnthropic(api_key=settings.secrets.anthropic_api_key or None)
        self.tools = tools
        self.run_tool = run_tool
        self.history: list[dict] = []
        self.last_turn = 0.0

    def forget(self) -> None:
        self.history.clear()

    async def respond(
        self, text: str, image: bytes | None = None, *, camera_on: bool = False, body_connected: bool = True
    ) -> AsyncIterator[Event]:
        cfg = self.s.claude
        if time.monotonic() - self.last_turn > cfg.forget_after_minutes * 60:
            self.history.clear()
        self.last_turn = time.monotonic()

        content: list[dict] = [image_block(image)] if image else []
        content.append({"type": "text", "text": text})
        self.history.append({"role": "user", "content": content})
        self.history = trim_history(self.history, cfg.memory_turns)
        start = len(self.history) - 1

        system = system_prompt(self.s.you.name, self.s.you.home, camera_on, body_connected)
        spoken: list[str] = []
        try:
            for _ in range(MAX_TOOL_ROUNDS + 1):
                chunker = SpeechChunker()
                async with self.client.messages.stream(
                    model=cfg.model,
                    max_tokens=cfg.max_tokens,
                    system=system,
                    messages=without_old_images(self.history),
                    tools=self.tools,
                ) as stream:
                    async for delta in stream.text_stream:
                        for event in chunker.feed(delta):
                            if event[0] == "say":
                                spoken.append(event[1])
                            yield event
                    final = await stream.get_final_message()
                for event in chunker.flush():
                    if event[0] == "say":
                        spoken.append(event[1])
                    yield event

                self.history.append({"role": "assistant", "content": _assistant_blocks(final.content)})
                if final.stop_reason == "pause_turn":  # a long server-side web search: let it keep going
                    continue
                if final.stop_reason != "tool_use":
                    return
                results = []
                for block in final.content:
                    if block.type == "tool_use":
                        log.info("tool: %s %s", block.name, block.input)
                        result = await self.run_tool(block.name, dict(block.input))
                        results.append({"type": "tool_result", "tool_use_id": block.id, "content": result})
                self.history.append({"role": "user", "content": results})
            line = "I got myself all tangled up there. Ask me again?"
            self.history.append({"role": "assistant", "content": [{"type": "text", "text": f"[confused] {line}"}]})
            yield ("mood", "confused")
            yield ("say", line)
        except BaseException:
            # Interrupted or failed: keep the question and what he got out, drop any half-finished tool calls, so
            # the next request is still a valid conversation.
            del self.history[start:]
            self.history.append({"role": "user", "content": [{"type": "text", "text": text}]})
            said = " ".join(spoken) or "..."
            self.history.append({"role": "assistant", "content": [{"type": "text", "text": f"{said} (cut off)"}]})
            raise
