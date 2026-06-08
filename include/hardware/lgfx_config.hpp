#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/** LovyanGFX device: GC9A01 on SPI. Pin values come from config.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
#if defined(DISPLAY_TARGET_SUNTON_2432S028R)
  lgfx::Panel_ILI9341 _panel;
  lgfx::Light_PWM _light;

  void fillPhysicalRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint16_t color) {
    if (w == 0 || h == 0) {
      return;
    }

    static constexpr uint32_t kSwapMask = 0xFF00FF;
    const uint32_t x_range = x | (static_cast<uint32_t>(x + w - 1) << 16);
    const uint32_t y_range = y | (static_cast<uint32_t>(y + h - 1) << 16);

    startWrite();
    _bus.writeCommand(0x2A, 8);
    _bus.writeData(((x_range >> 8) & kSwapMask) + ((x_range & kSwapMask) << 8),
                   32);
    _bus.writeCommand(0x2B, 8);
    _bus.writeData(((y_range >> 8) & kSwapMask) + ((y_range & kSwapMask) << 8),
                   32);
    _bus.writeCommand(0x2C, 8);
    _panel.writeBlock(color, static_cast<uint32_t>(w) * h);
    endWrite();
  }
#else
  lgfx::Panel_GC9A01 _panel;
#endif

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = static_cast<int>(config::kDisplayPinMiso);
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.memory_width = config::kDisplayMemoryWidth;
      cfg.memory_height = config::kDisplayMemoryHeight;
      cfg.panel_width = config::kDisplayWidth;
      cfg.panel_height = config::kDisplayHeight;
      cfg.offset_x = config::kDisplayOffsetX;
      cfg.offset_y = config::kDisplayOffsetY;
      cfg.readable = config::kDisplayPinMiso != GPIO_NUM_NC;
      cfg.bus_shared = false;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      _panel.config(cfg);
    }
#if defined(DISPLAY_TARGET_SUNTON_2432S028R)
    {
      auto cfg = _light.config();
      cfg.pin_bl = static_cast<int>(config::kDisplayPinBacklight);
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif
    setPanel(&_panel);
  }

  void clearOuterArea(uint16_t color) {
#if defined(DISPLAY_TARGET_SUNTON_2432S028R)
    const bool rotated = (config::kDisplayRotation & 1) != 0;
    const uint16_t physical_width = rotated ? config::kDisplayMemoryHeight
                                            : config::kDisplayMemoryWidth;
    const uint16_t physical_height = rotated ? config::kDisplayMemoryWidth
                                             : config::kDisplayMemoryHeight;
    const uint16_t margin_x =
        (physical_width - static_cast<uint16_t>(config::kDisplayWidth)) / 2;
    const uint16_t margin_y =
        (physical_height - static_cast<uint16_t>(config::kDisplayHeight)) / 2;

    fillPhysicalRect(0, 0, physical_width, margin_y, color);
    fillPhysicalRect(0, margin_y + config::kDisplayHeight, physical_width,
                     margin_y, color);
    fillPhysicalRect(0, margin_y, margin_x, config::kDisplayHeight, color);
    fillPhysicalRect(margin_x + config::kDisplayWidth, margin_y, margin_x,
                     config::kDisplayHeight, color);

    startWrite();
    setWindow(0, 0, config::kDisplayWidth - 1, config::kDisplayHeight - 1);
    endWrite();
#else
    static_cast<void>(color);
#endif
  }
};
