#include "clock_sync.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#define NTP_HOST           "1.tw.pool.ntp.org"
#define NTP_PORT           123
#define NTP_PACKET_LEN     48
// Seconds between the NTP epoch (1900-01-01) and the Unix epoch
// (1970-01-01). 70 years × 365.25 days ≈ this magic number.
#define NTP_TO_UNIX_DELTA  2208988800ULL

#define DNS_TIMEOUT_MS  10000
#define NTP_TIMEOUT_MS  5000

#ifndef BUILD_EPOCH
// 2026-01-01T00:00:00Z, used only if cmake didn't bake one in.
#define BUILD_EPOCH 1767225600
#endif

// ----- DNS one-shot --------------------------------------------------------

static volatile bool dns_done;
static volatile bool dns_ok;
static          ip_addr_t dns_addr;

static void dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name; (void)arg;
    if (ipaddr) {
        dns_addr = *ipaddr;
        dns_ok = true;
    }
    dns_done = true;
}

static int resolve_host(const char *host, ip_addr_t *out) {
    dns_done = false;
    dns_ok = false;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, &dns_addr, dns_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        *out = dns_addr;
        return 0;
    }
    if (err != ERR_INPROGRESS) {
        printf("clock_sync: dns immediate err=%d\n", err);
        return -1;
    }

    absolute_time_t deadline = make_timeout_time_ms(DNS_TIMEOUT_MS);
    while (!dns_done) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            printf("clock_sync: dns timeout\n");
            return -1;
        }
        sleep_ms(50);
    }
    if (!dns_ok) return -1;
    *out = dns_addr;
    return 0;
}

// ----- NTP exchange --------------------------------------------------------

static volatile bool     ntp_done;
static volatile bool     ntp_ok;
static volatile uint32_t ntp_unix_seconds;

static void ntp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr; (void)port;
    if (p && p->tot_len >= NTP_PACKET_LEN) {
        uint8_t buf[NTP_PACKET_LEN];
        pbuf_copy_partial(p, buf, NTP_PACKET_LEN, 0);
        // RFC 5905: the Transmit Timestamp ("xmt") sits at offset 40,
        // big-endian seconds since 1900-01-01. Fractional part at +44
        // is ignored; per-second resolution is plenty for FirstSeen.
        uint32_t ntp_sec =
            ((uint32_t)buf[40] << 24) |
            ((uint32_t)buf[41] << 16) |
            ((uint32_t)buf[42] << 8)  |
            ((uint32_t)buf[43]);
        if (ntp_sec > NTP_TO_UNIX_DELTA) {
            ntp_unix_seconds = ntp_sec - (uint32_t)NTP_TO_UNIX_DELTA;
            ntp_ok = true;
        }
    }
    if (p) pbuf_free(p);
    ntp_done = true;
}

// ----- module state --------------------------------------------------------

static bool             g_synced;
static time_t           g_sync_unix;
static absolute_time_t  g_sync_uptime;

bool clock_sync_is_synced(void) { return g_synced; }

bool clock_sync_run(void) {
    ip_addr_t server;
    if (resolve_host(NTP_HOST, &server) != 0) return false;

    ntp_done = false;
    ntp_ok = false;

    struct udp_pcb *pcb;
    cyw43_arch_lwip_begin();
    pcb = udp_new();
    if (pcb) udp_recv(pcb, ntp_recv_cb, NULL);
    cyw43_arch_lwip_end();
    if (!pcb) {
        printf("clock_sync: udp_new failed\n");
        return false;
    }

    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_LEN, PBUF_RAM);
    err_t err = ERR_MEM;
    if (p) {
        memset(p->payload, 0, NTP_PACKET_LEN);
        // LI=0 (no warning), VN=3, Mode=3 (client). All other fields
        // zero is fine for an unauthenticated SNTP request.
        ((uint8_t *)p->payload)[0] = 0x1B;
        err = udp_sendto(pcb, p, &server, NTP_PORT);
        pbuf_free(p);
    }
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("clock_sync: udp_sendto err=%d\n", err);
        cyw43_arch_lwip_begin();
        udp_remove(pcb);
        cyw43_arch_lwip_end();
        return false;
    }

    absolute_time_t deadline = make_timeout_time_ms(NTP_TIMEOUT_MS);
    while (!ntp_done) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            printf("clock_sync: ntp timeout\n");
            break;
        }
        sleep_ms(50);
    }

    cyw43_arch_lwip_begin();
    udp_remove(pcb);
    cyw43_arch_lwip_end();

    if (!ntp_ok) return false;

    g_sync_unix   = (time_t)ntp_unix_seconds;
    g_sync_uptime = get_absolute_time();
    g_synced      = true;

    printf("clock_sync: synced, now=%lu\n", (unsigned long)g_sync_unix);
    return true;
}

time_t clock_sync_now(void) {
    if (g_synced) {
        int64_t elapsed_us =
            absolute_time_diff_us(g_sync_uptime, get_absolute_time());
        if (elapsed_us < 0) elapsed_us = 0;
        return g_sync_unix + (time_t)(elapsed_us / 1000000);
    }
    uint64_t uptime_s = to_us_since_boot(get_absolute_time()) / 1000000ULL;
    return (time_t)((uint64_t)BUILD_EPOCH + uptime_s);
}

void clock_sync_format_iso(char *buf, size_t buflen) {
    if (buflen == 0) return;
    time_t now = clock_sync_now();
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tm);
}
