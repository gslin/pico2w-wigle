#include "csv_buffer.h"

#include <stdio.h>
#include <string.h>

// Sized for ~1000 rows of typical Wigle CSV output (≈140 B each) plus
// the header. RP2350 has 520 KB SRAM so this is comfortable.
#define CSV_BUFFER_CAPACITY (192 * 1024)

// Header line + column line, per WigleWifi-1.4. The values in the
// banner row are cosmetic; Wigle accepts anything but expects the
// shape.
static const char CSV_HEADER[] =
    "WigleWifi-1.4,appRelease=1.0,model=Pico2W,release=" __DATE__
    ",device=pico2w,display=pico2w,board=pico2w,brand=RaspberryPi\n"
    "MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
    "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n";

static char   buffer[CSV_BUFFER_CAPACITY];
static size_t buffer_len;
static size_t entry_count;

#define SESSION_DEDUP_MAX 256
static uint8_t session_seen[SESSION_DEDUP_MAX][6];
static size_t  session_seen_count;

// Global (per-boot) BSSID set. Linear search is fine: ~512 × 6 cmp
// per scan callback is negligible. If we overflow the table, new
// BSSIDs read as "already seen" — silently dropping the LED blink,
// not affecting Wigle data.
#define GLOBAL_DEDUP_MAX 512
static uint8_t global_seen[GLOBAL_DEDUP_MAX][6];
static size_t  global_seen_count;

static void emit_header(void) {
    size_t n = sizeof(CSV_HEADER) - 1;
    memcpy(buffer, CSV_HEADER, n);
    buffer_len  = n;
    entry_count = 0;
}

void csv_buffer_init(void) {
    emit_header();
    session_seen_count = 0;
    global_seen_count  = 0;
}

size_t      csv_buffer_count(void) { return entry_count; }
size_t      csv_buffer_size(void)  { return buffer_len; }
const char *csv_buffer_data(void)  { return buffer; }

void csv_buffer_clear(void) {
    emit_header();
}

void csv_buffer_session_begin(void) {
    session_seen_count = 0;
}

bool csv_buffer_session_seen(const uint8_t bssid[6]) {
    for (size_t i = 0; i < session_seen_count; i++) {
        if (memcmp(session_seen[i], bssid, 6) == 0) return true;
    }
    if (session_seen_count < SESSION_DEDUP_MAX) {
        memcpy(session_seen[session_seen_count++], bssid, 6);
    }
    // If we exceed the dedupe table the worst case is that we append
    // a duplicate row in the same scan; not a correctness problem for
    // Wigle, just slightly wasteful.
    return false;
}

bool csv_buffer_global_seen(const uint8_t bssid[6]) {
    for (size_t i = 0; i < global_seen_count; i++) {
        if (memcmp(global_seen[i], bssid, 6) == 0) return true;
    }
    if (global_seen_count < GLOBAL_DEDUP_MAX) {
        memcpy(global_seen[global_seen_count++], bssid, 6);
    }
    return false;
}

// Strip characters that would break a Wigle CSV cell.
static size_t copy_clean(char *dst, size_t dst_cap,
                         const char *src, size_t src_len) {
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dst_cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0)               break;     // ssid is space-padded NULs
        if (c == ',' || c == '\n' || c == '\r' || c == '"') continue;
        if (c < 0x20)             continue;
        dst[out++] = (char)c;
    }
    dst[out] = '\0';
    return out;
}

bool csv_buffer_append_observation(
    const uint8_t bssid[6],
    const char *ssid, size_t ssid_len,
    const char *auth_mode,
    const char *first_seen_iso,
    int channel, int rssi,
    double latitude, double longitude,
    double altitude_m, double accuracy_m)
{
    char ssid_clean[64];
    copy_clean(ssid_clean, sizeof(ssid_clean), ssid, ssid_len);

    // Format directly into the tail of the buffer.
    size_t remaining = CSV_BUFFER_CAPACITY - buffer_len;
    if (remaining < 192) return false;

    int written = snprintf(
        buffer + buffer_len, remaining,
        "%02X:%02X:%02X:%02X:%02X:%02X,"
        "%s,%s,%s,%d,%d,"
        "%.6f,%.6f,%.1f,%.1f,WIFI\n",
        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
        ssid_clean, auth_mode, first_seen_iso,
        channel, rssi,
        latitude, longitude, altitude_m, accuracy_m);

    if (written <= 0 || (size_t)written >= remaining) return false;

    buffer_len += (size_t)written;
    entry_count++;
    return true;
}
