#include "scan.h"

#include <string.h>

#include "pico/cyw43_arch.h"

static scan_ap_t aps[SCAN_MAX_APS];
static size_t n_aps;
static uint32_t n_new;

// Runs from cyw43_arch_poll() on the main loop.
static int on_result(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    for (size_t i = 0; i < n_aps; i++) {
        if (memcmp(aps[i].bssid, r->bssid, sizeof aps[i].bssid) == 0) {
            if (r->rssi > aps[i].rssi) aps[i].rssi = r->rssi;
            return 0;
        }
    }
    if (n_aps == SCAN_MAX_APS) return 0;

    scan_ap_t *a = &aps[n_aps++];
    memcpy(a->bssid, r->bssid, sizeof a->bssid);
    a->ssid_len = MIN(r->ssid_len, sizeof a->ssid);
    memcpy(a->ssid, r->ssid, a->ssid_len);
    a->auth_mode = r->auth_mode;
    a->channel = (uint8_t)r->channel;
    a->rssi = r->rssi;
    a->first_seen = to_ms_since_boot(get_absolute_time()) / 1000;
    n_new++;
    return 0;
}

int scan_start(void) {
    cyw43_wifi_scan_options_t opts = {0};
    return cyw43_wifi_scan(&cyw43_state, &opts, NULL, on_result);
}

bool scan_active(void) {
    return cyw43_wifi_scan_active(&cyw43_state);
}

size_t scan_count(void) {
    return n_aps;
}

const scan_ap_t *scan_get(size_t i) {
    return &aps[i];
}

void scan_clear(void) {
    n_aps = 0;
}

uint32_t scan_take_new(void) {
    uint32_t n = n_new;
    n_new = 0;
    return n;
}
