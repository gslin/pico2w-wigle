// pico2w-wigle: continuously scan Wi-Fi access points, tag them with
// the device's fixed GPS coordinates, and upload to wigle.net every
// CSV_BUFFER_UPLOAD_THRESHOLD observations.

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "clock_sync.h"
#include "csv_buffer.h"
#include "gps.h"
#include "secrets.h"
#include "wifi_scan.h"
#include "wigle_upload.h"

// Time between scan kick-offs. Each scan typically reports for ~3 s
// before going quiet, so 8 s leaves a small idle gap and keeps
// duty-cycle reasonable.
#define SCAN_INTERVAL_MS 8000

static void blink_led(int times, int gap_ms) {
    for (int i = 0; i < times; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(80);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(gap_ms);
    }
}

// Brief Wi-Fi cycle at boot purely to bootstrap the wall clock from
// NTP, so even the very first batch's FirstSeen column is real time
// rather than BUILD_EPOCH + uptime. Best-effort: any failure leaves
// clock_sync on its fallback baseline; the upload-time sync will try
// again later.
static void bootstrap_clock(void) {
    printf("boot: associating with \"%s\" for initial NTP...\n",
           UPLOAD_WIFI_SSID);
    int err = cyw43_arch_wifi_connect_timeout_ms(
        UPLOAD_WIFI_SSID,
        UPLOAD_WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK,
        30000);
    if (err) {
        printf("boot: wifi connect failed (%d); first batch will use "
               "BUILD_EPOCH timestamps\n", err);
        return;
    }
    if (clock_sync_run()) {
        printf("boot: NTP sync ok\n");
    } else {
        printf("boot: NTP sync failed; first batch will use BUILD_EPOCH "
               "timestamps\n");
    }
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);  // give USB CDC time to enumerate

    printf("\npico2w-wigle starting up\n");

    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    gps_init();
    csv_buffer_init();
    wifi_scan_init();

    blink_led(2, 200);
    {
        const gps_state_t *g = gps_get_state();
        printf("Fixed location: %.6f,%.6f  alt=%.1fm acc=%.1fm\n",
               g->latitude, g->longitude, g->altitude_m, g->accuracy_m);
    }

    bootstrap_clock();

    absolute_time_t next_scan = make_timeout_time_ms(SCAN_INTERVAL_MS);

    while (true) {
        gps_poll();

        // Trigger an upload as soon as the buffer is full, before
        // starting more scans (so we don't drop observations).
        if (csv_buffer_count() >= CSV_BUFFER_UPLOAD_THRESHOLD) {
            printf("Buffer full (%u rows, %u bytes). Uploading...\n",
                   (unsigned)csv_buffer_count(),
                   (unsigned)csv_buffer_size());

            int rc = wigle_upload_run(csv_buffer_data(),
                                      csv_buffer_size());
            if (rc == 0) {
                printf("Upload OK; clearing buffer.\n");
                csv_buffer_clear();
                blink_led(3, 120);
            } else {
                printf("Upload failed (rc=%d). Will retry in %d ms.\n",
                       rc, SCAN_INTERVAL_MS);
                blink_led(5, 60);
            }

            next_scan = make_timeout_time_ms(SCAN_INTERVAL_MS);
        }

        // Kick off a scan when due, provided we know where we are.
        if (gps_has_fix()
            && !wifi_scan_busy()
            && absolute_time_diff_us(get_absolute_time(), next_scan) <= 0)
        {
            wifi_scan_start();
            next_scan = make_timeout_time_ms(SCAN_INTERVAL_MS);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        } else if (!wifi_scan_busy()) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }

        // Per-discovery flash. The scan callback queues one count for
        // each first-ever BSSID; we render each as a brief ~80 ms
        // OFF/ON wink. Cap the burst so a noisy environment can't
        // stall the loop indefinitely. The next pass through the
        // scan-state branches above will restore the normal
        // on-during-scan / off-when-idle pattern.
        uint32_t flashes = wifi_scan_drain_new_bssid_count();
        if (flashes > 10) flashes = 10;
        for (uint32_t i = 0; i < flashes; i++) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(40);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(40);
        }

        // Yield to lwip / cyw43 work queues. In threadsafe_background
        // mode this is a no-op but keeping it here means the same
        // source compiles under the poll variant too.
        cyw43_arch_poll();
        sleep_ms(50);
    }
}
