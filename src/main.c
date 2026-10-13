// pico2w-wigle: scan Wi-Fi from a fixed location and upload the results to
// WiGLE every UPLOAD_INTERVAL_S seconds. Wi-Fi is only joined for uploads.

#include "hardware/watchdog.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/version.h"

#include "clock.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "scan.h"
#include "upload.h"

#define FIRST_UPLOAD_S    60   // shortly after boot, to show that uploads work
#define RETRY_UPLOAD_S    1800 // after a failed upload
#define STATUS_INTERVAL_S 60   // status line on the console
#define WIFI_TIMEOUT_MS   30000
#define WATCHDOG_MS       8000

static uint32_t n_scans;
static const char *last_upload; // "ok" or "failed"
static uint32_t last_upload_ms;

static void led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

static const char *link_name(int status) {
    switch (status) {
    case CYW43_LINK_DOWN: return "down";
    case CYW43_LINK_JOIN: return "joining";
    case CYW43_LINK_NOIP: return "joined, waiting for DHCP";
    case CYW43_LINK_UP: return "up";
    case CYW43_LINK_FAIL: return "failed";
    case CYW43_LINK_NONET: return "network not found";
    case CYW43_LINK_BADAUTH: return "wrong password";
    default: return "unknown";
    }
}

static void log_config(void) {
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    LOG("wifi: MAC %02x:%02x:%02x:%02x:%02x:%02x, country %c%c",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        (char)(WIFI_COUNTRY & 0xff), (char)((WIFI_COUNTRY >> 8) & 0xff));
    LOG("config: upload network \"%s\" (%s)", WIFI_SSID, WIFI_PASSWORD[0] ? "WPA2" : "open");
    LOG("config: location " GPS_LAT "," GPS_LON ", altitude " GPS_ALT " m, accuracy " GPS_ACC " m");
    LOG("config: scan every %d s, upload every %d s, first upload after %d s",
        SCAN_INTERVAL_S, UPLOAD_INTERVAL_S, FIRST_UPLOAD_S);
    LOG("config: WiGLE API name %s, NTP server %s", WIGLE_API_NAME, NTP_SERVER);
    if (PROXY_HOST[0]) {
        LOG("config: SOCKS5 proxy %s:%d%s", PROXY_HOST, PROXY_PORT,
            PROXY_USER[0] ? ", with username/password" : "");
    } else {
        LOG("config: no proxy");
    }
}

static void log_status(absolute_time_t next_upload) {
    long next = (long)(absolute_time_diff_us(get_absolute_time(), next_upload) / 1000000);
    char last[40] = "none yet";
    if (last_upload) {
        snprintf(last, sizeof last, "%s %lu s ago", last_upload,
                 (unsigned long)((now_ms() - last_upload_ms) / 1000));
    }
    LOG("status: %lu scans, %u networks in table, next upload in %ld s, "
        "last upload %s, heap %lu of %lu bytes in use",
        (unsigned long)n_scans, (unsigned)scan_count(), next, last,
        (unsigned long)heap_used(), (unsigned long)heap_size());
}

static bool wifi_connect(void) {
    uint32_t start = now_ms();
    uint32_t auth = WIFI_PASSWORD[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    LOG("wifi: joining \"%s\"", WIFI_SSID);
    int err = cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, auth);
    if (err != 0) {
        LOG("wifi: cannot start joining (error %d)", err);
        return false;
    }
    absolute_time_t deadline = make_timeout_time_ms(WIFI_TIMEOUT_MS);
    int last = CYW43_LINK_DOWN;
    for (;;) {
        int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (status != last) {
            LOG("wifi: %s (%lu ms)", link_name(status), (unsigned long)(now_ms() - start));
            last = status;
        }
        if (status == CYW43_LINK_UP) break;
        if (status < 0) return false;
        if (time_reached(deadline)) {
            LOG("wifi: gave up after %d ms", WIFI_TIMEOUT_MS);
            return false;
        }
        net_poll(10);
    }

    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    char ip[IP4ADDR_STRLEN_MAX], mask[IP4ADDR_STRLEN_MAX], gw[IP4ADDR_STRLEN_MAX];
    char dns[IPADDR_STRLEN_MAX];
    int32_t rssi = 0;
    ip4addr_ntoa_r(netif_ip4_addr(n), ip, sizeof ip);
    ip4addr_ntoa_r(netif_ip4_netmask(n), mask, sizeof mask);
    ip4addr_ntoa_r(netif_ip4_gw(n), gw, sizeof gw);
    ipaddr_ntoa_r(dns_getserver(0), dns, sizeof dns);
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    LOG("wifi: IP %s, netmask %s, gateway %s, DNS %s, RSSI %ld dBm",
        ip, mask, gw, dns, (long)rssi);
    return true;
}

static bool upload(void) {
    if (scan_count() == 0) {
        LOG("upload: nothing to send");
        return true;
    }
    led(true);
    bool ok = false;
    if (wifi_connect()) {
        clock_sync();
        if (clock_valid()) {
            ok = upload_run();
        } else {
            LOG("upload: skipped, the clock was never set");
        }
    }
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    led(false);
    if (ok) scan_clear();
    last_upload = ok ? "ok" : "failed";
    last_upload_ms = now_ms();
    return ok;
}

int main(void) {
    stdio_init_all();
    LOG("pico2w-wigle " APP_VERSION ", built " __DATE__ " " __TIME__
        ", pico-sdk " PICO_SDK_VERSION_STRING);
    LOG("reset: %s", watchdog_enable_caused_reboot() ? "watchdog timeout" : "power-on or reset");
    if (cyw43_arch_init_with_country(WIFI_COUNTRY) != 0) {
        LOG("cyw43: init failed, rebooting");
        watchdog_reboot(0, 0, 1000);
        for (;;) tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();
    watchdog_enable(WATCHDOG_MS, true);
    log_config();

    absolute_time_t next_scan = get_absolute_time();
    absolute_time_t next_upload = make_timeout_time_ms(FIRST_UPLOAD_S * 1000);
    absolute_time_t next_status = make_timeout_time_ms(STATUS_INTERVAL_S * 1000);
    bool scanning = false;

    for (;;) {
        net_poll(100);
        if (scanning) {
            if (scan_active()) continue;
            scanning = false;
            n_scans++;
            if (scan_finish() > 0) {
                led(true);
                sleep_ms(50);
                led(false);
            }
        }
        if (time_reached(next_status)) {
            log_status(next_upload);
            next_status = make_timeout_time_ms(STATUS_INTERVAL_S * 1000);
        }
        if (time_reached(next_upload)) {
            uint32_t s = upload() ? UPLOAD_INTERVAL_S : RETRY_UPLOAD_S;
            next_upload = make_timeout_time_ms(s * 1000);
            LOG("upload: next one in %lu s", (unsigned long)s);
        } else if (time_reached(next_scan)) {
            int err = scan_start();
            if (err == 0) {
                scanning = true;
            } else {
                LOG("scan: cannot start (error %d)", err);
            }
            next_scan = make_timeout_time_ms(SCAN_INTERVAL_S * 1000);
        }
    }
}
