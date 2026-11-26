# ESP-IDF on the ES3C28P

The vendor demo uses ESP-IDF **5.4.1** and LVGL **8.4.0** (project `2.8inch_ESP32-S3_LVGL` in the data pack). The snippets below use the IDF ≥ 5.2 `i2c_master` driver and Espressif component-registry packages. Check the component versions with `idf.py add-dependency`.

## Contents
1. Project setup and sdkconfig
2. Shared I2C bus
3. LCD panel (esp_lcd + esp_lcd_ili9341)
4. Touch (esp_lcd_touch_ft5x06)
5. LVGL via esp_lvgl_port
6. Audio (esp_codec_dev + ES8311)
7. microSD (SDMMC 4-bit)
8. Backlight, LED, battery

## 1. Project setup

macOS install: follow https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/ (`brew install cmake ninja dfu-util`, clone v5.4.x, `./install.sh esp32s3`, `. ./export.sh`). The VS Code ESP-IDF extension also works. Keep project paths ASCII-only with no spaces.

```bash
idf.py create-project app && cd app && idf.py set-target esp32s3
idf.py add-dependency "espressif/esp_lcd_ili9341^2"
idf.py add-dependency "espressif/esp_lcd_touch_ft5x06^1"
idf.py add-dependency "espressif/esp_lvgl_port^2"
idf.py add-dependency "espressif/esp_codec_dev^1"
```

`sdkconfig.defaults`:
```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y            # or PARTITION_TABLE_SINGLE_APP_LARGE
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_FREERTOS_HZ=1000
CONFIG_LV_COLOR_16_SWAP=y                  # LVGL 8 + esp_lcd SPI (big-endian RGB565)
```
Flash and monitor: `idf.py -p /dev/cu.usbmodem101 flash monitor` (exit the monitor with Ctrl-]).

## 2. Shared I2C bus (touch 0x38 + ES8311 0x18 + header)
```c
#include "driver/i2c_master.h"
static i2c_master_bus_handle_t i2c_bus;
void board_i2c_init(void) {
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 16, .scl_io_num = 15,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   // board also has 4.7k pull-ups
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus));
}
```
Create this once and pass `i2c_bus` to both the touch and codec drivers. Don't let a component create its own bus on the same pins.

## 3. LCD panel
```c
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "driver/spi_master.h"

#define LCD_H 240
#define LCD_V 320
static esp_lcd_panel_handle_t panel; static esp_lcd_panel_io_handle_t lcd_io;

void board_lcd_init(void) {
    spi_bus_config_t bus = {
        .sclk_io_num = 12, .mosi_io_num = 11, .miso_io_num = 13,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = 46, .cs_gpio_num = 10,
        .pclk_hz = 40 * 1000 * 1000, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &lcd_io));

    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = -1,                       // tied to EN
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(lcd_io, &pc, &panel));
    esp_lcd_panel_reset(panel);                     // software reset
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);        // IPS
    // landscape 320x240: swap_xy(true) + mirror(true,true) ~ MADCTL 0x68; tune mirror to taste
    esp_lcd_panel_disp_on_off(panel, true);

    gpio_set_direction(45, GPIO_MODE_OUTPUT); gpio_set_level(45, 1);   // backlight
}
```
To use the vendor's exact gamma and power settings, pass `ili9341_vendor_config_t` with an `init_cmds` array built from the sequence in hardware.md §8.

## 4. Touch
```c
#include "esp_lcd_touch_ft5x06.h"
static esp_lcd_touch_handle_t tp;
void board_touch_init(void) {
    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io, &tp_io));
    esp_lcd_touch_config_t cfg = {
        .x_max = LCD_H, .y_max = LCD_V,
        .rst_gpio_num = 18, .int_gpio_num = 17,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },   // set to match panel rotation
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io, &cfg, &tp));
}
```

## 5. LVGL (esp_lvgl_port)
```c
#include "esp_lvgl_port.h"
lvgl_port_init(&(lvgl_port_cfg_t)ESP_LVGL_PORT_INIT_CONFIG());
lv_disp_t *disp = lvgl_port_add_disp(&(lvgl_port_display_cfg_t){
    .io_handle = lcd_io, .panel_handle = panel,
    .buffer_size = LCD_H * 40, .double_buffer = true,
    .hres = LCD_H, .vres = LCD_V, .monochrome = false,
    .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
    .flags = { .buff_dma = true, .swap_bytes = true },   // swap_bytes for LVGL9; LVGL8 uses LV_COLOR_16_SWAP
});
lvgl_port_add_touch(&(lvgl_port_touch_cfg_t){ .disp = disp, .handle = tp });
// Use lvgl_port_lock(0) / lvgl_port_unlock() around LVGL calls from your tasks.
```

## 6. Audio (ES8311 via esp_codec_dev)
```c
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
static i2s_chan_handle_t tx, rx;
static esp_codec_dev_handle_t spk;

void board_audio_init(void) {
    gpio_set_direction(1, GPIO_MODE_OUTPUT); gpio_set_level(1, 0);      // amp ON (active low)

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, &rx));
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),            // MCLK = 256*fs
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = 4, .bclk = 5, .ws = 7, .dout = 8, .din = 6 },
    };
    i2s_channel_init_std_mode(tx, &sc); i2s_channel_init_std_mode(rx, &sc);
    i2s_channel_enable(tx); i2s_channel_enable(rx);

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl_if, .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1,                 // we drive GPIO1 ourselves (active low)
        .use_mclk = true, .digital_mic = false,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es);
    spk = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec_if, .data_if = data_if });
    esp_codec_dev_open(spk, &(esp_codec_dev_sample_info_t){
        .sample_rate = 44100, .channel = 2, .bits_per_sample = 16 });
    esp_codec_dev_set_out_vol(spk, 60);
    esp_codec_dev_set_in_gain(spk, 30);
}
// play: esp_codec_dev_write(spk, pcm, bytes);  record: esp_codec_dev_read(spk, buf, bytes);
```
`ES8311_CODEC_DEFAULT_ADDR` is the 8-bit form (0x30) that esp_codec_dev expects. The 7-bit address is 0x18.

## 7. microSD (SDMMC, 4-bit)
```c
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
sdmmc_host_t host = SDMMC_HOST_DEFAULT();
sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
slot.width = 4; slot.clk = 38; slot.cmd = 40; slot.d0 = 39; slot.d1 = 41; slot.d2 = 48; slot.d3 = 47;
slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
esp_vfs_fat_sdmmc_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024 };
sdmmc_card_t *card;
ESP_ERROR_CHECK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &card));
```

## 8. Backlight, LED, battery
- Backlight PWM: LEDC on GPIO45 (`ledc_timer_config` at 5 kHz with 10-bit resolution, then `ledc_channel_config` with `.gpio_num = 45`).
- RGB LED: `espressif/led_strip` component with the RMT backend, `strip_gpio_num = 42`, `max_leds = 1`, `LED_MODEL_WS2812`.
- Battery: `adc_oneshot` on ADC_UNIT_1 / ADC_CHANNEL_8 (GPIO9), `ADC_ATTEN_DB_12`, with `adc_cali_create_scheme_curve_fitting`. V_bat (mV) = 2 × calibrated mV.
