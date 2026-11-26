---
name: es3c28p-display
description: Board support for the LCDwiki 2.8" ESP32-S3 display module (ES3C28P with capacitive touch / ES3N28P without): ILI9341V 240x320 IPS over SPI, FT6336G touch, ES8311 audio codec + speaker amp, mic, microSD (SDIO), WS2812 RGB LED, battery ADC, ESP32-S3 N16R8. Use this skill whenever the user is writing, building, flashing or debugging firmware for this board, or for any "ESP32-S3 with 2.8 inch screen", "ES3C28P", "lcdwiki ESP32-S3", ILI9341+FT6336 on ESP32-S3, or a project (e.g. a music/now-playing display) that runs on it, even if they don't name the board model. Also use it when the board shows up as an Espressif "USB JTAG_serial debug unit" (/dev/cu.usbmodem*) and the user wants to put a UI, touch, audio, SD or LED code on it. Covers pin map, correct display/touch init, Arduino (arduino-cli / PlatformIO + LovyanGFX or TFT_eSPI), ESP-IDF (esp_lcd + LVGL), audio, SD, battery and flashing on macOS.
---

# LCDwiki 2.8" ESP32-S3 Display (ES3C28P / ES3N28P)

An all-in-one dev board: ESP32-S3 (N16R8: 16 MB flash, 8 MB octal PSRAM) with a 2.8" 240×320 IPS panel on the back. **ES3C28P** has capacitive touch; **ES3N28P** is the same board without touch. Source docs: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display

Hardware facts in this skill were taken from the vendor wiki, the vendor demo PDFs and the schematic (2025-06-11). Where the vendor docs disagree, the schematic wins, and the difference is noted.

## Pin map (the part you'll need most)

| Function | GPIO | Notes |
|---|---|---|
| LCD SCK / MOSI / MISO | 12 / 11 / 13 | SPI2 (FSPI) host |
| LCD CS / DC(RS) | 10 / 46 | DC high = data |
| LCD RST | — | Tied to the chip's EN/CHIP_PU, so there is **no reset GPIO**: use `-1` and rely on a software reset |
| LCD backlight | 45 | Active **high**, PWM-able (drives a BSS138) |
| Touch SDA / SCL | 16 / 15 | FT6336G, I2C addr **0x38** |
| Touch RST / INT | 18 / 17 | INT goes low on touch |
| Audio codec I2C | 16 / 15 | ES8311 at **0x18**, on the *same* bus as touch |
| I2S MCLK / BCLK / LRCK | 4 / 5 / 7 | |
| I2S DOUT (to codec) / DIN (from mic) | 8 / 6 | |
| Speaker amp enable | 1 | **Low = on**, high = shutdown (SC8002B amp) |
| microSD (SDIO 4-bit) CLK / CMD | 38 / 40 | |
| microSD D0 / D1 / D2 / D3 | 39 / 41 / 48 / 47 | |
| WS2812B RGB LED | 42 | One pixel, powered from 5 V |
| Battery sense | 9 | ADC1_CH8, 200k/200k divider, so V_bat = 2 × V_pin |
| BOOT key | 0 | Usable as a normal button after boot |
| UART0 TX / RX | 43 / 44 | 4-pin header |
| Free IO header | 2, 3, 14, 21 | |
| I2C header | 16 / 15 | Shares the touch/codec bus |
| USB D- / D+ | 19 / 20 | Native USB Serial/JTAG |

Full tables, electrical specs and connector details are in `references/hardware.md`.

## Gotchas that waste hours

