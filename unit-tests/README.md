# Tony's hardware tests

Three small sketches that check Tony's parts one at a time, before you flash the full firmware. When something
doesn't work, you'll know which part it is: camera, microphone or speaker.

Each test runs its checks at start-up and prints `PASS` / `FAIL` / `WARN` on the serial monitor. A failed check
comes with a hint about what to look at. Then the test stays running so you can see, hear and poke at the part.

The pins and WiFi settings come from [`../firmware/include/pins.h`](../firmware/include/pins.h) and
`../firmware/include/secrets.h`, so the tests always match your real wiring. The camera and mic settings are
copied from the real firmware, so what you see and hear here is what Tony gets.

## Before you start

- Copy `firmware/include/secrets.example.h` to `firmware/include/secrets.h` and fill in your WiFi name and password.
  The camera and mic tests need WiFi for their web page. The speaker test doesn't.
- Plug in the board's **COM / UART** USB-C port, with the battery switched off.
- Leave the microSD slot empty. It shares pins 38-40 with the amplifier.

Run everything from this folder:

```powershell
cd unit-tests
```

If an upload can't connect, hold **BOOT**, tap **RESET**, release **BOOT**, and try again.

## 1. Camera

```powershell
pio run -e camera -t upload
pio device monitor
```

**Checks:** PSRAM is found, the camera driver starts, the sensor is an OV3660, a frame comes back as a complete
JPEG, and 20 frames in a row arrive at a steady rate.

**Then:** it joins WiFi and prints an address, `http://<ip>/` or `http://tony-camera.local/`. Open it in a browser
on the same network to see the live view. The page has:

- the live stream, plus its frame rate, frame size, WiFi signal and free memory
- resolution (VGA is what Tony uses), JPEG quality, brightness, saturation, flip and mirror
- **Snapshot**, which opens one full frame in a new tab

The board's RGB LED glows red while the camera is on, the same as in Tony.

If the picture is upside down, change `set_vflip` in `firmware/src/Camera.cpp` to match whatever looks right here.

## 2. Microphone + amplifier

```powershell
pio run -e mic -t upload
pio device monitor
```

**Checks:** the mic's I2S clock runs, the data line isn't stuck (a missing or miswired mic usually reads all 0s or
all 1s), and the quiet-room noise floor looks reasonable. Then it asks you to **clap or say "Hey Tony"** within
4 seconds and checks that the level jumps.

**Then:** it's a loopback. **Tap BOOT** (or press `r`) and it records 3 seconds, then plays them back through the
amplifier and speaker. The serial monitor shows a live level meter. The web page at `http://tony-mic.local/` has:

- a live level meter with peak hold and a clipping warning
- **Record 3 s, play on Tony** and **Play again on Tony**
- **Play here in the browser**: the same recording through your computer. If it sounds right in the browser but
  not on Tony, the mic is fine and the amp or speaker is the problem (run test 3).
- speaker volume, and mic gain (`MIC_SHIFT`)

Serial keys: `r` record + play, `p` play again, `m` meter on / off, `+` more mic gain, `-` less mic gain.

If the gain sounds better at a different setting, put that `MIC_SHIFT` number into `firmware/src/Audio.cpp`.
A lower number means more gain.

## 3. Speaker

```powershell
pio run -e speaker -t upload
pio device monitor
```

The ESP32 can't tell whether a speaker is attached, so this test is by ear. At start-up it plays every sound once
and prints what you should hear:

| # | Sound | Listen for |
|---|---|---|
| 1 | 440 Hz beep, 1 s | Clean and steady, with no buzz or rattle |
| 2 | Sweep, 100 Hz to 8 kHz | A smooth rising whistle. It's faint at the bottom, which is normal for a small speaker |
| 3 | Volume steps, 10 to 100 % | Each beep is louder than the last. Crackle or a reset at the top means the 5 V rail is sagging |
| 4 | Arpeggio | Eight clean, separate notes |
| 5 | Tony's chirp | The "I'm listening" blip from the real firmware |

Serial keys: `1`-`5` play one sound, `a` play them all, `s` stop, `+` / `-` volume, `h` menu.
BOOT button: tap for the next sound, hold to stop.

## If the board resets during sound

Every test checks why the board last restarted. If it says **BROWNOUT**, the speaker pulled the supply down. Put a
470 µF capacitor across the amp's VIN and GND, right at the amp, and check that the battery is charged. See
[`../schematic/WIRING.md`](../schematic/WIRING.md).

## What's in here

```
unit-tests/
├── platformio.ini        one environment per test: camera, mic, speaker
├── include/TestKit.h     PASS / FAIL printing, brownout check, WiFi join
└── src/
    ├── camera_test.cpp
    ├── mic_test.cpp
    └── speaker_test.cpp
```
