// LovyanGFX device config for the LCDwiki 2.8" ESP32-S3 display (ES3C28P / ES3N28P)
// ILI9341V IPS 240x320 on SPI2 + FT6336G capacitive touch (I2C 0x38) + PWM backlight.
//
// Usage:
//   #include "LGFX_ES3C28P.hpp"
//   LGFX tft;
//   void setup(){ tft.init(); tft.setRotation(1); tft.fillScreen(TFT_RED); }
//
// For the no-touch ES3N28P, define ES3C28P_NO_TOUCH before including this file.
// NOTE: the touch I2C bus (SDA16/SCL15) is shared with the ES8311 codec (0x18).
//       LovyanGFX owns I2C port 1 here; if you also talk to the codec, either
//       use Wire (port 0) on the same pins *only while* LGFX isn't mid-transaction,
//       or better, read/write the codec through lgfx::i2c on the same port
//       (see references/arduino.md "Sharing the I2C bus").
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
#ifndef ES3C28P_NO_TOUCH
  lgfx::Touch_FT5x06 _touch;  // FT6336G is register-compatible
#endif

 public:
  LGFX(void) {
    {  // SPI bus
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;  // FSPI
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;  // 40 MHz reliable; 80 MHz often works
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 13;
      cfg.pin_dc = 46;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {  // Panel
      auto cfg = _panel.config();
      cfg.pin_cs = 10;
      cfg.pin_rst = -1;   // LCD reset is tied to the chip EN pin
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;      // IPS panel: vendor init sends INVON (0x21)
      cfg.rgb_order = false;  // BGR panel handled by Panel_ILI9341 default
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;  // SD card uses SDMMC, not this SPI bus
      _panel.config(cfg);
    }
    {  // Backlight on GPIO45, active high
      auto cfg = _light.config();
      cfg.pin_bl = 45;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#ifndef ES3C28P_NO_TOUCH
    {  // FT6336G touch
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 239;
      cfg.y_min = 0;
      cfg.y_max = 319;
      cfg.pin_int = 17;
      cfg.pin_rst = 18;
      cfg.bus_shared = false;
      cfg.offset_rotation = 2;  // touch panel is mounted 180 deg from the LCD (verified on hardware)
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 16;
      cfg.pin_scl = 15;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#endif
    setPanel(&_panel);
  }
};
