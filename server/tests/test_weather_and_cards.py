import io

from PIL import Image

from sample_forecast import sample_forecast
from tony import cards
from tony.weather import describe, pick_place


def test_forecast_starts_at_the_current_hour():
    fc = sample_forecast()
    assert fc.hours[0].time.hour == 22
    assert len(fc.hours) == 12
    assert fc.hours[-1].time.hour == 9  # runs past midnight
    assert [d.high for d in fc.days] == [63.1, 58.4, 61.0]


def test_describe_mentions_the_essentials():
    text = describe(sample_forecast())
    assert "New York" in text
    assert "54°F" in text
    assert "Tomorrow: rain" in text
    assert "on your screen" in text


def test_state_hint_picks_the_right_city():
    results = [
        {"name": "Portland", "admin1": "Oregon", "country": "United States", "country_code": "US",
         "latitude": 45.5, "longitude": -122.7},
        {"name": "Portland", "admin1": "Maine", "country": "United States", "country_code": "US",
         "latitude": 43.7, "longitude": -70.3},
    ]
    assert pick_place(results, " ME").region == "Maine"
    assert pick_place(results, None).region == "Oregon"
    assert pick_place([], "NY") is None


def test_place_labels_read_naturally():
    nyc = pick_place([{"name": "New York", "admin1": "New York", "country": "United States",
                       "country_code": "US", "latitude": 40.7, "longitude": -74.0}], None)
    paris = pick_place([{"name": "Paris", "admin1": "Île-de-France", "country": "France",
                         "country_code": "FR", "latitude": 48.9, "longitude": 2.35}], None)
    assert nyc.label == "New York, NY"
    assert paris.label == "Paris, Île-de-France"


def _check_jpeg(data: bytes):
    assert len(data) < 60_000
    img = Image.open(io.BytesIO(data))
    assert img.format == "JPEG"
    assert img.size == (320, 240)
    assert "progressive" not in img.info  # the ESP32's decoder only reads baseline


def test_weather_card_renders_for_every_icon(tmp_path):
    for code, is_day in [(0, 1), (0, 0), (2, 1), (2, 0), (3, 1), (45, 1), (53, 1), (63, 1), (73, 1), (95, 0)]:
        data = cards.weather_card(sample_forecast(code=code, is_day=is_day))
        _check_jpeg(data)
    (tmp_path / "weather.jpg").write_bytes(cards.weather_card(sample_forecast()))


def test_timer_cards_render():
    _check_jpeg(cards.timer_card("pasta", 300))
    _check_jpeg(cards.timer_card("", 5400))
    _check_jpeg(cards.timer_card("pasta", 300, done=True))
