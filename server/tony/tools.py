"""What Tony can do besides talk."""

from __future__ import annotations

import asyncio
import logging
from datetime import datetime, timedelta
from typing import TYPE_CHECKING, Any

from . import cards
from .brain import image_block
from .personality import TIMER_DONE, pick
from .weather import WeatherClient, describe

if TYPE_CHECKING:
    from .hub import Tony

log = logging.getLogger(__name__)

GET_WEATHER = {
    "name": "get_weather",
    "description": "Current weather plus the next 12 hours and 3 days for a place. Also shows a weather chart on "
    "Tony's screen.",
    "input_schema": {
        "type": "object",
        "properties": {
            "location": {
                "type": "string",
                "description": "A city, optionally with state or country: 'New York', 'Austin, TX', 'Paris, France'.",
            }
        },
        "required": ["location"],
    },
}

SET_TIMER = {
    "name": "set_timer",
    "description": "Start a countdown timer. Tony rings and calls out when it's done.",
    "input_schema": {
        "type": "object",
        "properties": {
            "seconds": {"type": "integer", "description": "How long, in seconds (5 minutes = 300)."},
            "label": {"type": "string", "description": "What it's for, like 'pasta'. Optional."},
        },
        "required": ["seconds"],
    },
}

CANCEL_TIMERS = {
    "name": "cancel_timers",
    "description": "Cancel every running timer.",
    "input_schema": {"type": "object", "properties": {}},
}

SET_CAMERA = {
    "name": "set_camera",
    "description": "Turn Tony's camera on or off. Turning it on returns a fresh photo of what's in front of him.",
    "input_schema": {
        "type": "object",
        "properties": {"on": {"type": "boolean"}},
        "required": ["on"],
    },
}

LOOK = {
    "name": "look",
    "description": "Take a photo with Tony's camera (only works while the camera is on).",
    "input_schema": {"type": "object", "properties": {}},
}

GO_TO_SLEEP = {
    "name": "go_to_sleep",
    "description": "Put Tony to sleep after this reply: his mic and camera switch off until someone holds his "
    "button or wakes him from the dashboard. Say goodnight first.",
    "input_schema": {"type": "object", "properties": {}},
}


class Timers:
    def __init__(self, tony: Tony):
        self.tony = tony
        self._tasks: dict[int, asyncio.Task] = {}
        self._next_id = 1

    def add(self, seconds: int, label: str) -> datetime:
        timer_id, self._next_id = self._next_id, self._next_id + 1
        self._tasks[timer_id] = asyncio.create_task(self._ring(timer_id, seconds, label))
        return datetime.now() + timedelta(seconds=seconds)

    def cancel_all(self) -> int:
        count = len(self._tasks)
        for task in self._tasks.values():
            task.cancel()
        self._tasks.clear()
        return count

    async def _ring(self, timer_id: int, seconds: int, label: str) -> None:
        await asyncio.sleep(seconds)
        self._tasks.pop(timer_id, None)
        card = await asyncio.to_thread(cards.timer_card, label, seconds, True)
        await self.tony.announce(pick(TIMER_DONE, label=label or "kitchen"), mood="excited", card=card, chime=True)


class Toolbox:
    def __init__(self, tony: Tony):
        self.tony = tony
        self.weather = WeatherClient()
        self.timers = Timers(tony)

    def definitions(self) -> list[dict]:
        s = self.tony.s
        tools = [GET_WEATHER, SET_TIMER, CANCEL_TIMERS, SET_CAMERA, GO_TO_SLEEP]
        if not s.camera.attach_when_on:
            tools.append(LOOK)
        if s.claude.web_search:
            tools.append({"type": "web_search_20250305", "name": "web_search", "max_uses": 2})
        return tools

    async def run(self, name: str, args: dict) -> Any:
        handler = getattr(self, f"_tool_{name}", None)
        if handler is None:
            return f"There's no tool called {name}."
        try:
            return await handler(**args)
        except TypeError as e:
            return f"Bad arguments for {name}: {e}"
        except Exception as e:
            log.exception("Tool %s failed", name)
            return f"{name} didn't work: {e}"

    async def _tool_get_weather(self, location: str = "") -> str:
        query = location.strip() or self.tony.s.you.home
        place = await self.weather.find(query)
        if place is None:
            return f"Couldn't find any place called {query!r}."
        forecast = await self.weather.forecast(place, metric=self.tony.s.you.units == "metric")
        card = await asyncio.to_thread(cards.weather_card, forecast)
        await self.tony.show_card(card, seconds=20)
        return describe(forecast)

    async def _tool_set_timer(self, seconds: int, label: str = "") -> str:
        seconds = int(seconds)
        if not 1 <= seconds <= 24 * 3600:
            return "Timers have to be between 1 second and 24 hours."
        rings_at = self.timers.add(seconds, label.strip())
        card = await asyncio.to_thread(cards.timer_card, label.strip(), seconds)
        await self.tony.show_card(card, seconds=6)
        return f"Timer set for {seconds} seconds{f' ({label})' if label else ''}. It rings at {rings_at:%I:%M %p}."

    async def _tool_cancel_timers(self) -> str:
        count = self.timers.cancel_all()
        return f"Cancelled {count} timer{'s' if count != 1 else ''}." if count else "There weren't any timers."

    async def _tool_set_camera(self, on: bool) -> Any:
        result = await self.tony.set_camera(bool(on))
        if result is None:
            return "Tony's body isn't connected, so there's no camera right now."
        if result != bool(on):
            return "The camera didn't switch. It might be unplugged, or Tony's asleep."
        if not on:
            return "Camera's off now."
        await asyncio.sleep(0.8)  # give the sensor a moment to settle its exposure
        frame = await self.tony.snapshot()
        if frame is None:
            return "Camera's on, but the first photo didn't come through."
        return [{"type": "text", "text": "Camera's on. Here's what's in front of you right now:"}, image_block(frame)]

    async def _tool_look(self) -> Any:
        frame = await self.tony.snapshot()
        if frame is None:
            return "No photo: the camera's off or not connected."
        return [image_block(frame)]

    async def _tool_go_to_sleep(self) -> str:
        if self.tony.device is None:
            return "Tony's body isn't connected, so there's nothing to put to sleep."
        self.tony.sleep_after_reply = True
        return "You'll fall asleep as soon as you finish talking."
