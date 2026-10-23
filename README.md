# gud-es3c28p

Firmware that turns the **LCDWiki ES3C28P** (2.8" ESP32-S3 display, ILI9341V 240x320 SPI, FT6336G capacitive
touch, ESP32-S3-WROOM-1-N16R8) into a USB display for Linux. Port of
[Ponywka/gud-esp32s3](https://github.com/Ponywka/gud-esp32s3) (Waveshare ESP32-S3-Touch-LCD-4.3).

On the board's Type-C port (ESP32-S3 native USB) the host sees:

- **GUD (Generic USB Display)**, `1d50:614d`: a DRM card of the `gud` driver, 320x240 (default), RGB565, LZ4,
  adjustable backlight (`/sys/class/backlight/cardN-USB-1-backlight`);
- **HID multitouch** (FT6336G, 2 contacts): `hid-multitouch`, a regular `/dev/input/eventN`.

## What's inside

| Path | What it does |
|---|---|
| `main/gud/gud.c`, `gud.h` | GUD protocol, device side (from [notro/gud-pico](https://github.com/notro/gud-pico)) |
| `main/gud/gud_driver.c` | TinyUSB class driver: vendor control + bulk OUT into 2 receive buffers, render task, flow control, performance counters |
| `main/gud/gud_lz4.c` | streaming LZ4 block decoder (64 KB ring, block copies), no dependencies |
| `main/usb_glue.c` | descriptors (HID + GUD), touch HID reports, reboot on a USB request, stack on core 1 |
| `main/lcd.c` | ILI9341V over SPI at 40 MHz: pixel streaming through 2 DMA bounce buffers, framebuffer for the firmware's own screens |
| `main/board.c` | I2C bus, backlight PWM on IO45 |
| `main/touch.c` | FT6336G over I2C, polled every 5 ms, rotated like the picture |
| `main/screen.c`, `ui.c` | `<NO SIGNAL>` screen, sleep on timeout, reaction to signal loss, DPMS = backlight off + panel sleep |
| `bootloader_components/usb_dl_window` | USB-Serial-JTAG download window in the bootloader (flashing without buttons) |
| `components/tinyusb` | TinyUSB (espressif/tinyusb 0.21.x), device + HID + DWC2 only; no component registry needed |
| `tools/usb_flash.py` | flashing over the display's own cable from a Linux host |
| `tools/gud_test.c`, `gud_bench.c` | KMS test picture and throughput benchmark for the Linux host (see "Performance") |
| `tools/test/test_lz4.py` | host tests of the LZ4 decoder and the byte swap, compiled from the firmware sources |
| `host/gud-module` | building the `gud` driver for kernels where it is disabled (Debian) |

### Differences from the Waveshare original

The Waveshare board has an RGB panel that scans a PSRAM framebuffer continuously. The ES3C28P has an
ILI9341V with its own GRAM on SPI, so:

- pictures from the host are not kept on the ESP32 at all: rectangles are decoded and streamed straight into
  the panel's GRAM (see "How the picture gets to the screen");
- GUD RGB565 is little endian and the ILI9341 wants big endian: bytes are swapped while copying into the DMA
  bounce buffers;
- rotation is done by the panel (MADCTL), so the host, the framebuffer and the touch coordinates all share one
  orientation and nothing is rotated in software;
- no CH422G expander: the touch reset is IO18, the LCD reset is tied to EN (a software reset is sent at boot).

### How the picture gets to the screen

The host sends only the rectangles that changed, each LZ4-compressed, in one USB transfer. Three stages work
at the same time:

```
USB task (core 1)        receive buffer A <- rectangle N+1 (bulk OUT)
render task (core 0)     receive buffer B -> LZ4 -> 64 KB ring (SRAM) -> byte swap -> DMA bounce buffer 1/2
SPI DMA                  bounce buffer -> ILI9341 GRAM (40 MHz)
```

- **USB and drawing overlap.** There are two receive buffers in PSRAM. The USB task only receives; as soon as
  a rectangle is complete it is queued for the render task and the next one can come in. When both buffers are
  taken, SET_BUFFER is still accepted, but the bulk transfer is armed only once a buffer is free, and the USB
  controller NAKs the host meanwhile (standard flow control, no timeouts).
- **Decoding and SPI overlap.** The decoder hands its output over every 4 KB; it is byte-swapped into one of
  two 16 KB DMA buffers, and while one is on the wire the other is filled. The first transfer of a rectangle
  is RAMWR, the following ones RAMWRC, so nothing depends on CS staying low between transfers.
- **Block copies in LZ4.** Literals and matches are copied with `memcpy`; repeating runs (a match longer than
  its offset, e.g. a flat colour) are copied with a doubling distance, so a whole screen of one colour costs a
  few dozen `memcpy` calls instead of 150 000 byte copies.
## Pins (LCDWiki)

| Function | GPIO |
|---|---|
| LCD CS / DC / SCK / MOSI / MISO | 10 / 46 / 12 / 11 / 13 |
| LCD backlight (high = on) | 45 |
| Touch SDA / SCL / RST / INT | 16 / 15 / 18 / 17 |
| USB D- / D+ (Type-C) | 19 / 20 |
| UART0 TX / RX (log console) | 43 / 44 |

## Configuration (`main/board.h`)

| Macro | Default | Meaning |
|---|---|---|
| `BOARD_LCD_ROTATION` | `1` | 0 = portrait 240x320, 1 = landscape 320x240, 2/3 = the same upside down. Also `idf.py -DBOARD_LCD_ROTATION=0 build` |
| `BOARD_LCD_BGR` | `1` | flip if red and blue are swapped |
| `BOARD_LCD_INVERT` | `1` | the ES3C28P panel needs display inversion; flip if the picture looks like a negative |
| `BOARD_LCD_SPI_HZ` | 40 MHz | 80 MHz garbles the picture on this panel; `idf.py -DBOARD_LCD_SPI_HZ=80000000 build` to try another one |
| `BOARD_BL_PWM_GPIO` | `45` | backlight PWM; `-1` = no brightness control |
| `BOARD_BL_DEFAULT_PERCENT` | `50` | brightness after the first power-up |

If touches land mirrored, use `TOUCH_SWAP_XY` / `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y` in `main/touch.c` (they act
on the raw portrait coordinates, before the rotation). For panel debugging there is `lcd_draw_test_pattern()`
(colour bars white/yellow/cyan/green/magenta/red/blue/black, grey ramp, 1 px border).

## Building and flashing

ESP-IDF v6.1 (the code only uses APIs that also exist in v5.3+).

```sh
idf.py set-target esp32s3     # once; delete sdkconfig after editing sdkconfig.defaults
idf.py build
```

First flashing: hold **BOOT**, press **RST**, release BOOT, then

```sh
idf.py -p COMx flash
```

The firmware takes the native USB port for itself (GUD + HID), so after that the port no longer shows up as a
serial port while the application runs. Logs go to UART0 (IO43/IO44, external USB-UART adapter).

Later updates, either:

- BOOT + RST again, then `idf.py -p COMx flash`; or
- right after a reset the bootloader hands the port to USB-Serial-JTAG for 1.5 s (`303a:1001`): from a Linux host
  `cd build && python ../tools/usb_flash.py` (needs `esptool` and `pyserial`) sends the reboot request
  (vendor request `0xB0`) and catches that window.

## Host

```sh
sudo modprobe gud
ls /sys/class/drm | grep USB          # cardN-USB-1
```

Debian (6.12+) has `CONFIG_DRM_GUD` disabled: build the module from `host/gud-module` (`make`, then
`sudo modprobe -a drm_kms_helper drm_shmem_helper lz4_compress && sudo insmod gud.ko`).

With the default landscape orientation no rotation is needed on the host. In Xorg with `fbdev`:

```
Section "Device"
    Identifier "gud-fbdev"
    Driver     "fbdev"
    Option     "fbdev"  "/dev/fb0"
EndSection
```

Under Wayland/KDE bind the touchscreen to the `USB-1` output (System Settings -> Touchscreen).

## Performance

`tools/gud_bench.c` measures on the device (the Linux driver merges updates the device can't take, so frames
counted on the host mean nothing). On the Linux host:

```sh
sudo apt install gcc libdrm-dev
gcc -O2 gud_bench.c -o gud_bench $(pkg-config --cflags --libs libdrm)
sudo systemctl stop gdm3          # the desktop must not own the display during the test
sudo ./gud_bench                  # solid, ui, gradient, noise, square; 8 s each
ffmpeg -i clip.mp4 -vf scale=320:240 -pix_fmt bgr0 -f rawvideo - 2>/dev/null | sudo ./gud_bench video
```

`device fps` is what was drawn. Per frame: `usb` = bulk transfer time, `render` = decode + SPI (runs alongside
USB), `stall` = waiting for a free receive buffer. `render busy` near 100 % means the panel side sets the pace,
a low value means USB does.

Measured through a VirtualBox VM, 40 MHz SPI. v1 = decode and SPI one after the other in the USB task,
v2 = the pipeline above:

| Test | v1 | v2 | What limits v2 |
|---|---|---|---|
| solid / ui | 17 fps | **30.4 fps** | SPI: 150 KB at 40 MHz = 31 ms (render busy 98 %) |
| gradient | 10 fps | **16.8 fps** | USB: 37 KB compressed per frame, 44 ms |
| noise | 5 fps | 5 fps | USB: incompressible, ~750 KB/s |
| square 40x40 | 92 fps | 90 fps | USB latency per transfer in the VM (~6 ms) |

Run the host tests of the decoder and byte swap with `pip install lz4 ziglang` and
`python tools/test/test_lz4.py` (any `cc`/`gcc`/`clang` on PATH works instead of ziglang).

## Screen and backlight

- At power-up: `<NO SIGNAL>`; if no picture arrives within 60 s, the screen goes dark.
- A picture from the host turns the screen on, also from sleep. If the host goes away after it has shown a
  picture, the screen goes dark immediately.
- Host DPMS turns off the backlight and puts the ILI9341 into sleep mode.
- Brightness 0-100 is saved to NVS 2 s after the last change (0 is not saved).

## Notes

- The ESP32-S3's native USB is Full-Speed only (~1 MB/s in practice); LZ4 helps. A full 320x240 RGB565 frame is
  150 KB: ~31 ms on 40 MHz SPI, ~150 ms over USB when it doesn't compress, so for video and noise USB is the limit.
- A VM adds latency to every USB transfer (VirtualBox: ~6 ms even for tiny ones); a Linux machine with the
  display plugged in directly is faster for small updates.
- The ESP32-S3's OUT endpoint packet counter is 7 bits wide: transfers are split at 127 packets.

## License

MIT, see [`LICENSE`](LICENSE). Third-party code (TinyUSB, the GUD protocol from gud-pico, the Linux `gud`
driver) keeps its own license and copyright notices.
