#include "wifi_scan.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"

#include "csv_buffer.h"
#include "gps.h"

// Wigle's CSV uses Android-style capability strings. The CYW43 scan
// callback only gives us a coarse auth_mode byte, so we collapse the
// space to "open" vs "encrypted (assume WPA2)". This matches what most
// modern networks actually run; the field is informational and Wigle
// does not reject rows based on it.
static const char *auth_mode_to_string(uint8_t auth_mode) {
    if (auth_mode == 0) return "[ESS]";
    return "[WPA2-PSK-CCMP][ESS]";
}

static int scan_result_cb(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (!r) return 0;
    if (csv_buffer_session_seen(r->bssid)) return 0;

    const gps_state_t *gps = gps_get_state();
    if (!gps->has_fix || gps->timestamp[0] == '\0') {
        // Without a fix the row would have lat=0/lon=0 which Wigle
        // would reject. Drop the observation rather than poison the
        // upload batch.
        return 0;
    }

    bool ok = csv_buffer_append_observation(
        r->bssid,
        (const char *)r->ssid, sizeof(r->ssid),
        auth_mode_to_string(r->auth_mode),
        gps->timestamp,
        (int)r->channel,
        (int)r->rssi,
        gps->latitude, gps->longitude,
        gps->altitude_m, gps->accuracy_m);

    if (!ok) {
        // Buffer is full. Drop the row; the main loop will trigger an
        // upload as soon as we return.
    }
    return 0;
}

void wifi_scan_init(void) {
    // Nothing to do — cyw43_arch is initialised in main() and the
    // dedupe set lives in csv_buffer.
}

int wifi_scan_start(void) {
    if (cyw43_wifi_scan_active(&cyw43_state)) return 0;
    csv_buffer_session_begin();
    cyw43_wifi_scan_options_t opts = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_result_cb);
    if (err) {
        printf("wifi_scan: start failed: %d\n", err);
    }
    return err;
}

bool wifi_scan_busy(void) {
    return cyw43_wifi_scan_active(&cyw43_state);
}
