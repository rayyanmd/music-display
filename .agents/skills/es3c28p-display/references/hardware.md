# Hardware reference: ES3C28P / ES3N28P

Sources: lcdwiki.com/2.8inch_ESP32-S3_Display, *ES3C28P&ES3N28P ESP-IDF Demo Instructions* (CR2025-MI6890), *Arduino Demo Instructions* (CR2025-MI6140), and the schematic `2.8inch_ESP32-S3_Display_Schematic.pdf` (2025-06-11).

## Contents
1. SoC and memory
2. Display
3. Touch
4. Full GPIO table
5. Audio path
6. Power, battery, charging
7. Connectors and buttons
8. Vendor LCD init sequence
9. Vendor demos and downloads

## 1. SoC and memory
- ESP32-S3R8 (dual-core Xtensa LX7 at 240 MHz, 512 KB SRAM, **8 MB octal PSRAM in-package**)
- External 16 MB SPI flash (25VQ128), so the module behaves like "N16R8"
- Wi-Fi 2.4 GHz b/g/n, Bluetooth 5 LE. Uses an on-board SMD antenna.
- Octal PSRAM occupies GPIO33–37. Those pins are not available and aren't broken out anyway.
- Over USB-C it enumerates as **Espressif USB JTAG_serial debug unit**, VID `0x303A`. On macOS the port is `/dev/cu.usbmodem*`. No USB-UART bridge chip or driver is needed.

## 2. Display
| Item | Value |
|---|---|
| Panel | 2.8" IPS TFT, 240(RGB)×320, all viewing angles |
| Driver IC | ILI9341V |
| Interface | 4-wire SPI (CS, DC, SCK, MOSI, plus MISO for reads) |
| Color | RGB565 (65K) typical; RGB666 (262K) max |
| Active area | 43.20 × 57.60 mm |
| Backlight | 4 white LEDs, ~79 mA, driven via a BSS138 from GPIO45 (high = on). 280 cd/m² typical (230 with the touch layer). |

Required init details: **inversion ON (0x21)**, **MADCTL 0x08 (BGR)**, **COLMOD 0x55 (16-bit)**. Native orientation is portrait 240 wide × 320 tall.

Rotation via MADCTL (from the vendor `LCD_direction`, all with BGR = bit 3):
- 0: `0x08` portrait (240×320)
- 1: `0x08|0x40|0x20 = 0x68` landscape (320×240)
- 2: `0x08|0x40|0x80 = 0xC8` portrait, flipped
- 3: `0x08|0x80|0x20 = 0xA8` landscape, flipped

SPI clock: 40 MHz is reliable for writes. The vendor TFT_eSPI setup uses 27–40 MHz. 80 MHz often works for writes on short traces but isn't guaranteed. Keep reads at 16 MHz or lower.

## 3. Touch (ES3C28P only)
- FT6336G capacitive controller, I2C address **0x38**, up to 400 kHz
- SDA GPIO16, SCL GPIO15 (4.7 kΩ pull-ups to 3.3 V on board), RST GPIO18 (low = reset), INT GPIO17 (low while touched)
- The register map is compatible with FT5x06/FT6x06 drivers. Reports up to 2 points in native portrait coordinates, 0–239 × 0–319.

## 4. Full GPIO table
| GPIO | Function | Notes |
|---|---|---|
| 0 | BOOT key | 10 k pull-up. Hold at power-on/reset for download mode. A normal input afterwards. |
| 1 | AUDIO_EN | To the amp's SHUTDOWN pin. **Low = amp on.** |
| 2 | Expansion header | free |
| 3 | Expansion header | free (strapping pin JTAG-sel; safe as GPIO) |
| 4 | I2S MCLK | to ES8311 |
| 5 | I2S BCLK (SCLK) | |
| 6 | I2S DIN (ES8311 ASDOUT, i.e. mic data into the ESP32) | |
| 7 | I2S LRCK (WS) | |
| 8 | I2S DOUT (ES8311 DSDIN, i.e. playback data) | |
| 9 | BAT_ADC | ADC1_CH8, 200k/200k divider |
| 10 | LCD CS | |
| 11 | LCD MOSI | |
| 12 | LCD SCK | |
| 13 | LCD MISO | |
| 14 | Expansion header | free |
| 15 | I2C SCL | touch, codec and I2C header |
| 16 | I2C SDA | touch, codec and I2C header |
| 17 | Touch INT | 10 k pull-up |
| 18 | Touch RST | 10 k pull-up |
| 19 / 20 | USB D- / D+ | native USB |
| 21 | Expansion header | free |
| 38 | SD CLK | |
| 39 | SD D0 | 10 k pull-up |
| 40 | SD CMD | 10 k pull-up |
| 41 | SD D1 | 10 k pull-up |
| 42 | WS2812B data | single RGB pixel |
| 43 / 44 | UART0 TX / RX | 4-pin header (with 5 V and GND) |
| 45 | LCD backlight | high = on; strapping pin (VDD_SPI voltage), pulled down on board |
| 46 | LCD DC/RS | strapping pin (boot mode / ROM log), pulled down on board |
| 47 | SD D3 | |
| 48 | SD D2 | |
| EN (CHIP_PU) | RESET key and LCD RST | low = reset of the chip and the LCD together |

