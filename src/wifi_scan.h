// Wraps cyw43_wifi_scan and pumps results into csv_buffer.

#pragma once

#include <stdbool.h>

void wifi_scan_init(void);

// Kick off an asynchronous scan. Returns 0 on success. Safe to call
// even when a scan is already running (it becomes a no-op).
int  wifi_scan_start(void);

// True while the chip is still reporting results from the last scan.
bool wifi_scan_busy(void);
