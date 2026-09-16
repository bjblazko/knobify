# Third-party components

knobify itself is licensed under GPL-3.0-or-later (see [LICENSE](LICENSE)).
The firmware links the components below; each keeps its own licence, and
all of them are compatible with GPL-3.0-or-later. Versions are pinned in
[`platformio.ini`](platformio.ini).

| Component | Used for | Licence |
|---|---|---|
| [ESP32-audioI2S](https://github.com/esphome/ESP32-audioI2S) 2.3.0 | MP3, AAC/M4A and FLAC decoding, I2S output | GPL-3.0 |
| [LVGL](https://lvgl.io) 8.3.x | UI toolkit and its built-in Montserrat fonts | MIT (fonts: SIL OFL 1.1) |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) 1.8.2 | Album cover decoding (baseline and progressive JPEG) | Apache-2.0 |
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) 1.4.9 | QSPI display driver | BSD-3-Clause (Adafruit GFX lineage) |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) 2.0.x | Arduino core, TinyUSB, SD_MMC | LGPL-2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf) (bundled with the core) | FreeRTOS, FatFs, drivers | Apache-2.0 |
| [Material Symbols](https://github.com/google/material-design-icons) | The icon glyphs in `lib/ui-widgets/IconFont*.c` | Apache-2.0 |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | Host-side unit tests only (not shipped) | MIT |
| [stb_vorbis](https://github.com/nothings/stb) (vendored) | Ogg Vorbis decoding | Public domain / MIT |

GPL-3.0-or-later for knobify is not a free choice: ESP32-audioI2S is
GPL-3.0, so any distributed firmware linking it is covered as a whole.

## AAC/M4A decoding

M4A files are decoded by the AAC decoder bundled inside ESP32-audioI2S,
which derives from RealNetworks' Helix AAC decoder and is distributed by
that project as part of its GPL-3.0 release. knobify adds no decoder of
its own; it reads MP4 metadata (`lib/library/Mp4Parser.h`) and hands the
audio to that library.

AAC-LC — the profile this player targets — dates from the mid-1990s and
its core patents have expired; the licensing pool for it has wound down.
Newer profiles (HE-AAC and successors) are a different matter and are not
targeted here. This is a description of the project's situation, not
legal advice: anyone distributing builds should reach their own
conclusion. See [ADR 0016](docs/adr/0016-native-formats-and-usb-drive.md).
