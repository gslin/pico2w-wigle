#include "gps.h"

#include <string.h>

#include "pico/stdlib.h"

#include "clock_sync.h"
#include "secrets.h"

static gps_state_t state;

void gps_init(void) {
    memset(&state, 0, sizeof(state));
    state.has_fix    = true;
    state.latitude   = FIXED_LATITUDE;
    state.longitude  = FIXED_LONGITUDE;
    state.altitude_m = FIXED_ALTITUDE_M;
    state.accuracy_m = FIXED_ACCURACY_M;
    state.satellites = 0;
    clock_sync_format_iso(state.timestamp, sizeof(state.timestamp));
}

void gps_poll(void) {
    clock_sync_format_iso(state.timestamp, sizeof(state.timestamp));
}

bool gps_has_fix(void) { return state.has_fix; }

const gps_state_t *gps_get_state(void) { return &state; }
