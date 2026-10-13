#include "scan.h"

#include <string.h>

#include "pico/cyw43_arch.h"

#include "log.h"

static scan_ap_t aps[SCAN_MAX_APS];
static size_t n_aps;
static uint32_t n_new;
static uint32_t n_results;
static uint32_t started_ms;
static bool full_logged;

// Printable copy of the SSID for the console.
static void log_ssid(char *dst, const scan_ap_t *a) {
    for (size_t i = 0; i < a->ssid_len; i++) {
        unsigned char ch = a->ssid[i];
        dst[i] = ch < 0x20 || ch == 0x7f ? '?' : (char)ch;
    }
    dst[a->ssid_len] = '\0';
}

// Runs from cyw43_arch_poll() on the main loop.
static int on_result(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    n_results++;
    for (size_t i = 0; i < n_aps; i++) {
        if (memcmp(aps[i].bssid, r->bssid, sizeof aps[i].bssid) == 0) {
            if (r->rssi > aps[i].rssi) aps[i].rssi = r->rssi;
            return 0;
        }
    }
    if (n_aps == SCAN_MAX_APS) {
        if (!full_logged) LOG("scan: table full (%d BSSIDs), new ones are dropped", SCAN_MAX_APS);
        full_logged = true;
        return 0;
    }

    scan_ap_t *a = &aps[n_aps++];
    memcpy(a->bssid, r->bssid, sizeof a->bssid);
    a->ssid_len = MIN(r->ssid_len, sizeof a->ssid);
    memcpy(a->ssid, r->ssid, a->ssid_len);
    a->auth_mode = r->auth_mode;
    a->channel = (uint8_t)r->channel;
    a->rssi = r->rssi;
    a->first_seen = now_ms() / 1000;
    n_new++;

    char ssid[sizeof a->ssid + 1];
    log_ssid(ssid, a);
    LOG("scan: new %02x:%02x:%02x:%02x:%02x:%02x ch %2u %4d dBm %-16s \"%s\"",
        a->bssid[0], a->bssid[1], a->bssid[2], a->bssid[3], a->bssid[4], a->bssid[5],
        a->channel, a->rssi, scan_capabilities(a->auth_mode), ssid);
    return 0;
}

const char *scan_capabilities(uint8_t auth) {
    if (auth & 4) return auth & 2 ? "[WPA][WPA2][ESS]" : "[WPA2][ESS]";
    if (auth & 2) return "[WPA][ESS]";
    if (auth & 1) return "[WEP][ESS]";
    return "[ESS]";
}

int scan_start(void) {
    cyw43_wifi_scan_options_t opts = {0};
    n_results = 0;
    started_ms = now_ms();
    return cyw43_wifi_scan(&cyw43_state, &opts, NULL, on_result);
}

bool scan_active(void) {
    return cyw43_wifi_scan_active(&cyw43_state);
}

uint32_t scan_finish(void) {
    uint32_t n = n_new;
    // Quiet scans are not logged, except when nothing was heard at all.
    if (n > 0 || n_results == 0) {
        LOG("scan: %lu results in %lu ms, %lu new, %u in table",
            (unsigned long)n_results, (unsigned long)(now_ms() - started_ms),
            (unsigned long)n, (unsigned)n_aps);
    }
    n_new = 0;
    return n;
}

size_t scan_count(void) {
    return n_aps;
}

const scan_ap_t *scan_get(size_t i) {
    return &aps[i];
}

void scan_clear(void) {
    n_aps = 0;
    full_logged = false;
}
