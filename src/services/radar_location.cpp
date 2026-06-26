#include "services/radar_location.h"

#include <Preferences.h>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat0[] = "lat0";
constexpr char kKeyLon0[] = "lon0";
constexpr char kKeyLat1[] = "lat1";
constexpr char kKeyLon1[] = "lon1";
constexpr char kKeyLat2[] = "lat2";
constexpr char kKeyLon2[] = "lon2";
constexpr char kKeyLat3[] = "lat3";
constexpr char kKeyLon3[] = "lon3";
constexpr char kKeyName0[] = "name0";
constexpr char kKeyName1[] = "name1";
constexpr char kKeyName2[] = "name2";
constexpr char kKeyName3[] = "name3";
constexpr char kKeySelected[] = "sel";
constexpr int kLocationNameLen = 32;

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
double s_lats[4] = {config::kDefaultRadarLat, 0.0, 0.0, 0.0};
double s_lons[4] = {config::kDefaultRadarLon, 0.0, 0.0, 0.0};
char s_names[4][kLocationNameLen + 1] = {{""}, {""}, {""}, {""}};
int s_selected_location = 0;

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void persist(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat0, lat);
  prefs.putDouble(kKeyLon0, lon);
  prefs.end();
  s_lat = lat;
  s_lon = lon;
  s_lats[0] = lat;
  s_lons[0] = lon;
}

void persistSelectedLocation() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putUChar(kKeySelected, static_cast<uint8_t>(s_selected_location));
  prefs.end();
}

void persistLocationNames() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString(kKeyName0, s_names[0]);
  prefs.putString(kKeyName1, s_names[1]);
  prefs.putString(kKeyName2, s_names[2]);
  prefs.putString(kKeyName3, s_names[3]);
  prefs.end();
}

void persistLocationLatLons() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat0, s_lats[0]);
  prefs.putDouble(kKeyLon0, s_lons[0]);
  prefs.putDouble(kKeyLat1, s_lats[1]);
  prefs.putDouble(kKeyLon1, s_lons[1]);
  prefs.putDouble(kKeyLat2, s_lats[2]);
  prefs.putDouble(kKeyLon2, s_lons[2]);
  prefs.putDouble(kKeyLat3, s_lats[3]);
  prefs.putDouble(kKeyLon3, s_lons[3]);
  prefs.end();
}

void loadName(const char* key, char* out, size_t len) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(key)) {
    const String stored = prefs.getString(key, "");
    strncpy(out, stored.c_str(), len - 1);
    out[len - 1] = '\0';
  } else {
    out[0] = '\0';
  }
  prefs.end();
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);

  // Load all 4 location lat/lon pairs
  s_lats[0] = prefs.getDouble(kKeyLat0, config::kDefaultRadarLat);
  s_lons[0] = prefs.getDouble(kKeyLon0, config::kDefaultRadarLon);
  s_lats[1] = prefs.getDouble(kKeyLat1, 0.0);
  s_lons[1] = prefs.getDouble(kKeyLon1, 0.0);
  s_lats[2] = prefs.getDouble(kKeyLat2, 0.0);
  s_lons[2] = prefs.getDouble(kKeyLon2, 0.0);
  s_lats[3] = prefs.getDouble(kKeyLat3, 0.0);
  s_lons[3] = prefs.getDouble(kKeyLon3, 0.0);

  // Load selected location index
  if (prefs.isKey(kKeySelected)) {
    const uint8_t stored = prefs.getUChar(kKeySelected, 0);
    if (stored <= 3) {
      s_selected_location = stored;
    }
  } else {
    s_selected_location = 0;
  }

  prefs.end();

  // Set current lat/lon from selected location
  s_lat = s_lats[s_selected_location];
  s_lon = s_lons[s_selected_location];

  // Load all location names
  loadName(kKeyName0, s_names[0], sizeof(s_names[0]));
  loadName(kKeyName1, s_names[1], sizeof(s_names[1]));
  loadName(kKeyName2, s_names[2], sizeof(s_names[2]));
  loadName(kKeyName3, s_names[3], sizeof(s_names[3]));
}

