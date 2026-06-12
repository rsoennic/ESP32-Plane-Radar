#include "hardware/gps.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K_LINE_CAPACITY 128

// Current serial backend used by the parser.
gps_serial_t* g_serial = NULL;
char g_line[K_LINE_CAPACITY];
size_t g_line_len = 0;

// Most recent parsed fix and the sentence type that produced it.
gps_fix_t g_fix = {false, 0.0, 0.0, 0.0, 0, 0.0, 0};
char g_last_sentence[32] = {0};

// Validate the NMEA checksum before trusting a sentence.
static bool is_checksum_ok(const char* line) {
  if (line[0] != '$') {
    return false;
  }

  const char* star = strchr(line, '*');
  if (star == NULL || star[1] == '\0' || star[2] == '\0') {
    return false;
  }

  uint8_t checksum = 0;
  for (const char* p = line + 1; p < star; ++p) {
    checksum ^= (uint8_t)(*p);
  }

  char expected[3] = {star[1], star[2], '\0'};
  char* endptr = NULL;
  const unsigned int parsed = (unsigned int)strtoul(expected, &endptr, 16);
  return endptr != expected && *endptr == '\0' && parsed == checksum;
}

// Convert a NMEA DMS coordinate string into decimal degrees.
static double nmea_to_decimal(const char* text, const char* hemi) {
  if (text == NULL || text[0] == '\0') {
    return 0.0;
  }

  char* endptr = NULL;
  const double value = strtod(text, &endptr);
  if (endptr == text) {
    return 0.0;
  }

  const double degrees = floor(value / 100.0);
  const double minutes = value - (degrees * 100.0);
  double decimal = degrees + (minutes / 60.0);

  if (hemi != NULL && (*hemi == 'S' || *hemi == 'W')) {
    decimal = -decimal;
  }

  return decimal;
}

// Parse RMC sentences to recover position and speed when available.
static bool parse_rmc(const char* line, gps_fix_t* fix) {
  char copy[K_LINE_CAPACITY];
  if (strlen(line) >= sizeof(copy)) {
    return false;
  }
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  char* token = strtok(copy, ",");
  char* fields[16] = {NULL};
  size_t count = 0;
  while (token != NULL && count < 16) {
    fields[count++] = token;
    token = strtok(NULL, ",");
  }

  if (count < 8 || fields[2] == NULL || fields[3] == NULL || fields[5] == NULL) {
    return false;
  }

  if (fields[2][0] != 'A') {
    return false;
  }

  const double lat = nmea_to_decimal(fields[3], fields[4]);
  const double lon = nmea_to_decimal(fields[5], fields[6]);
  if (lat == 0.0 && lon == 0.0) {
    return false;
  }

  fix->valid = true;
  fix->latitude_deg = lat;
  fix->longitude_deg = lon;
  fix->speed_knots = (fields[7] != NULL) ? strtod(fields[7], NULL) : 0.0;
  fix->satellites = 0;
  fix->altitude_m = 0.0;
  fix->fix_age_ms = 0;

  return true;
}

// Parse GGA sentences to recover position, satellites, and altitude.
static bool parse_gga(const char* line, gps_fix_t* fix) {
  char copy[K_LINE_CAPACITY];
  if (strlen(line) >= sizeof(copy)) {
    return false;
  }
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  char* token = strtok(copy, ",");
  char* fields[16] = {NULL};
  size_t count = 0;
  while (token != NULL && count < 16) {
    fields[count++] = token;
    token = strtok(NULL, ",");
  }

  if (count < 9 || fields[6] == NULL || fields[2] == NULL || fields[3] == NULL || fields[5] == NULL) {
    return false;
  }

  const unsigned int quality = (unsigned int)strtoul(fields[6], NULL, 10);
  if (quality == 0) {
    return false;
  }

  const double lat = nmea_to_decimal(fields[3], fields[4]);
  const double lon = nmea_to_decimal(fields[5], fields[6]);
  if (lat == 0.0 && lon == 0.0) {
    return false;
  }

  fix->valid = true;
  fix->latitude_deg = lat;
  fix->longitude_deg = lon;
  fix->speed_knots = 0.0;
  fix->satellites = (fields[7] != NULL) ? (uint8_t)strtoul(fields[7], NULL, 10) : 0;
  fix->altitude_m = (fields[9] != NULL) ? strtod(fields[9], NULL) : 0.0;
  fix->fix_age_ms = 0;

  return true;
}

// Route a valid sentence to the appropriate parser.
static void process_line(const char* line) {
  if (!is_checksum_ok(line)) {
    return;
  }

  const char* start = line;
  if (strncmp(start, "$GPRMC", 6) == 0) {
    gps_fix_t candidate = {false, 0.0, 0.0, 0.0, 0, 0.0, 0};
    if (parse_rmc(line, &candidate)) {
      g_fix = candidate;
      strncpy(g_last_sentence, "RMC", sizeof(g_last_sentence) - 1);
      return;
    }
  }

  if (strncmp(start, "$GPGGA", 6) == 0) {
    gps_fix_t candidate = {false, 0.0, 0.0, 0.0, 0, 0.0, 0};
    if (parse_gga(line, &candidate)) {
      g_fix = candidate;
      strncpy(g_last_sentence, "GGA", sizeof(g_last_sentence) - 1);
    }
  }
}

// Attach the byte source that will feed the GPS parser.
void gps_begin(gps_serial_t* serial) {
  // Configure the default UART1 GPS pins on the ESP32-C3 Super Mini.
  pinMode(GPS_UART1_RX_PIN, INPUT);
  pinMode(GPS_UART1_TX_PIN, OUTPUT);

  // Keep the serial backend available for the later NMEA reader hookup.
  g_serial = serial;
  g_line_len = 0;
  g_fix.valid = false;
  memset(g_line, 0, sizeof(g_line));
  memset(g_last_sentence, 0, sizeof(g_last_sentence));
}

// Drain available bytes and parse complete NMEA lines.
void gps_update(void) {
  if (g_serial == NULL || g_serial->available == NULL || g_serial->read == NULL) {
    return;
  }

  while (g_serial->available(g_serial->context) > 0) {
    const int ch = g_serial->read(g_serial->context);
    if (ch < 0) {
      break;
    }

    if (ch == '\n') {
      if (g_line_len > 0) {
        g_line[g_line_len] = '\0';
        process_line(g_line);
        g_line_len = 0;
        memset(g_line, 0, sizeof(g_line));
      }
      continue;
    }

    if (ch == '\r') {
      continue;
    }

    if (g_line_len < (K_LINE_CAPACITY - 1)) {
      g_line[g_line_len++] = (char)ch;
    }
  }
}

// Clear buffered NMEA input and invalidate the current fix.
void gps_reset(void) {
  g_fix.valid = false;
  g_line_len = 0;
  memset(g_line, 0, sizeof(g_line));
  memset(g_last_sentence, 0, sizeof(g_last_sentence));
}

bool gps_has_fix(void) {
  return g_fix.valid;
}

bool gps_get_position(double* latitude_deg, double* longitude_deg) {
  if (!g_fix.valid) {
    return false;
  }

  if (latitude_deg != NULL) {
    *latitude_deg = g_fix.latitude_deg;
  }
  if (longitude_deg != NULL) {
    *longitude_deg = g_fix.longitude_deg;
  }
  return true;
}

bool gps_get_fix(gps_fix_t* fix_out) {
  if (!g_fix.valid || fix_out == NULL) {
    return false;
  }

  *fix_out = g_fix;
  return true;
}