Free for your own use: **2, 3, 14, 21** (header). Pins **15/16** can be used for more I2C devices on the I2C header, but avoid addresses 0x18 and 0x38. If the speaker isn't used, GPIO1 can be repurposed, but leaving it high keeps the amp off.

## 5. Audio path
- **ES8311** mono codec, I2C address **0x18** (CE pin low). It needs MCLK (GPIO4), typically 256 × fs.
- Playback: I2S DOUT (GPIO8) → ES8311 DAC → OUTP/OUTN → **SC8002B** class-AB amp (the wiki lists FM8002E; the schematic shows SC8002B) → 2-pin 1.25 mm SPEAKER connector. Max is 1.5 W into 8 Ω or 2 W into 4 Ω. The amp runs from 5 V, so it is weak or silent on battery alone.
- Capture: LMA2718B381 analog MEMS mic → ES8311 MIC1P/N → ADC → I2S DIN (GPIO6).
- The codec has its own 3.3 V LDO (ME6217) and a separate analog ground.

## 6. Power, battery, charging
- USB-C 5 V input. A ME6217C33 LDO supplies 3.3 V (3.0–3.6 V operating).
- Battery: 1.25 mm 2-pin "BAT" socket for a single-cell 3.7 V LiPo. **Check the polarity**, since these connectors are not standardised.
- Charger: TP4054, about 290 mA actual (500 mA max), terminating at 4.24 V. The charge input range is 4.2–6.5 V.
- Power path: an SL2305 P-MOSFET switches from battery to VBUS when USB is present.
- Battery sense: `V_bat = 2 × V_adc` (R14 = R15 = 200 kΩ). Read with calibrated mV (`analogReadMilliVolts(9)` or `adc_cali`) and multiply by 2. Rough LiPo mapping: 4.20 V = 100 %, 3.70 V ≈ 50 %, 3.30 V = empty.
- Current draw: about 140 mA with the display only (0.7 W). About 560 mA with display, speaker and charging (2.8 W).

## 7. Connectors and buttons (all 1.25 mm pitch)
- **UART** 4P: 5V, GND, TXD0 (43), RXD0 (44). For an external USB-UART adapter.
- **Expanded IO** 4P: IO2, IO3, IO14, IO21
- **I2C** 4P: 3V3, GND, SDA (16), SCL (15). Shared with touch and codec.
- **SPEAKER** 2P, **BAT** 2P
- **RESET** key (EN), **BOOT** key (IO0)
- microSD slot (SDIO 4-bit, pull-ups fitted)

## 8. Vendor LCD init sequence (ILI9341V IPS)
From `ILI9341V_Init.txt`. Use it if writing a raw driver, or to sanity-check a library's init:
```
CF: 00 C1 30        ED: 64 03 12 81     E8: 85 00 78
CB: 39 2C 00 34 02  F7: 20              EA: 00 00
C0: 13 (VRH)        C1: 13              C5: 22 35          C7: BD
21 (INVON)          36: 08 (MADCTL BGR) B6: 0A A2          3A: 55 (RGB565)
F6: 01 30           B1: 00 1B           F2: 00             26: 01
E0: 0F 35 31 0B 0E 06 49 A7 33 07 0F 03 0C 0A 00
E1: 00 0A 0F 04 11 08 36 58 4D 07 10 0C 32 34 0F
11 (sleep out), wait 120 ms, 29 (display on)
```
The hardware reset is just the chip reset, so start with a software reset (`0x01`, wait 120 ms).

## 9. Vendor demos and downloads
Arduino demos (Arduino IDE 2.3.4, esp32 core 3.2.0): 01_Simple_test (no libraries, fills and rectangles), 02 colligate test, 03 graphics, 04 scroll/rotation, 05 SD JPG, 06 RGB LED, 07 flash JPG via DMA, 08 LVGL demos (widgets, benchmark, music…), 09 key interrupt, 10 UART, 11 RTC, 12 timer, 13 battery voltage, 14 backlight PWM, 15 RGB LED touch, 16 music (MP3 from SD via ESP32-audioI2S), 17 echo (mic → speaker), 18–24 Wi-Fi (scan, AP, SmartConfig, STA, TCP client/server, UDP), 25–26 BLE (the server demo needs core 2.0.x), 27 weather clock desktop, 28 phone dialer, 29 touch pen, 30 AI chat (Doubao + Baidu ASR). The board also supports the "Xiaozhi" AI voice-chat firmware.

ESP-IDF demo: `2.8inch_ESP32-S3_LVGL` (IDF 5.4.1, LVGL 8.4.0). The vendor notes that the project path must be ASCII-only.

Downloads:
- Full data pack: linked from the wiki (123pan and Baidu mirrors). Contains the demos, the replaced TFT_eSPI/LVGL files and tools.
- Schematic: https://www.lcdwiki.com/res/ES3C28P/2.8inch_ESP32-S3_Display_Schematic.pdf
- Init code: https://www.lcdwiki.com/res/ES3C28P/ILI9341V_Init.txt
- Datasheets: ILI9341V, FT6336G, ES8311, TP4054, WS2812B (all linked from the wiki page)
