# Arduino / PlatformIO on the ES3C28P

## Contents
1. Board settings (Arduino IDE, arduino-cli, PlatformIO)
2. Display with LovyanGFX
3. Display with TFT_eSPI (vendor way)
4. Touch, including sharing the I2C bus with the codec
5. Backlight, RGB LED, BOOT key, battery
6. microSD (SD_MMC, 4-bit)
7. Audio (ES8311 + amp)
8. LVGL glue (v8 and v9)
9. Troubleshooting

## 1. Board settings

Arduino IDE (vendor-tested: IDE 2.3.4, esp32 core 3.2.0), **Tools** menu:
| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | **Enabled** |
| Flash Size | **16MB (128Mb)** |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS), or 16M Flash (3MB APP/9.9MB FATFS) |
| PSRAM | **OPI PSRAM** |
| Upload Mode | UART0 / Hardware CDC |
| USB Mode | Hardware CDC and JTAG |
| Flash Mode | QIO 80MHz |

arduino-cli FQBN:
```
esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app,USBMode=hwcdc,UploadSpeed=921600
```
For OTA room, use `PartitionScheme=app3M_fat9M_16MB`. List all options with `arduino-cli board details -b esp32:esp32:esp32s3`.

Setup on macOS:
```bash
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32
arduino-cli lib install LovyanGFX "Adafruit NeoPixel"
```

PlatformIO: see `assets/platformio/platformio.ini`. The key lines are `board = esp32-s3-devkitc-1`, `board_build.arduino.memory_type = qio_opi`, `board_upload.flash_size = 16MB`, `board_build.partitions = default_16MB.csv`, and `-DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`. Install PlatformIO with `pipx install platformio` or `brew install platformio`.

## 2. Display with LovyanGFX

Copy `assets/LGFX_ES3C28P.hpp` into the sketch or `include/` folder:
```cpp
#include "LGFX_ES3C28P.hpp"
LGFX tft;
void setup() {
  tft.init();
  tft.setRotation(1);        // 0/2 portrait 240x320, 1/3 landscape 320x240
  tft.setBrightness(200);    // PWM on GPIO45
  tft.fillScreen(TFT_BLACK);
}
```
- For smooth animation, draw into an `LGFX_Sprite` and `pushSprite()`. A full-screen 16-bit sprite is 150 KB. Use `sprite.setPsram(true)` before `createSprite` to place it in PSRAM, or keep smaller sprites in internal RAM, which is faster.
- JPEG and PNG decode is built in: `tft.drawJpgFile(SD_MMC, "/a.jpg", x, y)` and `tft.drawJpg(buf, len, x, y)`. This suits album art: fetch the JPEG over HTTP into PSRAM, then call `drawJpg`. Use the scale/clip arguments to fit 240×240 or smaller.
- Wrong colors: check `cfg.invert = true`. If red and blue are swapped, flip `cfg.rgb_order`.

## 3. Display with TFT_eSPI (only for running vendor demos)

The vendor demos patch the library. With a fresh TFT_eSPI from Library Manager:
1. Replace `TFT_eSPI/User_Setup.h` with the vendor's (from "Replaced files" in the data pack), or write this minimal one:
   ```cpp
   #define ILI9341_DRIVER
   #define TFT_WIDTH 240
   #define TFT_HEIGHT 320
   #define TFT_RGB_ORDER TFT_BGR
   #define TFT_INVERSION_ON
   #define TFT_MISO 13
   #define TFT_MOSI 11
   #define TFT_SCLK 12
   #define TFT_CS   10
   #define TFT_DC   46
   #define TFT_RST  -1
   #define TFT_BL   45
   #define TFT_BACKLIGHT_ON HIGH
   #define USE_FSPI_PORT
   #define SPI_FREQUENCY 40000000
   #define SPI_READ_FREQUENCY 16000000
   #define LOAD_GLCD
   #define LOAD_FONT2
   #define LOAD_FONT4
   #define LOAD_GFXFF
   #define SMOOTH_FONT
   ```
2. Replace `TFT_Drivers/ILI9341_Init.h` with the vendor's IPS init (the same sequence as in hardware.md §8).
3. In `Processor/TFT_eSPI_ESP32_S3.c`, change `dma_end_callback` to use `WRITE_PERI_REG(SPI_DMA_CONF_REG(SPI_DMA_CH_AUTO), 0b11);` instead of `SPI_DMA_CONF_REG(spi_host)`. Without this, DMA pushes hang on the S3.

