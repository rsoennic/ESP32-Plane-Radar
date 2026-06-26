#include "services/wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdio>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootResetHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

namespace {

/** Separate from planeradar prefs (rangeInit) to avoid NVS handle conflicts. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();

constexpr int kCoordParamLen = 20;
constexpr int kNameParamLen = 32;
constexpr char kCoordInputAttrs[] =
    " type=\"number\" step=\"0.000001\"";
constexpr char kTextInputAttrs[] = " type=\"text\"";

WiFiManagerParameter s_param_select_0(
    "<div style=\"margin:1rem 0 0.5rem 0; padding-top:0.5rem; border-top:1px solid #ccc;\">"
  "<input type=\"radio\" id=\"radar_select_0\" name=\"radar_location_select\" value=\"0\""
  " onclick=\"radarSetSelected(this.value)\" onchange=\"radarSetSelected(this.value)\"> "
    "<label for=\"radar_select_0\">Select location 1</label></div>"
  "<script>(function(){"
  "window.radarSetSelected=function(v){"
  "var sel=document.querySelector('input[name=\\\"radar_selected\\\"]');"
  "if(sel){sel.value=v;}"
  "};"
  "function radarInitSelected(){"
  "var sel=document.querySelector('input[name=\\\"radar_selected\\\"]');"
  "var v=(sel&&sel.value&&sel.value.length)?sel.value:'0';"
  "if(sel){sel.value=v;}"
  "var radios=document.querySelectorAll('input[name=\\\"radar_location_select\\\"]');"
  "for(var i=0;i<radios.length;i++){radios[i].checked=(radios[i].value===v);}"
  "}"
  "if(document.addEventListener){document.addEventListener('DOMContentLoaded', radarInitSelected);}"
  "setTimeout(radarInitSelected, 0);"
  "})();</script>");
WiFiManagerParameter s_param_name("radar_name", "Location name", "", kNameParamLen, kTextInputAttrs);
WiFiManagerParameter s_param_lat("radar_lat", "Latitude (deg)", "0",
                                kCoordParamLen, kCoordInputAttrs);
WiFiManagerParameter s_param_lon("radar_lon", "Longitude (deg)", "0",
                                kCoordParamLen, kCoordInputAttrs);

WiFiManagerParameter s_param_select_1(
    "<hr><div style=\"margin:1rem 0 0.5rem 0;\">"
    "<input type=\"radio\" id=\"radar_select_1\" name=\"radar_location_select\" value=\"1\""
  " onclick=\"radarSetSelected(this.value)\" onchange=\"radarSetSelected(this.value)\"> "
    "<label for=\"radar_select_1\">Select location 2</label></div>");
WiFiManagerParameter s_param_name_1("radar_name_1", "Location 2 name", "",
                                    kNameParamLen, kTextInputAttrs);
WiFiManagerParameter s_param_lat_1("radar_lat_1", "Latitude 2 (deg)", "",
                                  kCoordParamLen, kCoordInputAttrs);
WiFiManagerParameter s_param_lon_1("radar_lon_1", "Longitude 2 (deg)", "",
                                  kCoordParamLen, kCoordInputAttrs);

WiFiManagerParameter s_param_select_2(
    "<hr><div style=\"margin:1rem 0 0.5rem 0;\">"
    "<input type=\"radio\" id=\"radar_select_2\" name=\"radar_location_select\" value=\"2\""
  " onclick=\"radarSetSelected(this.value)\" onchange=\"radarSetSelected(this.value)\"> "
    "<label for=\"radar_select_2\">Select location 3</label></div>");
WiFiManagerParameter s_param_name_2("radar_name_2", "Location 3 name", "",
                                    kNameParamLen, kTextInputAttrs);
WiFiManagerParameter s_param_lat_2("radar_lat_2", "Latitude 3 (deg)", "",
                                  kCoordParamLen, kCoordInputAttrs);
WiFiManagerParameter s_param_lon_2("radar_lon_2", "Longitude 3 (deg)", "",
                                  kCoordParamLen, kCoordInputAttrs);

WiFiManagerParameter s_param_select_3(
    "<hr><div style=\"margin:1rem 0 0.5rem 0;\">"
    "<input type=\"radio\" id=\"radar_select_3\" name=\"radar_location_select\" value=\"3\""
  " onclick=\"radarSetSelected(this.value)\" onchange=\"radarSetSelected(this.value)\"> "
    "<label for=\"radar_select_3\">Select location 4</label></div>");
WiFiManagerParameter s_param_name_3("radar_name_3", "Location 4 name", "",
                                    kNameParamLen, kTextInputAttrs);
WiFiManagerParameter s_param_lat_3("radar_lat_3", "Latitude 4 (deg)", "",
                                  kCoordParamLen, kCoordInputAttrs);
WiFiManagerParameter s_param_lon_3("radar_lon_3", "Longitude 4 (deg)", "",
                                  kCoordParamLen, kCoordInputAttrs);

WiFiManagerParameter s_param_selected("radar_selected", "", "0", 2,
                                      "type=\"hidden\"");

char s_miles_checkbox_attrs[32] = "type=\"checkbox\" checked";
WiFiManagerParameter s_param_miles("use_miles", "Display distances in miles", "T", 2,
                                   s_miles_checkbox_attrs, WFM_LABEL_AFTER);

char s_runways_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_runways("show_runways", "Show airport runways", "T", 2,
                                     s_runways_checkbox_attrs, WFM_LABEL_AFTER);

void refreshPortalParamDefaults() {
  char lat_buf[kCoordParamLen + 1];
  char lon_buf[kCoordParamLen + 1];
  
  // Set all 4 location coordinate pairs
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::latByIndex(0));
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lonByIndex(0));
  s_param_lat.setValue(lat_buf, kCoordParamLen);
  s_param_lon.setValue(lon_buf, kCoordParamLen);
  
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::latByIndex(1));
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lonByIndex(1));
  s_param_lat_1.setValue(lat_buf, kCoordParamLen);
  s_param_lon_1.setValue(lon_buf, kCoordParamLen);
  
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::latByIndex(2));
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lonByIndex(2));
  s_param_lat_2.setValue(lat_buf, kCoordParamLen);
  s_param_lon_2.setValue(lon_buf, kCoordParamLen);
  
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::latByIndex(3));
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lonByIndex(3));
  s_param_lat_3.setValue(lat_buf, kCoordParamLen);
  s_param_lon_3.setValue(lon_buf, kCoordParamLen);
  
  // Set all location names
  s_param_name.setValue(services::location::name(0), kNameParamLen);
  s_param_name_1.setValue(services::location::name(1), kNameParamLen);
  s_param_name_2.setValue(services::location::name(2), kNameParamLen);
  s_param_name_3.setValue(services::location::name(3), kNameParamLen);
  
  // Set selected location index in hidden field
  char selected_buf[3];
  snprintf(selected_buf, sizeof(selected_buf), "%d",
           services::location::selectedLocationIndex());
  s_param_selected.setValue(selected_buf, 2);
  
  snprintf(s_miles_checkbox_attrs, sizeof(s_miles_checkbox_attrs), "type=\"checkbox\"%s",
           ui::radar::useMiles() ? " checked" : "");
  s_param_miles.setValue("T", 2);
  snprintf(s_runways_checkbox_attrs, sizeof(s_runways_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::showRunways() ? " checked" : "");
  s_param_runways.setValue("T", 2);
}

void onPortalParamsSaved() {
  services::location::saveNamesFromStrings(s_param_name.getValue(),
                                           s_param_name_1.getValue(),
                                           s_param_name_2.getValue(),
                                           s_param_name_3.getValue());

  Serial.printf("Portal save selected(raw)=%s\n", s_param_selected.getValue());

  if (services::location::saveLatLonsFromStrings(
          s_param_lat.getValue(), s_param_lon.getValue(),
          s_param_lat_1.getValue(), s_param_lon_1.getValue(),
          s_param_lat_2.getValue(), s_param_lon_2.getValue(),
          s_param_lat_3.getValue(), s_param_lon_3.getValue())) {
    const int selected_index = atoi(s_param_selected.getValue());
    if (selected_index >= 0 && selected_index <= 3) {
      services::location::saveSelectedLocation(selected_index);
      Serial.printf("Portal save selected=%d active=%.6f,%.6f\n", selected_index,
                    services::location::lat(), services::location::lon());
    } else {
      Serial.printf("Invalid selected location index %d in portal\n",
                    selected_index);
    }
  } else {
    Serial.println("Invalid location lat/lon in portal — keeping previous location");
  }

  ui::radar::saveMilesFromPortal(s_param_miles.getValue());
  ui::radar::saveRunwaysFromPortal(s_param_runways.getValue());
}

void attachPortalParams(WiFiManager& wm) {
  refreshPortalParamDefaults();
  wm.addParameter(&s_param_select_0);
  wm.addParameter(&s_param_name);
  wm.addParameter(&s_param_lat);
  wm.addParameter(&s_param_lon);
  wm.addParameter(&s_param_select_1);
  wm.addParameter(&s_param_name_1);
  wm.addParameter(&s_param_lat_1);
  wm.addParameter(&s_param_lon_1);
  wm.addParameter(&s_param_select_2);
  wm.addParameter(&s_param_name_2);
  wm.addParameter(&s_param_lat_2);
  wm.addParameter(&s_param_lon_2);
  wm.addParameter(&s_param_select_3);
  wm.addParameter(&s_param_name_3);
  wm.addParameter(&s_param_lat_3);
  wm.addParameter(&s_param_lon_3);
  wm.addParameter(&s_param_selected);
  wm.addParameter(&s_param_miles);
  wm.addParameter(&s_param_runways);
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  if (!pending) {
    return false;
  }

  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

bool storedWifiCredentials() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return false;
  }
  return conf.sta.ssid[0] != '\0';
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  ui::radar::unitsReset();
  Serial.println("WiFi credentials, location, and units cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_13dBm);//WIFI_POWER_8_5dBm);///ras
 /// WiFi.setSleep(WIFI_PS_MAX_MODEM); ///ras
  statusScreenPortal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  config::kPortalHostname, config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  refreshPortalParamDefaults();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                config::kPortalHostname, WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_13dBm);//WIFI_POWER_8_5dBm);///ras
  ///WiFi.setSleep(WIFI_PS_MAX_MODEM); ///ras
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt,
                    config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }

    startStaConnect(ssid, pass);

    if (waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)) {
      return true;
    }
  }

  return false;
}

bool connectSavedNetwork(bool show_ui) {
  if (!storedWifiCredentials()) {
    return false;
  }

  ensureWifiManager();
  const String ssid = s_wm.getWiFiSSID();
  if (ssid.length() == 0) {
    return false;
  }
  const String pass = s_wm.getWiFiPass();
  return tryConnectWithUi(ssid, pass, show_ui);
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName);
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    if (!s_long_press_handled &&
        millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork(true);
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      bootButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  initBootButton();
  ensureWifiManager();

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  if (force_portal) {
    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi (portal opens if needed)...");

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials() && connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials()) {
    Serial.println("Saved WiFi could not connect — opening setup portal");
  } else {
    Serial.println("No saved WiFi — opening setup portal");
  }

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
}
