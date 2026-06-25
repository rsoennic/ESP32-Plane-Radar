#pragma once
#include <cstdint>
#include <driver/gpio.h>

namespace config {


// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.0.108";
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;
constexpr unsigned long kWifiConnectingFrameMs = 50;
constexpr unsigned long kWifiDownGraceMs = 4000;
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

constexpr unsigned long kBootResetHoldMs = 3000UL;
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Board pin profiles (selected via -DBOARD_* in platformio.ini) ---
// Boards drive a GC9A01(A) 1.28" round 240×240 panel over SPI.
#if defined(BOARD_WAVESHARE_S3)

    // Waveshare ESP32-S3 LCD 1.28" pinout
    #pragma message("Using configuration for BOARD_WAVESHARE_S3")
    constexpr gpio_num_t kBootPin = GPIO_NUM_0;  // ESP32-S3 BOOT button, active LOW
    constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_12;
    constexpr gpio_num_t kDisplayPinCs  = GPIO_NUM_9;
    constexpr gpio_num_t kDisplayPinDc  = GPIO_NUM_8;
    constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_11;  // LCD DIN
    constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_10;  // LCD CLK
    constexpr gpio_num_t kDisplayPinBl  = GPIO_NUM_40;   // Backlight
    #define HAS_BACKLIGHT_PIN

#elif defined(BOARD_S3_WAVESHARE_TOUCH128)

// Waveshare ESP32-S3-Touch-LCD-1.28 (ESP32-S3R2)
#pragma message("Using configuration for BOARD_ESP32_S3_TOUCH128")
constexpr gpio_num_t kBootPin = GPIO_NUM_0;  // BOOT0 button, active LOW
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_14;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_9;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_8;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_11;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_10;  // display SCL
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_2;     // backlight (active HIGH)
#define DISPLAY_HAS_BACKLIGHT 1

// CST816S capacitive touch (I2C). A tap cycles the range preset (no BOOT button).
constexpr gpio_num_t kTouchPinSda = GPIO_NUM_6;
constexpr gpio_num_t kTouchPinScl = GPIO_NUM_7;
constexpr gpio_num_t kTouchPinInt = GPIO_NUM_5;
constexpr gpio_num_t kTouchPinRst = GPIO_NUM_13;
constexpr uint8_t kTouchI2cAddr = 0x15;
/** Debounce: ignore a repeat tap within this window of the last one. */
constexpr unsigned long kTouchTapDebounceMs = 250UL;
#define DISPLAY_HAS_TOUCH 1

#elif defined(BOARD_SEEED_XIAO)
// --- Display: 1.28" round 240×240 XIAO Round Display from SeeedStudio ---
#pragma message("Using configuration for BOARD_SEEED_XIAO")
constexpr gpio_num_t kBootPin = GPIO_NUM_9; //ESP32-C3 BOOT button, active LOW
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_NC;   // not exposed on xiao round display
constexpr gpio_num_t kDisplayPinCs  = GPIO_NUM_3;    // D1/GPIO 3
constexpr gpio_num_t kDisplayPinDc  = GPIO_NUM_5;    // D3/GPIO 5
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_10;  // XIAO SPI MOSI
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_8;   // XIAO SPI SCK
// CST816S capacitive touch (I2C). A tap cycles the range preset (no BOOT button).
constexpr gpio_num_t kTouchPinSda = GPIO_NUM_6;
constexpr gpio_num_t kTouchPinScl = GPIO_NUM_7;
constexpr gpio_num_t kTouchPinInt = GPIO_NUM_20;
constexpr gpio_num_t kTouchPinRst = GPIO_NUM_NC; // no reset pin on Xiao interface
constexpr uint8_t kTouchI2cAddr = 0x2E;
/** Debounce: ignore a repeat tap within this window of the last one. */
constexpr unsigned long kTouchTapDebounceMs = 250UL;
#define DISPLAY_HAS_TOUCH 1

#else // BOARD_C3_SUPERMINI (default)

    #pragma message("Using configuration for BOARD_C3_SUPERMINI")
    // ESP32-C3 Super Mini pinout
    constexpr gpio_num_t kBootPin = GPIO_NUM_9;  // ESP32-C3 Super Mini, active LOW
    constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
    constexpr gpio_num_t kDisplayPinCs  = GPIO_NUM_1;
    constexpr gpio_num_t kDisplayPinDc  = GPIO_NUM_10;
    constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;   // display SDA
    constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;   // display SCL
    constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_NC;   // backlight tied to power (no GPIO)

#endif

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
//constexpr double kDefaultRadarLat = 45.541313; // Portland, OR
//constexpr double kDefaultRadarLon = -122.647316;

constexpr double kDefaultRadarLat = 37.51186; //KSQL;  
constexpr double kDefaultRadarLon = -122.24953;

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

} // namespace config