#include "upload.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"
#include "pico/stdlib.h"
#include "pico/version.h"

#include "config.h"
#include "net.h"
#include "scan.h"

#define WIGLE_HOST  "api.wigle.net"
#define WIGLE_PORT  443
#define WIGLE_PATH  "/api/v2/file/upload"
#define APP_VERSION "1.0"
// CSV lines start with a MAC address, so this never appears in the body.
#define BOUNDARY    "pico2w-wigle-5f0c2e9a"

// https://api.wigle.net/csvFormat.html
#define CSV_HEADER                                                        \
    "WigleWifi-1.6,appRelease=" APP_VERSION ",model=Pico 2 W,"            \
    "release=" PICO_SDK_VERSION_STRING ",device=pico2w-wigle,display=,"   \
    "board=RP2350,brand=Raspberry Pi,star=Sol,body=3,subBody=0\n"         \
    "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude," \
    "CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n"

static const char ca_pem[] = CA_PEM;

static net_conn_t conn;
static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt ca;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;

// Outgoing data is collected into TLS records of up to sizeof(obuf) bytes;
// the first error sticks in out_err.
static unsigned char obuf[1024];
static size_t olen;
static int out_err;

// Needed by MBEDTLS_PLATFORM_MS_TIME_ALT.
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return to_ms_since_boot(get_absolute_time());
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int n = net_write(ctx, buf, len);
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int n = net_read(ctx, buf, len);
    return n < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : n;
}

static void print_error(const char *what, int err) {
    char msg[128] = "network error";
    // mbedtls_strerror() only knows the NET codes with MBEDTLS_NET_C.
    if (err != MBEDTLS_ERR_NET_CONNECT_FAILED && err != MBEDTLS_ERR_NET_SEND_FAILED &&
        err != MBEDTLS_ERR_NET_RECV_FAILED) {
        mbedtls_strerror(err, msg, sizeof msg);
    }
    printf("upload: %s: %s (-0x%04x)\n", what, msg, (unsigned)-err);
}

static void out_flush(void) {
    for (size_t off = 0; off < olen && !out_err;) {
        int r = mbedtls_ssl_write(&ssl, obuf + off, olen - off);
        if (r > 0) {
            off += r;
        } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            out_err = r;
        }
    }
    olen = 0;
}

static void out(const void *data, size_t len) {
    const unsigned char *p = data;
    while (len > 0) {
        size_t n = MIN(len, sizeof obuf - olen);
        memcpy(obuf + olen, p, n);
        olen += n;
        p += n;
        len -= n;
        if (olen == sizeof obuf) out_flush();
    }
}

// Android-style capability string; WiGLE looks for WPA2/WPA/WEP in it.
static const char *capabilities(uint8_t auth) {
    if (auth & 4) return auth & 2 ? "[WPA][WPA2][ESS]" : "[WPA2][ESS]";
    if (auth & 2) return "[WPA][ESS]";
    if (auth & 1) return "[WEP][ESS]";
    return "[ESS]";
}

// SSID as a CSV field (RFC 4180), without control characters. dst needs
// 2 * 32 + 3 bytes.
static void csv_ssid(char *dst, const scan_ap_t *a) {
    char *p = dst + 1;
    bool quote = false;
    for (size_t i = 0; i < a->ssid_len; i++) {
        char ch = a->ssid[i];
        if ((unsigned char)ch < 0x20 || ch == 0x7f) continue;
        if (ch == ',' || ch == '"') quote = true;
        if (ch == '"') *p++ = '"';
        *p++ = ch;
    }
    *p = '\0';
    if (quote) {
        dst[0] = '"';
        p[0] = '"';
        p[1] = '\0';
    } else {
        memmove(dst, dst + 1, p - dst);
    }
}

// Returns the row length; offset converts uptime seconds to Unix time.
static size_t format_row(char *buf, size_t size, const scan_ap_t *a, time_t offset) {
    char ssid[2 * sizeof a->ssid + 3];
    csv_ssid(ssid, a);
    time_t t = offset + a->first_seen;
    struct tm tm;
    gmtime_r(&t, &tm);
    // The CYW43439 is a 2.4 GHz radio.
    int freq = a->channel == 14 ? 2484 : 2407 + 5 * a->channel;
    int n = snprintf(buf, size,
                     "%02x:%02x:%02x:%02x:%02x:%02x,%s,%s,"
                     "%04d-%02d-%02d %02d:%02d:%02d,%u,%d,%d,"
                     GPS_LAT "," GPS_LON "," GPS_ALT "," GPS_ACC ",,,WIFI\n",
                     a->bssid[0], a->bssid[1], a->bssid[2],
                     a->bssid[3], a->bssid[4], a->bssid[5],
                     ssid, capabilities(a->auth_mode),
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec,
                     a->channel, freq, a->rssi);
    return MIN((size_t)n, size - 1);
}

