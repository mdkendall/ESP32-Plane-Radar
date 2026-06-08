#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

#if defined(DISPLAY_TARGET_SUPERMINI)
// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
#pragma message("Using configuration for DISPLAY_TARGET_SUPERMINI")
constexpr gpio_num_t kDisplayPinBacklight = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMiso = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL
constexpr int kDisplayMemoryWidth = 240;
constexpr int kDisplayMemoryHeight = 240;
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;
constexpr int kDisplayOffsetX = 0;
constexpr int kDisplayOffsetY = 0;
constexpr int kDisplayRotation = 0;
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;
#elif defined(DISPLAY_TARGET_SEEED_XIAO)
// --- BOOT button (XIAO ESP32-C3, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;

// --- Display: 1.28" round 240×240 XIAO Round Display from SeeedStudio ---
#pragma message("Using configuration for DISPLAY_TARGET_SEEED_XIAO")
constexpr gpio_num_t kDisplayPinBacklight = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_NC;   // not exposed on xiao round display
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_3;     // D1/GPIO 3
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_5;     // D3/GPIO 5
constexpr gpio_num_t kDisplayPinMiso = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_10;  // XIAO SPI MOSI
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_8;   // XIAO SPI SCK
constexpr int kDisplayMemoryWidth = 240;
constexpr int kDisplayMemoryHeight = 240;
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;
constexpr int kDisplayOffsetX = 0;
constexpr int kDisplayOffsetY = 0;
constexpr int kDisplayRotation = 0;
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;
#elif defined(DISPLAY_TARGET_SUNTON_2432S028R)
// --- BOOT button (Sunton ESP32-2432S028R, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_0;

// --- Display: 2.8" ILI9341 320×240 (SPI), use centered 240×240 viewport ---
#pragma message("Using configuration for DISPLAY_TARGET_SUNTON_2432S028R")
constexpr gpio_num_t kDisplayPinBacklight = GPIO_NUM_21;
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_NC;   // tied to board reset
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_15;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_2;
constexpr gpio_num_t kDisplayPinMiso = GPIO_NUM_12;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_13;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_14;
constexpr int kDisplayMemoryWidth = 240;
constexpr int kDisplayMemoryHeight = 320;
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;
constexpr int kDisplayOffsetX = 0;
constexpr int kDisplayOffsetY = 40;
constexpr int kDisplayRotation = 3;
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = false;
#else
#error "Define one DISPLAY_TARGET_* build flag."
#endif

constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
