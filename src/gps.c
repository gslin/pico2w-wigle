#include "gps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/uart.h"
#include "pico/stdlib.h"

// UART1 with RX on GP5. GPS modules typically default to 9600 8N1.
#define GPS_UART      uart1
#define GPS_UART_TX_PIN 4
#define GPS_UART_RX_PIN 5
#define GPS_BAUD      9600

static gps_state_t state;

// Parser state: accumulate one NMEA sentence at a time.
#define LINE_BUF_MAX 96
static char  line_buf[LINE_BUF_MAX];
static size_t line_len;

// Date and time are reported on different sentences. We hold them
// separately so we can stitch them when we have both.
static char date_ddmmyy[7];   // "DDMMYY" + NUL, from RMC
static char time_hhmmss[7];   // "HHMMSS" + NUL, from GGA/RMC

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

static bool checksum_ok(const char *line, size_t len) {
    // Expected: "$....*XX". XOR everything between '$' and '*'.
    if (len < 4 || line[0] != '$') return false;
    const char *star = memchr(line, '*', len);
    if (!star || star + 2 >= line + len) return false;
    int hi = hexval(star[1]);
    int lo = hexval(star[2]);
    if (hi < 0 || lo < 0) return false;
    uint8_t want = (uint8_t)((hi << 4) | lo);
    uint8_t got = 0;
    for (const char *p = line + 1; p < star; p++) got ^= (uint8_t)*p;
    return got == want;
}

// Convert "ddmm.mmmm" + hemisphere into signed decimal degrees.
// Longitudes use "dddmm.mmmm"; the only difference is how many digits
// belong to the integer-degree component, which split_deg infers from
// the position of the decimal point.
static double parse_latlon(const char *s, char hemi) {
    if (!*s) return 0.0;
    const char *dot = strchr(s, '.');
    if (!dot) return 0.0;
    // Two digits to the left of the dot are minutes; the rest is
    // degrees.
    int min_int_digits = 2;
    size_t left = (size_t)(dot - s);
    if (left < (size_t)min_int_digits) return 0.0;
    char deg_buf[8];
    size_t deg_digits = left - min_int_digits;
    if (deg_digits >= sizeof(deg_buf)) return 0.0;
    memcpy(deg_buf, s, deg_digits);
    deg_buf[deg_digits] = '\0';
    double deg = atof(deg_buf);
    double minutes = atof(s + deg_digits);
    double v = deg + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') v = -v;
    return v;
}

// Split a NMEA sentence body in place into comma-separated fields.
// `out[i]` points to field i (which may be empty). Returns the number
// of fields written. The trailing "*XX" checksum suffix, if any, is
// dropped.
#define MAX_FIELDS 20
static int split_fields(char *body, char *out[MAX_FIELDS]) {
    int n = 0;
    out[n++] = body;
    for (char *p = body; *p; p++) {
        if (*p == ',') {
            *p = '\0';
            if (n < MAX_FIELDS) out[n++] = p + 1;
        } else if (*p == '*') {
            *p = '\0';
            break;
        }
    }
    return n;
}

static char *field_or_empty(char *f[MAX_FIELDS], int n, int idx) {
    if (idx < n && f[idx]) return f[idx];
    return "";
}

static void rebuild_timestamp(void) {
    if (date_ddmmyy[0] == '\0' || time_hhmmss[0] == '\0') {
        state.timestamp[0] = '\0';
        return;
    }
    int dd = (date_ddmmyy[0]-'0')*10 + (date_ddmmyy[1]-'0');
    int mm = (date_ddmmyy[2]-'0')*10 + (date_ddmmyy[3]-'0');
    int yy = (date_ddmmyy[4]-'0')*10 + (date_ddmmyy[5]-'0');
    int hh = (time_hhmmss[0]-'0')*10 + (time_hhmmss[1]-'0');
    int mi = (time_hhmmss[2]-'0')*10 + (time_hhmmss[3]-'0');
    int ss = (time_hhmmss[4]-'0')*10 + (time_hhmmss[5]-'0');
    snprintf(state.timestamp, sizeof(state.timestamp),
             "%04d-%02d-%02d %02d:%02d:%02d",
             2000 + yy, mm, dd, hh, mi, ss);
}

