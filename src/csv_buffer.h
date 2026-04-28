// In-RAM buffer of Wigle WiFi CSV rows pending upload.
//
// The buffer holds the WigleWifi-1.4 header followed by one row per
// observation. When the row count crosses CSV_BUFFER_UPLOAD_THRESHOLD,
// main.c triggers an upload and clears the buffer.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Trigger an upload when the buffer reaches this many observations.
#define CSV_BUFFER_UPLOAD_THRESHOLD 1000

void   csv_buffer_init(void);
size_t csv_buffer_count(void);
size_t csv_buffer_size(void);
const char *csv_buffer_data(void);

// Begin a new scan session. Resets the per-session BSSID dedupe set so
// the same access point reported twice within one scan only counts
// once, but observations from different scans (different timestamps /
// GPS positions) are kept.
void csv_buffer_session_begin(void);

// Returns true if the BSSID has already been recorded in the current
// session.
bool csv_buffer_session_seen(const uint8_t bssid[6]);

// Append a single WigleWifi observation. Returns false if the buffer
// is full (caller should trigger an upload).
bool csv_buffer_append_observation(
    const uint8_t bssid[6],
    const char *ssid,            // not necessarily NUL-terminated
    size_t ssid_len,
    const char *auth_mode,       // e.g. "[WPA2-PSK-CCMP][ESS]"
    const char *first_seen_iso,  // "YYYY-MM-DD HH:MM:SS"
    int channel,
    int rssi,
    double latitude,
    double longitude,
    double altitude_m,
    double accuracy_m);

// Drop all buffered rows (called after a successful upload). The
// header is automatically re-emitted so the next upload is well-formed.
void csv_buffer_clear(void);
