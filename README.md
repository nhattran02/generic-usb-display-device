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
| `main/gud/gud_driver.c` | TinyUSB class driver: vendor control + bulk OUT, streaming LZ4 decoder, rectangles into the framebuffer, `write_buffer` hook |
| `main/usb_glue.c` | descriptors (HID + GUD), touch HID reports, reboot on a USB request, stack on core 1 |
| `main/lcd.c` | ILI9341V over SPI (40 MHz): PSRAM shadow framebuffer, damaged rectangles flushed via 2 DMA bounce buffers |
| `main/board.c` | I2C bus, backlight PWM on IO45 |
| `main/touch.c` | FT6336G over I2C, polled every 5 ms, rotated like the picture |
| `main/screen.c`, `ui.c` | `<NO SIGNAL>` screen, sleep on timeout, reaction to signal loss, DPMS = backlight off + panel sleep |
| `bootloader_components/usb_dl_window` | USB-Serial-JTAG download window in the bootloader (flashing without buttons) |
| `components/tinyusb` | TinyUSB (espressif/tinyusb 0.21.x), device + HID + DWC2 only; no component registry needed |
| `tools/usb_flash.py` | flashing over the display's own cable from a Linux host |
| `host/gud-module` | building the `gud` driver for kernels where it is disabled (Debian) |

### Differences from the Waveshare original

The Waveshare board has an RGB panel that scans a PSRAM framebuffer continuously. The ES3C28P has an
ILI9341V with its own GRAM on SPI, so:

- the framebuffer is only a shadow copy; when a rectangle from the host is complete, `gud_driver.c` calls
  `write_buffer` -> `lcd_flush()`, which sends just that rectangle to the panel;
- GUD RGB565 is little endian and the ILI9341 wants big endian: bytes are swapped while copying into the DMA
  bounce buffers;
- rotation is done by the panel (MADCTL), so the host, the framebuffer and the touch coordinates all share one
  orientation and nothing is rotated in software;
- no CH422G expander: the touch reset is IO18, the LCD reset is tied to EN (a software reset is sent at boot).

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
| `BOARD_LCD_INVERT` | `0` | flip if the picture looks like a negative |
| `BOARD_LCD_SPI_HZ` | 40 MHz | 80 MHz usually works too (IO_MUX pins) |
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

## Screen and backlight

- At power-up: `<NO SIGNAL>`; if no picture arrives within 60 s, the screen goes dark.
- A picture from the host turns the screen on, also from sleep. If the host goes away after it has shown a
  picture, the screen goes dark immediately.
- Host DPMS turns off the backlight and puts the ILI9341 into sleep mode.
- Brightness 0-100 is saved to NVS 2 s after the last change (0 is not saved).

## Notes

- The ESP32-S3's native USB is Full-Speed only (~1 MB/s in practice); LZ4 helps. A full 320x240 RGB565 frame is
  150 KB, i.e. ~30 ms on 40 MHz SPI, so the panel is faster than the USB link.
- The ESP32-S3's OUT endpoint packet counter is 7 bits wide: transfers are split at 127 packets.

## License

MIT, see [`LICENSE`](LICENSE). Third-party code (TinyUSB, the GUD protocol from gud-pico, the Linux `gud`
driver) keeps its own license and copyright notices.
