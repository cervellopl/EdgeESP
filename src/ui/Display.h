#pragma once
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>
#include "config.h"

// LovyanGFX binding for the MCUFRIEND 3.6" Mega shield.
// On the ESP32-S3 this maps onto the LCD_CAM i80 peripheral, so the 16-bit bus
// is DMA-driven and a full 480x320 frame costs almost no CPU.

class EdgeDisplay : public lgfx::LGFX_Device {
 public:
  EdgeDisplay() {
    {
      auto cfg = _bus.config();
      cfg.freq_write = LCD_BUS_FREQ_HZ;
      cfg.pin_wr = LCD_WR;
      cfg.pin_rd = LCD_RD;
      cfg.pin_rs = LCD_RS;
      cfg.pin_d0  = LCD_D0;   cfg.pin_d1  = LCD_D1;   cfg.pin_d2  = LCD_D2;
      cfg.pin_d3  = LCD_D3;   cfg.pin_d4  = LCD_D4;   cfg.pin_d5  = LCD_D5;
      cfg.pin_d6  = LCD_D6;   cfg.pin_d7  = LCD_D7;   cfg.pin_d8  = LCD_D8;
      cfg.pin_d9  = LCD_D9;   cfg.pin_d10 = LCD_D10;  cfg.pin_d11 = LCD_D11;
      cfg.pin_d12 = LCD_D12;  cfg.pin_d13 = LCD_D13;  cfg.pin_d14 = LCD_D14;
      cfg.pin_d15 = LCD_D15;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = LCD_CS;
      cfg.pin_rst  = LCD_RST;
      cfg.pin_busy = -1;
      cfg.memory_width  = 320;
      cfg.memory_height = 480;
      cfg.panel_width   = 320;
      cfg.panel_height  = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable   = true;
      cfg.invert     = false;
      cfg.rgb_order  = false;
      cfg.dlen_16bit = true;
      cfg.bus_shared = true;      // the SD card shares SPI, not this bus, but be safe
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }

 private:
  lgfx::Bus_Parallel16 _bus;
#if   defined(PANEL_ILI9481)
  lgfx::Panel_ILI9481 _panel;
#elif defined(PANEL_R61581)
  lgfx::Panel_R61581  _panel;
#elif defined(PANEL_HX8357B)
  lgfx::Panel_HX8357B _panel;
#else
  lgfx::Panel_ILI9486 _panel;
#endif
};

// Bit-bangs the 8080 bus before LovyanGFX takes it over and prints whatever the
// panel answers to the ID registers. Run once, then pin -DPANEL_xxx in
// platformio.ini. Costs nothing at runtime and saves an afternoon of guessing.
void dumpPanelId();
