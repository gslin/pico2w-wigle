#include "gps.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pico/stdlib.h"

#include "secrets.h"

// CMake injects the configure-time epoch via -DBUILD_EPOCH=NNN.
// Fall back to 2026-01-01 00:00:00 UTC if somebody compiles without
// it; the uploads are still well-formed CSV, just timestamped from
// an arbitrary baseline.
#ifndef BUILD_EPOCH
#define BUILD_EPOCH 1767225600
#endif

static gps_state_t state;

static void rebuild_timestamp(void) {
    uint64_t uptime_s = to_us_since_boot(get_absolute_time()) / 1000000ULL;
    time_t now = (time_t)((uint64_t)BUILD_EPOCH + uptime_s);

    struct tm tm;
    // gmtime_r rather than gmtime: the latter shares one static
    // struct tm across the whole program, which would race with any
    // future caller. Newlib has gmtime_r unconditionally.
    gmtime_r(&now, &tm);
    strftime(state.timestamp, sizeof(state.timestamp),
             "%Y-%m-%d %H:%M:%S", &tm);
}

void gps_init(void) {
    memset(&state, 0, sizeof(state));
    state.has_fix    = true;
    state.latitude   = FIXED_LATITUDE;
    state.longitude  = FIXED_LONGITUDE;
    state.altitude_m = FIXED_ALTITUDE_M;
    state.accuracy_m = FIXED_ACCURACY_M;
    state.satellites = 0;
    rebuild_timestamp();
}

void gps_poll(void) {
    rebuild_timestamp();
}

bool gps_has_fix(void) { return state.has_fix; }

const gps_state_t *gps_get_state(void) { return &state; }