// $..GGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,fix,sats,hdop,alt,M,...
static void handle_gga(char *body) {
    char *f[MAX_FIELDS] = {0};
    int n = split_fields(body, f);

    char *t    = field_or_empty(f, n, 0);
    char *lat  = field_or_empty(f, n, 1);
    char *ns   = field_or_empty(f, n, 2);
    char *lon  = field_or_empty(f, n, 3);
    char *ew   = field_or_empty(f, n, 4);
    char *fix  = field_or_empty(f, n, 5);
    char *sats = field_or_empty(f, n, 6);
    char *hdop = field_or_empty(f, n, 7);
    char *alt  = field_or_empty(f, n, 8);

    int fix_quality = atoi(fix);
    state.has_fix = (fix_quality >= 1);
    if (!state.has_fix) return;

    if (*lat && *ns)  state.latitude   = parse_latlon(lat, *ns);
    if (*lon && *ew)  state.longitude  = parse_latlon(lon, *ew);
    if (*alt)         state.altitude_m = atof(alt);
    if (*sats)        state.satellites = atoi(sats);
    if (*hdop) {
        // Rough horizontal-position 1-sigma in metres. 5 m * HDOP is a
        // common rule of thumb for consumer GPS.
        state.accuracy_m = 5.0 * atof(hdop);
    }

    if (strlen(t) >= 6) {
        memcpy(time_hhmmss, t, 6);
        time_hhmmss[6] = '\0';
        rebuild_timestamp();
    }
}

// $..RMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,knots,deg,ddmmyy,...
static void handle_rmc(char *body) {
    char *f[MAX_FIELDS] = {0};
    int n = split_fields(body, f);

    char *t      = field_or_empty(f, n, 0);
    char *status = field_or_empty(f, n, 1);
    char *date   = field_or_empty(f, n, 8);

    if (*status == 'A') state.has_fix = true;
    if (strlen(date) >= 6) {
        memcpy(date_ddmmyy, date, 6);
        date_ddmmyy[6] = '\0';
    }
    if (strlen(t) >= 6) {
        memcpy(time_hhmmss, t, 6);
        time_hhmmss[6] = '\0';
    }
    rebuild_timestamp();
}

static void handle_line(char *line, size_t len) {
    if (!checksum_ok(line, len)) return;
    char *star = strchr(line, '*');
    if (star) *star = '\0';

    // Talker IDs vary (GP, GN, GL, GA, ...). Match the 3-letter
    // sentence type at offset 3.
    if (len < 7) return;
    const char *type = line + 3;
    if (strncmp(type, "GGA", 3) == 0) {
        handle_gga(line + 6 + 1); // skip "$XXGGA,"
    } else if (strncmp(type, "RMC", 3) == 0) {
        handle_rmc(line + 6 + 1);
    }
}

void gps_init(void) {
    memset(&state, 0, sizeof(state));
    line_len = 0;
    date_ddmmyy[0] = '\0';
    time_hhmmss[0] = '\0';
    state.accuracy_m = 50.0; // pessimistic until first HDOP reading

    uart_init(GPS_UART, GPS_BAUD);
    gpio_set_function(GPS_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(GPS_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(GPS_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(GPS_UART, true);
}

void gps_poll(void) {
    while (uart_is_readable(GPS_UART)) {
        char c = (char)uart_getc(GPS_UART);
        if (c == '\r') continue;
        if (c == '\n') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                handle_line(line_buf, line_len);
            }
            line_len = 0;
            continue;
        }
        if (line_len + 1 < LINE_BUF_MAX) {
            line_buf[line_len++] = c;
        } else {
            // Overlong line, discard.
            line_len = 0;
        }
    }
}

bool gps_has_fix(void) { return state.has_fix; }

const gps_state_t *gps_get_state(void) { return &state; }
