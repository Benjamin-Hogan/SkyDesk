# 02 — Hardware: the CYD

> Audience: agents writing firmware. Read before touching pins, drivers, SPI,
> or memory-heavy code. These settings are copied from a sibling project
> (`../minecraft-server-dash-cyd`) where they were proven on this exact board.

## Board
**ESP32-2432S028R** ("Cheap Yellow Display"), **CYD2USB** revision (two USB
ports: micro-USB + USB-C).

| Part | Detail |
|---|---|
| MCU | ESP32-WROOM-32, dual core 240 MHz, **520 KB SRAM, no PSRAM**, 4 MB flash |
| Display | 2.8" 240×320 TFT. On CYD2USB this is an **ST7789 run in ILI9341-compatible mode with inversion** |
| Touch | XPT2046 resistive, on its **own** SPI bus |
| Extras | RGB LED (active-LOW), LDR light sensor, microSD slot, speaker header (unused) |

## Display wiring (TFT_eSPI, HSPI) — configured entirely via `build_flags`
| Signal | GPIO |
|---|---|
| TFT_MISO | 12 |
| TFT_MOSI | 13 |
| TFT_SCLK | 14 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_RST | -1 (tied to EN) |
| TFT_BL | 21 (backlight, PWM-able) |

Required flags (do **not** remove): `ILI9341_2_DRIVER`, `TFT_INVERSION_ON`,
`TFT_RGB_ORDER=TFT_BGR`, `USER_SETUP_LOADED`. Without them colors are wrong.
If a user has the older single-USB CYD (true ILI9341), swap to
`ILI9341_DRIVER` and remove `TFT_INVERSION_ON` — document it, don't guess.

Orientation: landscape, `setRotation(1)` → **320 × 240** logical pixels.

## Touch wiring (XPT2046 via `XPT2046_Touchscreen`, own SPI bus)
| Signal | GPIO |
|---|---|
| T_CLK | 25 |
| T_MOSI | 32 |
| T_MISO | 39 |
| T_CS | 33 |
| T_IRQ | 36 (**do not use**: construct the driver *without* IRQ; IRQ mode silently fails on many units) |

Touch is unit-to-unit variable; calibration is stored in NVS (`Preferences`).

## Other pins
| Use | GPIO | Notes |
|---|---|---|
| LED R / G / B | 4 / 16 / 17 | Active-LOW. Used for a subtle "plane overhead" pulse |
| LDR | 34 | Analog, optional auto-brightness |
| SD (VSPI) | CS 5, SCK 18, MISO 19, MOSI 23 | Not used in v1 |

## Hard constraints for firmware
1. **RAM**: ~300 KB free heap after WiFi+TLS. A full-screen 16-bit sprite is
   320×240×2 = **150 KB** — do **not** allocate one. Use partial sprites
   (e.g. the sky-pointer 120×120×2 = 28 KB) or 8-bit/4-bit sprites.
2. **TLS** costs ~40–50 KB per concurrent connection. Make HTTPS requests
   **sequentially**, never in parallel.
3. **JSON**: stream-parse with ArduinoJson filters. Never `getString()` a
   whole response.
4. **Non-blocking UI**: networking runs in a FreeRTOS task pinned to core 0;
   UI + touch run in `loop()` on core 1. No `delay()` in `loop()`.
5. **RGB565**: gradients band visibly. Designs use flat fills; any gradient
   must be ≤ 3 steps.
6. **Fonts**: TFT_eSPI built-ins + Adafruit GFX FreeFonts (see `06-ui-spec.md`
   §Typography). Smooth (anti-aliased) VLW fonts need SPIFFS/LittleFS and are
   out of scope for v1.

## Power
USB 5 V. Backlight at full ~110 mA; dim at night via PWM (LEDC) on GPIO 21.
