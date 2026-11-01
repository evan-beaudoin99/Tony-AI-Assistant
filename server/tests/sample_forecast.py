"""An Open-Meteo response shaped like the real thing, for tests and card previews."""

import math
from datetime import datetime, timedelta

from tony.weather import Place, parse_forecast

NYC = Place("New York", "New York", "United States", "US", 40.71, -74.01)


def sample_response(start="2026-10-31T22:00", code=2, is_day=0):
    t0 = datetime.fromisoformat(start).replace(hour=0)
    hours = [t0 + timedelta(hours=i) for i in range(96)]
    temps = [55 + 8 * math.sin((h.hour - 9) / 24 * 2 * math.pi) for h in hours]
    rain = [0] * 30 + [10, 20, 40, 70, 80, 60, 30, 10] + [0] * 58
    return {
        "current": {
            "time": start,
            "temperature_2m": 54.2,
            "apparent_temperature": 50.8,
            "relative_humidity_2m": 61,
            "is_day": is_day,
            "weather_code": code,
            "wind_speed_10m": 9.4,
        },
        "hourly": {
            "time": [h.isoformat(timespec="minutes") for h in hours],
            "temperature_2m": temps,
            "precipitation_probability": rain,
            "weather_code": [61 if r >= 40 else 3 for r in rain],
            "is_day": [1 if 7 <= h.hour < 18 else 0 for h in hours],
        },
        "daily": {
            "time": [(t0 + timedelta(days=i)).date().isoformat() for i in range(4)],
            "temperature_2m_max": [63.1, 58.4, 61.0, 66.2],
            "temperature_2m_min": [48.0, 45.5, 47.9, 50.3],
            "weather_code": [2, 63, 0, 3],
            "precipitation_probability_max": [10, 80, 0, 20],
        },
    }


def sample_forecast(**kw):
    return parse_forecast(NYC, sample_response(**kw), metric=False)
