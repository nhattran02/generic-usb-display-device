# Third-party code

All third-party code in this repository is under the MIT license (full text below).

| Path | Origin | Copyright | Changes |
|---|---|---|---|
| `components/tinyusb/src/`, `components/tinyusb/LICENSE` | [TinyUSB](https://github.com/hathach/tinyusb) via [espressif/tinyusb](https://github.com/espressif/tinyusb) 0.21.x (device stack, HID class, DWC2 port only) | Ha Thach (tinyusb.org) and contributors; per-file notices | none (`CMakeLists.txt`, `tusb_config.h` are this project's) |
| `main/gud/gud.c`, `main/gud/gud.h` | [notro/gud-pico](https://github.com/notro/gud-pico), `libraries/gud_pico/` (protocol header mirrors Linux `include/drm/gud.h`) | Copyright (c) 2021-2024 Noralf Trønnes | small adaptations |
| `main/gud/gud_driver.c`, `main/gud/gud_driver.h` | based on `driver.c` from notro/gud-pico | Copyright (c) 2021-2024 Noralf Trønnes | largely rewritten: composite device, ESP32-S3 OUT transfer limit, streaming LZ4 decoder, reboot request; via [Ponywka/gud-esp32s3](https://github.com/Ponywka/gud-esp32s3) (Copyright (c) 2026 Vladimir Avtsenov), here also the `write_buffer` hook for the SPI panel |
| `host/gud-module/gud_*.c`, `gud_internal.h`, `drm/gud.h` | Linux kernel v6.12.107, `drivers/gpu/drm/gud/`, `include/drm/gud.h` | Copyright 2020 Noralf Trønnes | none (`Makefile` is this project's) |

The rest of the firmware (`main/`, `bootloader_components/`, `tools/usb_flash.py`) is a port of
[Ponywka/gud-esp32s3](https://github.com/Ponywka/gud-esp32s3), Copyright (c) 2026 Vladimir Avtsenov, MIT.

Board documentation: [LCDWiki 2.8inch ESP32-S3 Display](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display).

## MIT License

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
