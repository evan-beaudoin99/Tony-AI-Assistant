"""Weather from Open-Meteo: free, no key, worldwide."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime

import httpx

GEOCODE_URL = "https://geocoding-api.open-meteo.com/v1/search"
FORECAST_URL = "https://api.open-meteo.com/v1/forecast"

# WMO weather codes -> (what to call it, which icon)
_CONDITIONS = {
    0: ("Clear", "clear"),
    1: ("Mostly clear", "clear"),
    2: ("Partly cloudy", "partly"),
    3: ("Overcast", "cloudy"),
    45: ("Foggy", "fog"),
    48: ("Freezing fog", "fog"),
    51: ("Light drizzle", "drizzle"),
    53: ("Drizzle", "drizzle"),
    55: ("Heavy drizzle", "drizzle"),
    56: ("Freezing drizzle", "drizzle"),
    57: ("Freezing drizzle", "drizzle"),
    61: ("Light rain", "rain"),
    63: ("Rain", "rain"),
    65: ("Heavy rain", "rain"),
    66: ("Freezing rain", "rain"),
    67: ("Freezing rain", "rain"),
    71: ("Light snow", "snow"),
    73: ("Snow", "snow"),
    75: ("Heavy snow", "snow"),
    77: ("Snow grains", "snow"),
    80: ("Rain showers", "rain"),
    81: ("Rain showers", "rain"),
    82: ("Violent showers", "rain"),
    85: ("Snow showers", "snow"),
    86: ("Heavy snow showers", "snow"),
    95: ("Thunderstorms", "storm"),
    96: ("Storms with hail", "storm"),
    99: ("Storms with hail", "storm"),
}

_US_STATES = {
    "al": "alabama", "ak": "alaska", "az": "arizona", "ar": "arkansas", "ca": "california", "co": "colorado",
    "ct": "connecticut", "de": "delaware", "fl": "florida", "ga": "georgia", "hi": "hawaii", "id": "idaho",
    "il": "illinois", "in": "indiana", "ia": "iowa", "ks": "kansas", "ky": "kentucky", "la": "louisiana",
    "me": "maine", "md": "maryland", "ma": "massachusetts", "mi": "michigan", "mn": "minnesota",
    "ms": "mississippi", "mo": "missouri", "mt": "montana", "ne": "nebraska", "nv": "nevada",
    "nh": "new hampshire", "nj": "new jersey", "nm": "new mexico", "ny": "new york", "nc": "north carolina",
    "nd": "north dakota", "oh": "ohio", "ok": "oklahoma", "or": "oregon", "pa": "pennsylvania",
    "ri": "rhode island", "sc": "south carolina", "sd": "south dakota", "tn": "tennessee", "tx": "texas",
    "ut": "utah", "vt": "vermont", "va": "virginia", "wa": "washington", "wv": "west virginia",
    "wi": "wisconsin", "wy": "wyoming", "dc": "district of columbia",
}
_STATE_CODES = {name: code.upper() for code, name in _US_STATES.items()}

_ALIASES = {"nyc": "New York", "new york city": "New York", "la": "Los Angeles", "sf": "San Francisco"}


def condition(code: int) -> tuple[str, str]:
    return _CONDITIONS.get(int(code), ("Unsettled", "cloudy"))


@dataclass
class Place:
    name: str
    region: str
    country: str
    country_code: str
    lat: float
    lon: float

    @property
    def label(self) -> str:
        parts = [self.name]
        state = _STATE_CODES.get(self.region.lower()) if self.country_code == "US" else None
        if state:
            parts.append(state)
        elif self.region and self.region != self.name:
            parts.append(self.region)
        elif self.country:
            parts.append(self.country)
        return ", ".join(parts)


@dataclass
class Hour:
    time: datetime
    temp: float
    rain_chance: int
    code: int
    is_day: bool


@dataclass
class Day:
    date: datetime
    high: float
    low: float
    code: int
    rain_chance: int


@dataclass
class Forecast:
    place: Place
    metric: bool
    time: datetime  # local time at the place
    temp: float
    feels: float
    humidity: int
    wind: float
    code: int
    is_day: bool
    hours: list[Hour]
    days: list[Day]

    @property
    def temp_unit(self) -> str:
        return "°C" if self.metric else "°F"

    @property
    def wind_unit(self) -> str:
        return "km/h" if self.metric else "mph"


def _matches(result: dict, hint: str) -> bool:
    hint = hint.strip().lower()
    hint = _US_STATES.get(hint, hint)
    fields = (result.get("admin1", ""), result.get("country", ""), result.get("country_code", ""))
    return any(hint == f.lower() or hint in f.lower() for f in fields if f)


def pick_place(results: list[dict], hint: str | None) -> Place | None:
    if not results:
        return None
    chosen = next((r for r in results if _matches(r, hint)), None) if hint else None
    r = chosen or results[0]
    return Place(
        name=r.get("name", ""),
        region=r.get("admin1", ""),
        country=r.get("country", ""),
        country_code=r.get("country_code", ""),
        lat=r["latitude"],
        lon=r["longitude"],
    )


def parse_forecast(place: Place, data: dict, metric: bool) -> Forecast:
    cur = data["current"]
    now = datetime.fromisoformat(cur["time"])
    hourly = data["hourly"]
    times = [datetime.fromisoformat(t) for t in hourly["time"]]
    this_hour = now.replace(minute=0, second=0, microsecond=0)
    start = next((i for i, t in enumerate(times) if t >= this_hour), 0)
    hours = [
        Hour(
            time=times[i],
            temp=hourly["temperature_2m"][i],
            rain_chance=int(hourly["precipitation_probability"][i] or 0),
            code=int(hourly["weather_code"][i]),
            is_day=bool(hourly["is_day"][i]),
        )
        for i in range(start, min(start + 12, len(times)))
    ]
    daily = data["daily"]
    days = [
        Day(
            date=datetime.fromisoformat(daily["time"][i]),
            high=daily["temperature_2m_max"][i],
            low=daily["temperature_2m_min"][i],
            code=int(daily["weather_code"][i]),
            rain_chance=int(daily["precipitation_probability_max"][i] or 0),
        )
        for i in range(min(3, len(daily["time"])))
    ]
    return Forecast(
        place=place,
        metric=metric,
        time=now,
        temp=cur["temperature_2m"],
        feels=cur["apparent_temperature"],
        humidity=int(cur["relative_humidity_2m"]),
        wind=cur["wind_speed_10m"],
        code=int(cur["weather_code"]),
        is_day=bool(cur["is_day"]),
        hours=hours,
        days=days,
    )


def _clock(t: datetime) -> str:
    return t.strftime("%I %p").lstrip("0")


def describe(fc: Forecast) -> str:
    """The forecast in words, for Claude to talk about."""
    u = fc.temp_unit
    label, _ = condition(fc.code)
    lines = [
        f"{fc.place.label} ({fc.place.country}). Local time {fc.time.strftime('%A %I:%M %p')}.",
        f"Now: {fc.temp:.0f}{u}, feels like {fc.feels:.0f}{u}, {label.lower()}, "
        f"wind {fc.wind:.0f} {fc.wind_unit}, humidity {fc.humidity}%.",
    ]
    if fc.days:
        d = fc.days[0]
        lines.append(f"Today: high {d.high:.0f}{u}, low {d.low:.0f}{u}, rain chance {d.rain_chance}%.")
    if fc.hours:
        steps = ", ".join(
            f"{_clock(h.time)} {h.temp:.0f}{u} ({h.rain_chance}% rain, {condition(h.code)[0].lower()})"
            for h in fc.hours[::3]
        )
        lines.append(f"Next 12 hours: {steps}.")
    for i, d in enumerate(fc.days[1:], start=1):
        name = "Tomorrow" if i == 1 else d.date.strftime("%A")
        lines.append(
            f"{name}: {condition(d.code)[0].lower()}, high {d.high:.0f}{u}, low {d.low:.0f}{u}, "
            f"rain chance {d.rain_chance}%."
        )
    lines.append("A chart of this is on your screen now.")
    return "\n".join(lines)


class WeatherClient:
    def __init__(self) -> None:
        self.http = httpx.AsyncClient(timeout=10)

    async def find(self, query: str) -> Place | None:
        query = query.strip()
        name, _, hint = query.partition(",")
        name = _ALIASES.get(name.strip().lower(), name.strip())
        r = await self.http.get(GEOCODE_URL, params={"name": name, "count": 10, "language": "en"})
        r.raise_for_status()
        return pick_place(r.json().get("results") or [], hint or None)

    async def forecast(self, place: Place, metric: bool) -> Forecast:
        params = {
            "latitude": place.lat,
            "longitude": place.lon,
            "current": "temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m",
            "hourly": "temperature_2m,precipitation_probability,weather_code,is_day",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
            "timezone": "auto",
            "forecast_days": 4,
        }
        if not metric:
            params["temperature_unit"] = "fahrenheit"
            params["wind_speed_unit"] = "mph"
        r = await self.http.get(FORECAST_URL, params=params)
        r.raise_for_status()
        return parse_forecast(place, r.json(), metric)
