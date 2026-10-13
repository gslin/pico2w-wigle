// Wi-Fi scanning and the table of access points seen since the last
// successful upload, one entry per BSSID.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCAN_MAX_APS 2048

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid_len;
    uint8_t auth_mode;   // cyw43 bits: 1 = privacy, 2 = WPA, 4 = RSN
    uint8_t channel;
    int16_t rssi;        // strongest seen, dBm
    uint32_t first_seen; // seconds since boot
    char ssid[32];
} scan_ap_t;

int scan_start(void);
bool scan_active(void);

// Call once a scan has finished. Logs a summary and returns the number of
// new BSSIDs.
uint32_t scan_finish(void);

size_t scan_count(void);
const scan_ap_t *scan_get(size_t i);
void scan_clear(void);

// Android-style capability string, e.g. "[WPA2][ESS]".
const char *scan_capabilities(uint8_t auth_mode);
