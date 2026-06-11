#ifndef GPS_H
#define GPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Small adapter used to read bytes from any NMEA GPS source.
// The caller can provide a UART/Stream wrapper via this struct.
typedef struct {
  void* context;
  int (*available)(void* context);
  int (*read)(void* context);
} gps_serial_t;

// Latest parsed GNSS fix returned by the NMEA reader.
typedef struct {
  bool valid;
  double latitude_deg;
  double longitude_deg;
  double speed_knots;
  uint8_t satellites;
  double altitude_m;
  uint32_t fix_age_ms;
} gps_fix_t;

// Initialize the GPS reader with a byte source that exposes available/read callbacks.
void gps_begin(gps_serial_t* serial);
// Read and parse any pending NMEA sentences from the serial source.
void gps_update(void);
// Clear the current fix and buffered line state.
void gps_reset(void);

// Query the last valid position or full fix information.
bool gps_has_fix(void);
bool gps_get_position(double* latitude_deg, double* longitude_deg);
bool gps_get_fix(gps_fix_t* fix_out);

#ifdef __cplusplus
}
#endif

#endif  // GPS_H
