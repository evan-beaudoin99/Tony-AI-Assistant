# Tony parts list

Your personal desktop AI assistant, with a New York attitude. Wiring for all of these is in
[`schematic/WIRING.md`](schematic/WIRING.md).

## Core parts

| Part | Job | What it does for Tony |
|---|---|---|
| **ESP32-S3 N16R8 CAM** dev board, with the **OV3660** camera | Body + eyes | WiFi, the face, the button, and the camera. 16 MB flash and 8 MB PSRAM hold the audio and frame buffers |
| **2" TFT LCD, ST7789, SPI** (240×320) | Face | His face, the status bar, and weather cards |
| **INMP441** I2S MEMS microphone | Ears | Hears "Hey Tony" from across the desk |
| **8 Ω 1 W mini speaker**, 20 mm | Mouth | Says it back, with attitude |
| **2× 18650 Li-ion cells** | Lungs | Cordless power, wired in parallel |

## Supporting parts

The speaker can't run straight off the ESP32, and the 18650s need a charger and a 5 V boost. These parts are
cheap and close the gaps.

### Required

| Part | Qty | Why | Approx. price |
|---|---|---|---|
| **MAX98357A** I2S amplifier breakout | 1 | Turns the ESP32's digital audio into speaker power | $3–6 |
| **IP5306 USB-C charge + 5 V boost module** (2.1 A, often sold as an "18650 charging/boost board") | 1 | Charges the cells over USB-C and boosts 3.7 V to 5 V | $2–5 |
| **2-slot 18650 battery holder**, with wire leads | 1 | Rewire it for parallel (+ to +, − to −) if it comes wired in series | $1–3 |
| **Slide switch** (SPDT, rated 1 A or more) | 1 | Power on/off | $1 |
| **470–1000 µF electrolytic capacitor**, 10 V or higher | 1 | Stops speaker peaks from browning out the ESP32 | <$1 |
| Jumper wires (female–female), or perfboard + 22–26 AWG wire | 1 set | Hooking it all up | $3–6 |
| **USB-C data cable** | 1 | Flashing the ESP32 and charging. Must be a data cable, not charge-only | — |

### Optional

| Part | Qty | Why | Approx. price |
|---|---|---|---|
| **100 kΩ resistors** | 2 | Battery percentage on the status bar | <$1 |
| **12 mm tactile push button** | 1 | A big button on the stand (the board's BOOT button works too) | <$1 |
| M2 / M2.5 screws and heat-set inserts | a few | Mounting everything in the 3D-printed stand | $2 |

**About $15–25 total** for the extras.

### Buying notes

- **18650 cells:** use two of the same make and model, both charged to the same voltage before you wire them in
  parallel. Protected cells (with a little circuit on the end) are the safer choice. Check the holder is long enough,
  since protected cells are a couple of millimeters longer.
- **Charger alternative:** a TP4056 USB-C charger (get the protected version, with six pads) plus an MT3608 boost
  module set to 5.0 V also works. But that's two boards, the MT3608 has to be tuned with a screwdriver and a
  multimeter before you connect anything, and running Tony while it charges can confuse the TP4056's charge cutoff.
  The IP5306 is the easy route.
- **MAX98357A:** Adafruit's original or any of the purple or blue clones work the same.
- **Speaker:** your 20 mm 1 W speaker works. A 28–40 mm, 2–3 W, 4–8 Ω speaker sounds a lot fuller if you have room
  in the stand.

### Not needed

- **A microSD card:** keep the slot **empty**, because Tony uses those pins for the amplifier.
- **A separate camera module:** the OV3660 on the board's ribbon is the one Tony uses.

## Running costs

The ESP32 is the body; the brain runs on your computer and uses cloud APIs.

| Service | What for | Cost |
|---|---|---|
| Anthropic API (Claude Haiku 4.5) | Thinking, talking, and looking at camera photos | Pay as you go. Usually under a cent per question, a little more with a photo |
| OpenAI API (gpt-4o-mini-tts) | The New York accent voice | Optional. Roughly 1.5 cents per minute of Tony talking |
| Open-Meteo | Weather | Free, no key |
| faster-whisper | Speech-to-text, on your own computer | Free |