static int tls_connect(void) {
    int r;
    if ((r = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0 ||
        (r = mbedtls_x509_crt_parse(&ca, (const unsigned char *)ca_pem, sizeof ca_pem)) != 0 ||
        (r = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        return r;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    if ((r = mbedtls_ssl_setup(&ssl, &conf)) != 0 ||
        (r = mbedtls_ssl_set_hostname(&ssl, WIGLE_HOST)) != 0) {
        return r;
    }

    if (PROXY_HOST[0]) {
        printf("upload: via SOCKS5 proxy %s:%d\n", PROXY_HOST, PROXY_PORT);
        if (net_connect(&conn, PROXY_HOST, PROXY_PORT) < 0 ||
            net_socks5(&conn, WIGLE_HOST, WIGLE_PORT, PROXY_USER, PROXY_PASS) < 0) {
            return MBEDTLS_ERR_NET_CONNECT_FAILED;
        }
    } else if (net_connect(&conn, WIGLE_HOST, WIGLE_PORT) < 0) {
        return MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    mbedtls_ssl_set_bio(&ssl, &conn, bio_send, bio_recv, NULL);
    r = mbedtls_ssl_handshake(&ssl);
    uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
    if (r != 0 && flags != 0 && flags != (uint32_t)-1) {
        char info[256];
        mbedtls_x509_crt_verify_info(info, sizeof info, "  ", flags);
        printf("upload: certificate verification failed:\n%s", info);
    }
    return r;
}

static void send_request(time_t now) {
    static const char tail[] = "\r\n--" BOUNDARY "--\r\n";
    static const char cred[] = WIGLE_API_NAME ":" WIGLE_API_TOKEN;
    time_t offset = now - (time_t)(to_ms_since_boot(get_absolute_time()) / 1000);
    // Static to keep them off the small main stack.
    static char row[256], part[192], auth[192], head[512];
    size_t alen;

    int plen = snprintf(part, sizeof part,
                        "--" BOUNDARY "\r\n"
                        "Content-Disposition: form-data; name=\"file\"; "
                        "filename=\"pico2w-wigle-%lu.csv\"\r\n"
                        "Content-Type: text/csv\r\n"
                        "\r\n",
                        (unsigned long)now);
    size_t body = plen + strlen(CSV_HEADER) + strlen(tail);
    for (size_t i = 0; i < scan_count(); i++) {
        body += format_row(row, sizeof row, scan_get(i), offset);
    }

    if (mbedtls_base64_encode((unsigned char *)auth, sizeof auth, &alen,
                              (const unsigned char *)cred, strlen(cred)) != 0) {
        out_err = MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
        return;
    }
    int hlen = snprintf(head, sizeof head,
                        "POST " WIGLE_PATH " HTTP/1.1\r\n"
                        "Host: " WIGLE_HOST "\r\n"
                        "Authorization: Basic %s\r\n"
                        "User-Agent: pico2w-wigle/" APP_VERSION "\r\n"
                        "Accept: application/json\r\n"
                        "Content-Type: multipart/form-data; boundary=" BOUNDARY "\r\n"
                        "Content-Length: %u\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        auth, (unsigned)body);

    out(head, hlen);
    out(part, plen);
    out(CSV_HEADER, strlen(CSV_HEADER));
    for (size_t i = 0; i < scan_count(); i++) {
        out(row, format_row(row, sizeof row, scan_get(i), offset));
    }
    out(tail, strlen(tail));
    out_flush();
}

// Reads until the server closes the connection or buf is full. Returns the
// HTTP status code, or 0 if there is none.
static int read_response(char *buf, size_t size) {
    size_t len = 0;
    while (len < size - 1) {
        int r = mbedtls_ssl_read(&ssl, (unsigned char *)buf + len, size - 1 - len);
        if (r > 0) {
            len += r;
        } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        }
    }
    buf[len] = '\0';
    int status = 0;
    sscanf(buf, "HTTP/%*s %d", &status);
    return status;
}

// Looks for "success": true in the JSON reply.
static bool json_success(const char *s) {
    const char *p = strstr(s, "\"success\"");
    if (!p) return false;
    p += strlen("\"success\"");
    while (*p == ' ' || *p == ':') p++;
    return strncmp(p, "true", 4) == 0;
}

bool upload_run(void) {
    static char resp[1024];
    bool ok = false;

    printf("upload: %u networks to https://" WIGLE_HOST WIGLE_PATH "\n",
           (unsigned)scan_count());
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&ca);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    olen = 0;
    out_err = 0;

    int r = tls_connect();
    if (r != 0) {
        print_error("TLS connection failed", r);
    } else {
        send_request(time(NULL));
        if (out_err) {
            print_error("send failed", out_err);
        } else {
            int status = read_response(resp, sizeof resp);
            const char *body = strstr(resp, "\r\n\r\n");
            body = body ? body + 4 : "";
            printf("upload: HTTP %d %s\n", status, body);
            ok = status == 200 && json_success(body);
        }
        mbedtls_ssl_close_notify(&ssl);
    }

    net_close(&conn);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&ca);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}
