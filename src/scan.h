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

size_t scan_count(void);
const scan_ap_t *scan_get(size_t i);
void scan_clear(void);

// Number of new BSSIDs since the previous call.
uint32_t scan_take_new(void);
