// Wall-clock time source.
//
// On a successful clock_sync_run() (one SNTP-style UDP exchange with
// 1.tw.pool.ntp.org) the module records the Unix epoch reported by
// the server together with the device uptime at that moment. Subsequent
// calls to clock_sync_now() advance from there using the monotonic
// uptime counter.
//
// Until the first successful sync, the fallback is BUILD_EPOCH (baked
// in at cmake configure time) plus uptime in seconds — enough to
// keep Wigle's FirstSeen column monotonic, but no guarantee of
// wall-clock accuracy.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Performs one NTP query against NTP_HOST. Blocking, ≤15 s.
// Caller must already hold a Wi-Fi association. Returns true on
// success, false on DNS / network / timeout failure (in which case
// any previous sync state is preserved).
bool clock_sync_run(void);

// Has clock_sync_run() ever succeeded? Useful for log lines.
bool clock_sync_is_synced(void);

// Current Unix epoch in seconds. If we have never synced, this
// returns BUILD_EPOCH + uptime_seconds.
time_t clock_sync_now(void);

// Format clock_sync_now() into "YYYY-MM-DD HH:MM:SS" (UTC).
// Buffer should be at least 20 bytes.
void clock_sync_format_iso(char *buf, size_t buflen);