Because these patches are lost on every library update, prefer LovyanGFX for new code.

## 4. Touch, including sharing the I2C bus

**Simple case (display and touch only):** let LovyanGFX own touch. `tft.getTouch(&x, &y)` returns coordinates already rotated to match `setRotation`.

**Touch plus audio codec:** both live on SDA16/SCL15. Two I2C controllers driving the same pins will collide. Pick one owner:
- Option A (simplest): remove the touch block from `LGFX_ES3C28P.hpp` (define `ES3C28P_NO_TOUCH`). Then use `Wire` for both touch and the codec:
  ```cpp
  #include <Wire.h>
  void touchBegin() {
    pinMode(18, OUTPUT); digitalWrite(18, LOW); delay(10); digitalWrite(18, HIGH); delay(300);
    pinMode(17, INPUT_PULLUP);
    Wire.begin(16, 15, 400000);
  }
  // returns true if touched; x,y in native portrait coords (0..239, 0..319)
  bool touchRead(int &x, int &y) {
    Wire.beginTransmission(0x38); Wire.write(0x02); if (Wire.endTransmission(false)) return false;
    if (Wire.requestFrom(0x38, 5) != 5) return false;
    uint8_t n = Wire.read() & 0x0F;
    uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    if (n == 0 || n > 2) return false;
    x = ((xh & 0x0F) << 8) | xl;  y = ((yh & 0x0F) << 8) | yl;
    return true;
  }
  // map to rotation 1 (landscape 320x240): sx = y; sy = 239 - x;  (verify on device, flip if mirrored)
  ```
- Option B: keep LovyanGFX touch on its I2C port and talk to the codec through LovyanGFX's I2C helpers on the same port, `lgfx::i2c::writeRegister8(1, 0x18, reg, val)`, instead of `Wire`.

## 5. Backlight, RGB LED, BOOT key, battery
```cpp
tft.setBrightness(0..255);                 // LovyanGFX PWM on GPIO45
// Without LovyanGFX: ledcAttach(45, 5000, 8); ledcWrite(45, duty);   (core 3.x API)

#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel rgb(1, 42, NEO_GRB + NEO_KHZ800);
rgb.begin(); rgb.setPixelColor(0, rgb.Color(0,0,255)); rgb.show();
// core 3.x also has rgbLedWrite(42, r, g, b) with no library needed

pinMode(0, INPUT_PULLUP);  // BOOT key, LOW when pressed

analogSetPinAttenuation(9, ADC_11db);
float vbat = analogReadMilliVolts(9) * 2 / 1000.0;   // 200k/200k divider
```
With only USB connected and no battery, the reading floats around the charger output (about 4.2 V) and is meaningless.

## 6. microSD (SDIO 4-bit via SD_MMC)
```cpp
#include <SD_MMC.h>
SD_MMC.setPins(38 /*CLK*/, 40 /*CMD*/, 39 /*D0*/, 41 /*D1*/, 48 /*D2*/, 47 /*D3*/);
if (!SD_MMC.begin("/sdcard", false /*1-bit mode? false = 4-bit*/)) Serial.println("SD mount failed");
```
Use FAT32 (cards up to 32 GB are safest). If you see 4-bit errors, retry with `SD_MMC.begin("/sdcard", true)` for 1-bit mode. In 1-bit mode only CLK, CMD and D0 are needed.

## 7. Audio (ES8311 + SC8002B amp)
Requirements: MCLK on GPIO4, the codec configured over I2C (address 0x18), and the amp enabled by **driving GPIO1 LOW**.

