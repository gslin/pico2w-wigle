// GPS NMEA parser for an external UART GPS module.
//
// Wiring (defaults — change in gps.c if you wire it differently):
//   GPS module TX  -> Pico GP5 (UART1 RX)
//   GPS module GND -> Pico GND
//   GPS module VCC -> Pico 3V3 OUT (most modern modules accept 3.3 V;
//                                   if your module needs 5 V, take
//                                   power from VBUS instead and add a
//                                   level shifter on TX -> GP5)
//
// The Pico does not transmit to the GPS, so the GPS RX pin is left
// unconnected.

#pragma once

#include <stdbool.h>

typedef struct {
    bool   has_fix;       // 2D / 3D fix from GGA
    double latitude;      // decimal degrees, +N / -S
    double longitude;     // decimal degrees, +E / -W
    double altitude_m;    // metres above mean sea level
    double accuracy_m;    // approximate, derived from HDOP
    int    satellites;
    char   timestamp[20]; // "YYYY-MM-DD HH:MM:SS" (UTC)
} gps_state_t;

void gps_init(void);

// Drain bytes from the UART FIFO and feed them to the line parser.
// Cheap; safe to call from the main loop on every iteration.
void gps_poll(void);

bool             gps_has_fix(void);
const gps_state_t *gps_get_state(void);