1. **Colors inverted or wrong:** the panel is IPS. The vendor init sends `INVON (0x21)` and `MADCTL = 0x08` (BGR). So turn **inversion on** and use **BGR** order. In LovyanGFX: `invert = true`, `rgb_order = false`. In TFT_eSPI: `#define TFT_INVERSION_ON` and `TFT_RGB_ORDER TFT_BGR`. In esp_lcd: `esp_lcd_panel_invert_color(panel, true)` and `LCD_RGB_ELEMENT_ORDER_BGR`.
2. **No LCD reset pin.** Configs that default to a reset GPIO will toggle a random pin. Set the reset pin to `-1`.
3. **One I2C bus, three users:** touch (0x38), the ES8311 codec (0x18) and the external I2C header all sit on SDA 16 / SCL 15. Create the bus once and share it. Two drivers each calling `Wire.begin()` with different pins, or two ESP-IDF masters on the same pins, causes intermittent touch or codec failures.
4. **No serial output over USB:** the board uses native USB (Espressif "USB JTAG_serial debug unit", `/dev/cu.usbmodem*`). Under Arduino you need **USB CDC On Boot = Enabled**, otherwise `Serial` goes to UART0 (GPIO43/44) and nothing appears on the USB port. Under ESP-IDF, set the console to USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`).
5. **PSRAM is octal (OPI).** Selecting QSPI PSRAM crashes at boot or reports 0 bytes of PSRAM. Select OPI / `qio_opi` and a 16 MB flash size.
6. **The backlight is off until you drive GPIO45 high.** A dark screen often means the backlight is off, not that the display is broken. GPIO45 and GPIO46 are strapping pins with on-board pull-downs that are safe at boot. Don't add external pull-ups to them.
7. **Silent speaker:** the amp needs GPIO1 **low**, *and* the ES8311 must be configured over I2C (power-up, DAC on, volume). Raw I2S output alone gives no sound. MCLK on GPIO4 is required by the codec.
8. **Touch axes don't match the screen after rotation:** the FT6336 reports in native portrait coordinates (0–239, 0–319). Rotate or swap touch coordinates to match the display rotation. LovyanGFX does this automatically when touch is attached to the LGFX device.
9. **LVGL version:** the vendor Arduino libraries and `lv_conf.h` target LVGL **8.3/8.4** and say "v9 cannot be used". That limit applies to *their* port files. With your own port, v9 works fine (`lv_display_create` / `lv_indev_create`). Match the API to the installed major version and don't mix them.
10. **Build fails with "sketch too big":** the vendor examples need the **Huge APP (3MB No OTA/1MB SPIFFS)** partition scheme. With 16 MB flash, a 16 MB layout (`default_16MB.csv`, or `app3M_fat9M_16MB`) gives OTA room as well.

## Choosing a stack

- **Arduino + LovyanGFX (recommended default for Arduino):** it supports the ESP32-S3 SPI/DMA cleanly, includes an FT6336 (FT5x06) touch driver, and needs no patched library files. A ready config is in `assets/LGFX_ES3C28P.hpp`.
- **Arduino + TFT_eSPI:** this is what the vendor demos use. On the ESP32-S3 with Arduino core 3.x it needs patched files (a custom `User_Setup.h`, the vendor's `ILI9341_Init.h`, and a DMA fix in `Processor/TFT_eSPI_ESP32_S3.c`). Use it only to run vendor examples as-is. `references/arduino.md` has the exact setup.
- **ESP-IDF (vendor demo: IDF 5.4.1 + LVGL 8.4):** use `esp_lcd` with the `esp_lcd_ili9341` and `esp_lcd_touch_ft5x06` components, `esp_codec_dev` for the ES8311, and `esp_lvgl_port`. See `references/esp-idf.md`.
- **MicroPython:** the vendor provides demos, and the same pins apply. Use an ESP32_GENERIC_S3 **SPIRAM_OCT** firmware build.

When the user has no preference, use **PlatformIO + Arduino + LovyanGFX** (+ LVGL if they need widgets). It is scriptable from the terminal on macOS, and `assets/platformio/` is a working starter project.

## Workflow

1. **Find the board.** Run `bash scripts/find_board.sh`. It lists Espressif USB devices and the `/dev/cu.usbmodem*` port, and runs `esptool chip_id` if esptool is installed. Expect "ESP32-S3", 16 MB flash and embedded PSRAM 8 MB.
2. **Scaffold.** Copy `assets/platformio/` into the project (it contains `platformio.ini`, `src/main.cpp` and `include/LGFX_ES3C28P.hpp`). For arduino-cli, use the FQBN in `references/arduino.md`.
3. **Prove the display first.** Fill the screen with red, green and blue, then draw text. This catches color-order, inversion and backlight problems before you build any UI on top.
4. **Add peripherals one at a time:** touch, then SD, then audio, then networking. Each shares a resource (the I2C bus, PSRAM or DMA), so adding them one at a time keeps faults attributable.
5. **Build, flash and monitor:**
   - PlatformIO: `pio run -t upload && pio device monitor` (115200 baud; the port is auto-detected).
   - arduino-cli: `arduino-cli compile --fqbn <FQBN> . && arduino-cli upload -p /dev/cu.usbmodem101 --fqbn <FQBN> .`
   - ESP-IDF: `idf.py set-target esp32s3 && idf.py -p /dev/cu.usbmodem101 flash monitor`
6. **If upload fails** ("Failed to connect" or the port disappears), put the board in download mode: hold **BOOT**, tap **RESET**, release BOOT, then upload again. After flashing in this mode, press RESET to run the new firmware.

## Reference files

- `references/hardware.md`: full pin tables, I2C addresses, power and battery specs, connectors, display init sequence, vendor demo list and download links.
- `references/arduino.md`: board settings and FQBN, PlatformIO config, LovyanGFX and TFT_eSPI setup, touch, SD_MMC, RGB LED, battery, backlight PWM, LVGL glue, audio with ES8311.
- `references/esp-idf.md`: esp_lcd panel and touch bring-up, I2C bus sharing, ES8311 via esp_codec_dev, SDMMC, sdkconfig defaults, LVGL port.
- `assets/LGFX_ES3C28P.hpp`: a drop-in LovyanGFX device class for this board.
- `assets/platformio/`: a starter project (display test, touch drawing, backlight, RGB LED, battery readout).
- `scripts/find_board.sh`: board detection on macOS and Linux.
