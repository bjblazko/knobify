# Device: Waveshare ESP32-S3-Knob-Touch-LCD-1.8

*Last modified: 2026-09-11*

## Links

- Product page: https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8

## How it was identified

The board is attached via USB and enumerates as a serial device at `/dev/cu.usbserial-120`. Its USB descriptor reports:

- `idVendor = 0x1A86` (6790) — WCH (Jiangsu Qinheng Electronics)
- `idProduct = 0x7523` (29987) — CH340 USB-to-serial converter
- Product string: "USB Serial"

This board uses a single CH340 USB-serial chip paired with a CH445P analog switch, which lets one USB connection program either of the board's two onboard MCUs — a signature of this specific Waveshare model. The identification was confirmed visually by the owner: a 1.8" round capacitive touch display in a CNC-machined metal knob housing, matching this model exactly.

## Capabilities

**MCUs (dual-chip design):**
- ESP32-S3R8 — dual-core Tensilica LX7 up to 240 MHz, 8 MB PSRAM, Wi-Fi 4, Bluetooth 5.0 LE + Mesh
- ESP32-U4WDH — dual-core Xtensa LX6 up to 240 MHz, 4 MB embedded flash, Wi-Fi 4, Classic Bluetooth

**Storage:**
- 16 MB SPI flash
- microSD card socket

**Display & touch:**
- 1.8" IPS LCD, 360×360 px, 262K colors, ~600 cd/m² brightness, 1200:1 contrast
- ST77916 display driver (QSPI)
- CST816 capacitive touch controller

**Input:**
- Dual rotary encoders (one per MCU)

**Audio:**
- PCM5100A I2S stereo DAC, output via 3.5mm jack (external/amplified speaker or headphones required — no onboard amp/speaker)
- Digital MEMS microphone

**Haptics:**
- DRV2605 vibration/LRA motor driver

**Connectivity:**
- USB-C for power and programming (CH340 USB-serial + CH445P analog switch, single-port dual-MCU programming)
- 2.4 GHz Wi-Fi 802.11 b/g/n
- Bluetooth 5 LE + Classic
- Ceramic antennas

**Power:**
- 5V USB-C input
- Optional 3.7V 800 mAh battery via PH1.25 connector, onboard charging circuit

**Physical:**
- CNC-machined metal enclosure

## GPIO pinout (primary ESP32-S3R8)

Sourced from [Sandjab/Waveshare-Knob](https://github.com/Sandjab/Waveshare-Knob),
a community project targeting this exact board (which itself organizes
Waveshare's own official demo code) — not independently verified against
the physical board yet; confirm with a multimeter/continuity check or a
successful flash before trusting blindly.

| Peripheral | Signal | GPIO |
|---|---|---|
| Display (ST77916, QSPI) | CLK | 13 |
| | D0–D3 | 15, 16, 17, 18 |
| | CS | 14 |
| | RST | 21 |
| | Backlight (PWM) | 47 |
| Touch (CST816) | SDA | 11 (shared I2C bus) |
| | SCL | 12 (shared I2C bus) |
| | INT | 9 |
| | RST | 10 |
| | I2C address | 0x15 |
| Haptics (DRV2605, out of v1 scope) | I2C | shared bus (11/12) |
| | I2C address | 0x5A |
| Rotary encoder (primary) | A (CLK) | 8 |
| | B (DT) | 7 |
| | Push button | **none documented** — matches this project's own web-research finding (see "Hardware quirks observed" and ADR 0004) |
| SD card (SDMMC 4-wire) | CMD | 3 |
| | CLK | 4 |
| | D0–D3 | 5, 6, 42, 2 |
| I2S audio DAC (PCM5100A) | BCLK | 39 |
| | WS/LRCK | 40 |
| | DOUT | 41 |
| Other | Battery ADC | 1 (confirmed via live serial probe 2026-09-13 — plausible, stable ~2400mV reading with battery attached; see [ADR 0007](docs/adr/0007-battery-indicator.md)) |
| | Mic (PDM) | CLK 45, DATA 46 |
| | Inter-MCU UART (out of v1 scope) | TX 43, RX 44 |

Waveshare's own official Arduino demo for this board (mirrored in the
same community repo) targets **LVGL v8.3.11** (RGB565 color depth,
byte-swapped for QSPI) — knobify's `lib_deps` should pin the same major
version rather than LVGL v9, to stay compatible with any ST77916 QSPI
init/driver code ported from that demo.

## Hardware quirks observed

- **The primary ESP32-S3R8 also enumerates via its own native
  USB-Serial-JTAG peripheral** (Espressif VID `0x303A`, PID `0x1001`),
  separately from the CH340 path this board was originally identified
  through (see "How it was identified" above). On 2026-09-11, flashing
  succeeded directly over this native port (`pio run -t upload` /
  `esptool.py` reported "Chip is ESP32-S3 (QFN56) ... Embedded PSRAM
  8MB", confirming the primary MCU) without going through the CH340 at
  all. `scripts/flash-primary-mcu.sh` auto-detects both VID:PID
  candidates.

- **USB-C cable orientation matters for which MCU you talk to.** Despite
  USB-C's connector being physically reversible, this board's single
  CH340-based USB-serial port is switched between the two onboard MCUs
  (ESP32-S3R8 primary / ESP32-U4WDH secondary) via the CH445P analog
  switch. In practice, flipping the cable's orientation can change which
  MCU actually receives flashing/serial traffic. When flashing or
  debugging, if the expected MCU (the primary ESP32-S3R8 for this
  project) doesn't respond, try the cable the other way round before
  assuming a wiring or code problem. `scripts/flash-primary-mcu.sh` (and
  the `flash-device` skill wrapping it) automates this check when
  flashing.

## Sources

- https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm
- https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8
- https://www.cnx-software.com/2025/06/25/battery-powered-knob-display-board-pairs-esp32-s3-and-esp32-wireless-socs-features-audio-dac-for-audio-visualization/
- https://github.com/KrX3D/WaveShare-Knob-Esp32S3
