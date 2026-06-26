#pragma once

namespace services::location {

/** Load saved lat/lon and location metadata from NVS, or use defaults. */
void init();

/** Factory defaults when nothing is stored (also used for portal field prefill). */
double lat();
double lon();
const char* name(int index);
int selectedLocationIndex();
double latByIndex(int index);
double lonByIndex(int index);

/** Parse portal strings, validate, persist to NVS, update runtime values. */
bool saveNamesFromStrings(const char* name0, const char* name1,
                          const char* name2, const char* name3);
bool saveLatLonsFromStrings(const char* lat0, const char* lon0,
                            const char* lat1, const char* lon1,
                            const char* lat2, const char* lon2,
                            const char* lat3, const char* lon3);
bool saveSelectedLocation(int index);

/** Clear stored coordinates and location metadata (e.g. with WiFi credential reset). */
void clear();

}  // namespace services::location
