/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#define ENABLE_GPS 0  // Set to 1 to use GPS position instead of the saved/default location.

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/gps.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

int gpsAvailable(void* context) {
  return static_cast<HardwareSerial*>(context)->available();
}

int gpsRead(void* context) {
  return static_cast<HardwareSerial*>(context)->read();
}

bool tryAcquireGpsPosition(double* latitude_deg, double* longitude_deg) {
  HardwareSerial& gps_port = Serial1;
  gps_port.begin(9600, SERIAL_8N1, GPS_UART1_RX_PIN, GPS_UART1_TX_PIN);

  gps_serial_t serial = {&gps_port, gpsAvailable, gpsRead};
  gps_begin(&serial);

  const unsigned long start_ms = millis();
  while (millis() - start_ms < GPS_FIX_TIMEOUT_MS) {
    gps_update();
    if (gps_get_position(latitude_deg, longitude_deg)) {
      return true;
    }
    delay(100);
  }

  return false;
}

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();

#if ENABLE_GPS
  double gps_lat = 0.0;
  double gps_lon = 0.0;
  if (tryAcquireGpsPosition(&gps_lat, &gps_lon)) {
    char lat_buf[32];
    char lon_buf[32];
    snprintf(lat_buf, sizeof(lat_buf), "%.6f", gps_lat);
    snprintf(lon_buf, sizeof(lon_buf), "%.6f", gps_lon);
    if (services::location::saveFromStrings(lat_buf, lon_buf)) {
      Serial.printf("GPS position applied: %.6f, %.6f\n", gps_lat, gps_lon);
    }
  } else {
    Serial.println("GPS fix timeout — using saved/default position");
  }
#endif
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