double lat() { return s_lat; }

double lon() { return s_lon; }

const char* name(int index) {
  if (index < 0 || index >= 4) {
    return "";
  }
  return s_names[index];
}

int selectedLocationIndex() { return s_selected_location; }

double latByIndex(int index) {
  if (index < 0 || index >= 4) {
    return config::kDefaultRadarLat;
  }
  return s_lats[index];
}

double lonByIndex(int index) {
  if (index < 0 || index >= 4) {
    return config::kDefaultRadarLon;
  }
  return s_lons[index];
}

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!parseCoord(lat_str, &lat) || !parseCoord(lon_str, &lon)) {
    return false;
  }
  if (!validLatLon(lat, lon)) {
    return false;
  }
  persist(lat, lon);
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

bool saveNamesFromStrings(const char* name0, const char* name1,
                          const char* name2, const char* name3) {
  strncpy(s_names[0], name0 ? name0 : "", kLocationNameLen);
  strncpy(s_names[1], name1 ? name1 : "", kLocationNameLen);
  strncpy(s_names[2], name2 ? name2 : "", kLocationNameLen);
  strncpy(s_names[3], name3 ? name3 : "", kLocationNameLen);
  s_names[0][kLocationNameLen] = '\0';
  s_names[1][kLocationNameLen] = '\0';
  s_names[2][kLocationNameLen] = '\0';
  s_names[3][kLocationNameLen] = '\0';
  persistLocationNames();
  return true;
}

bool saveLatLonsFromStrings(const char* lat0, const char* lon0,
                            const char* lat1, const char* lon1,
                            const char* lat2, const char* lon2,
                            const char* lat3, const char* lon3) {
  double lats[4], lons[4];
  if (!parseCoord(lat0, &lats[0]) || !parseCoord(lon0, &lons[0])) return false;
  if (!parseCoord(lat1, &lats[1]) || !parseCoord(lon1, &lons[1])) return false;
  if (!parseCoord(lat2, &lats[2]) || !parseCoord(lon2, &lons[2])) return false;
  if (!parseCoord(lat3, &lats[3]) || !parseCoord(lon3, &lons[3])) return false;

  for (int i = 0; i < 4; i++) {
    if (!validLatLon(lats[i], lons[i])) return false;
    s_lats[i] = lats[i];
    s_lons[i] = lons[i];
  }
  persistLocationLatLons();
  return true;
}

bool saveSelectedLocation(int index) {
  if (index < 0 || index > 3) {
    return false;
  }
  s_selected_location = index;
  persistSelectedLocation();
  // Update current lat/lon from selected location
  s_lat = s_lats[index];
  s_lon = s_lons[index];
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat0);
  prefs.remove(kKeyLon0);
  prefs.remove(kKeyLat1);
  prefs.remove(kKeyLon1);
  prefs.remove(kKeyLat2);
  prefs.remove(kKeyLon2);
  prefs.remove(kKeyLat3);
  prefs.remove(kKeyLon3);
  prefs.remove(kKeyName0);
  prefs.remove(kKeyName1);
  prefs.remove(kKeyName2);
  prefs.remove(kKeyName3);
  prefs.remove(kKeySelected);
  prefs.end();
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
  s_lats[0] = config::kDefaultRadarLat;
  s_lons[0] = config::kDefaultRadarLon;
  s_lats[1] = 0.0;
  s_lons[1] = 0.0;
  s_lats[2] = 0.0;
  s_lons[2] = 0.0;
  s_lats[3] = 0.0;
  s_lons[3] = 0.0;
  s_names[0][0] = '\0';
  s_names[1][0] = '\0';
  s_names[2][0] = '\0';
  s_names[3][0] = '\0';
  s_selected_location = 0;
}

}  // namespace services::location