The vendor demos use pschatzmann's **arduino-audio-tools** and **arduino-audio-driver** (ES8311 support), or **ESP32-audioI2S** for MP3/stream playback. Typical setup with audio-driver/audio-tools:
```cpp
#include "AudioTools.h"
#include "AudioBoard.h"          // from arduino-audio-driver
DriverPins pins;
AudioBoard board(AudioDriverES8311, pins);
I2SCodecStream i2s(board);

void audioBegin() {
  pins.addI2C(PinFunction::CODEC, 15 /*SCL*/, 16 /*SDA*/, 0x18, 100000, Wire);
  pins.addI2S(PinFunction::CODEC, 4 /*MCLK*/, 5 /*BCLK*/, 7 /*WS*/, 8 /*DOUT*/, 6 /*DIN*/);
  pinMode(1, OUTPUT); digitalWrite(1, LOW);   // amp ON (active low) — do it yourself
  auto cfg = i2s.defaultConfig(RXTX_MODE);
  cfg.sample_rate = 44100; cfg.channels = 2; cfg.bits_per_sample = 16;
  i2s.begin(cfg);
  i2s.setVolume(0.6);
}
```
Check the `addI2C` and `addI2S` argument order against the installed library version, because it has changed between releases. Don't register GPIO1 as the library's PA pin unless the library supports active-low PA. Driving it manually is safer.

For MP3 from SD or HTTP streams, use ESP32-audioI2S: `audio.setPinout(5 /*BCLK*/, 7 /*LRC*/, 8 /*DOUT*/, 4 /*MCLK*/)`. The ES8311 still needs its I2C init first, so do that with audio-driver or a few register writes, then hand I2S to the player.

The amp runs from 5 V. On battery only, output is weak or absent.

## 8. LVGL glue
LVGL v8 (vendor default; `lv_conf.h` with `LV_COLOR_DEPTH 16` and `LV_COLOR_16_SWAP 0` when using LovyanGFX `pushImage`/`writePixels` with swap):
```cpp
static lv_disp_draw_buf_t buf; static lv_color_t *b1;
void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite(); tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixels((lgfx::rgb565_t *)&px->full, w * h); tft.endWrite();
  lv_disp_flush_ready(d);
}
void touch(lv_indev_drv_t *, lv_indev_data_t *data) {
  int32_t x, y;
  if (tft.getTouch(&x, &y)) { data->state = LV_INDEV_STATE_PR; data->point.x = x; data->point.y = y; }
  else data->state = LV_INDEV_STATE_REL;
}
// setup: lv_init(); b1 = (lv_color_t*)heap_caps_malloc(320*40*2, MALLOC_CAP_DMA);
// lv_disp_draw_buf_init(&buf, b1, NULL, 320*40); drv.hor_res=320; drv.ver_res=240; ...
// loop: lv_timer_handler(); and call lv_tick_inc(ms) or set LV_TICK_CUSTOM 1 with millis()
```
LVGL v9: `lv_display_create(320, 240)`, `lv_display_set_buffers(...)`, `lv_display_set_flush_cb`, `lv_indev_create()` + `lv_indev_set_read_cb`, and `lv_tick_set_cb(millis)`. In v9 the flush cb receives a `uint8_t *px_map`. If colors look wrong, use `lv_draw_sw_rgb565_swap` or `tft.setSwapBytes(true)`.

Keep LVGL draw buffers in internal DMA-capable RAM (`MALLOC_CAP_DMA`), not PSRAM. Put large images and fonts in PSRAM or flash.

## 9. Troubleshooting
| Symptom | Fix |
|---|---|
| Black screen, board runs | Backlight: `tft.setBrightness(255)` or drive GPIO45 HIGH |
| White screen | Wrong pins, or the bus never initialised. Check DC=46 and CS=10. |
| Negative-looking colors | `invert = true` (or `TFT_INVERSION_ON`) |
| Red/blue swapped | Flip `rgb_order` / `TFT_RGB_ORDER` |
| Touch mirrored or rotated | Match touch `offset_rotation`, or remap x/y for the current rotation |
| `Serial` prints nothing | Enable USB CDC On Boot / `-DARDUINO_USB_CDC_ON_BOOT=1` |
| PSRAM = 0 / boot loop | PSRAM must be OPI (`qio_opi`) |
| Sketch too big | Use a Huge APP or 16 MB partition scheme |
| Upload "No serial data received" | Hold BOOT, tap RESET, release BOOT, retry. Press RESET after flashing. |
| Port vanishes after flashing | Firmware crashed early or reconfigured USB. Enter download mode as above. |
| No sound | GPIO1 LOW, codec I2C init done, MCLK present, speaker on the SPEAKER jack, USB 5 V present |
