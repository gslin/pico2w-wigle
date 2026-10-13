// pico2w-wigle: scan Wi-Fi from a fixed location and upload the results to
// WiGLE every UPLOAD_INTERVAL_S seconds. Wi-Fi is only joined for uploads.

#include <stdio.h>

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "clock.h"
#include "config.h"
#include "net.h"
#include "scan.h"
#include "upload.h"

#define FIRST_UPLOAD_S  60   // shortly after boot, to show that uploads work
#define RETRY_UPLOAD_S  1800 // after a failed upload
#define WIFI_TIMEOUT_MS 30000
#define WATCHDOG_MS     8000

static void led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

static bool wifi_connect(void) {
    uint32_t auth = WIFI_PASSWORD[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    printf("wifi: joining %s\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, auth) != 0) return false;
    absolute_time_t deadline = make_timeout_time_ms(WIFI_TIMEOUT_MS);
    for (;;) {
        int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (status == CYW43_LINK_UP) return true;
        if (status < 0 || time_reached(deadline)) {
            printf("wifi: failed, link status %d\n", status);
            return false;
        }
        net_poll(10);
    }
}

static bool upload(void) {
    if (scan_count() == 0) {
        printf("upload: nothing to send\n");
        return true;
    }
    led(true);
    bool ok = false;
    if (wifi_connect()) {
        clock_sync();
        if (clock_valid()) {
            ok = upload_run();
        } else {
            printf("upload: clock not set, skipped\n");
        }
    }
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    led(false);
    if (ok) scan_clear();
    printf("upload: %s\n", ok ? "done" : "failed");
    return ok;
}

int main(void) {
    stdio_init_all();
    if (cyw43_arch_init_with_country(WIFI_COUNTRY) != 0) {
        printf("cyw43: init failed, rebooting\n");
        watchdog_reboot(0, 0, 1000);
        for (;;) tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();
    watchdog_enable(WATCHDOG_MS, true);
    printf("pico2w-wigle: location " GPS_LAT "," GPS_LON ", upload every %d s\n",
           UPLOAD_INTERVAL_S);

    absolute_time_t next_scan = get_absolute_time();
    absolute_time_t next_upload = make_timeout_time_ms(FIRST_UPLOAD_S * 1000);
    bool scanning = false;

    for (;;) {
        net_poll(100);
        if (scanning) {
            if (scan_active()) continue;
            scanning = false;
            uint32_t n = scan_take_new();
            if (n > 0) {
                printf("scan: %lu new, %u total\n", (unsigned long)n, (unsigned)scan_count());
                led(true);
                sleep_ms(50);
                led(false);
            }
        }
        if (time_reached(next_upload)) {
            uint32_t s = upload() ? UPLOAD_INTERVAL_S : RETRY_UPLOAD_S;
            next_upload = make_timeout_time_ms(s * 1000);
        } else if (time_reached(next_scan)) {
            if (scan_start() == 0) {
                scanning = true;
            } else {
                printf("scan: cannot start\n");
            }
            next_scan = make_timeout_time_ms(SCAN_INTERVAL_S * 1000);
        }
    }
}
