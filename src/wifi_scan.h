// Wraps cyw43_wifi_scan and pumps results into csv_buffer.

#pragma once

#include <stdbool.h>

#include <stdint.h>

void wifi_scan_init(void);

// Kick off an asynchronous scan. Returns 0 on success. Safe to call
// even when a scan is already running (it becomes a no-op).
int  wifi_scan_start(void);

// True while the chip is still reporting results from the last scan.
bool wifi_scan_busy(void);

// Returns the number of brand-new (never seen since boot) BSSIDs
// observed since the last call to this function, and resets the
// counter. Used by main.c to drive the per-discovery LED blink.
uint32_t wifi_scan_drain_new_bssid_count(void);
