// Fixed-location stub.
//
// The Pico 2 W has no built-in GPS. Rather than require a UART GPS
// module on the BOM, `make init` asks for the device's permanent
// coordinates (decimal degrees + optional altitude/accuracy) and
// bakes them into secrets.h. This module exposes those values
// behind the same interface a real GPS would, plus a timestamp
// synthesised from the firmware's build time + uptime seconds, so
// every observation has a monotonically advancing FirstSeen field
// for Wigle.
//
// The interface name is kept as "gps_*" so the call sites in
// wifi_scan.c and main.c don't change. To drive a real UART GPS
// instead, restore the NMEA parser from git history and remove the
// FIXED_* defines.

#pragma once

#include <stdbool.h>

typedef struct {
    bool   has_fix;       // always true in the fixed-location build
    double latitude;      // decimal degrees, +N / -S
    double longitude;     // decimal degrees, +E / -W
    double altitude_m;    // metres above mean sea level
    double accuracy_m;    // metres (assumed 1-sigma)
    int    satellites;    // always 0 here; kept for API parity
    char   timestamp[20]; // "YYYY-MM-DD HH:MM:SS" (UTC), advances with uptime
} gps_state_t;

void gps_init(void);

// Refresh the timestamp. Cheap; safe to call from the main loop on
// every iteration, but a once-per-scan call is enough.
void gps_poll(void);

bool gps_has_fix(void);
const gps_state_t *gps_get_state(void);
