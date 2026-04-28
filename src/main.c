// pico2w-wigle: continuously scan Wi-Fi access points, tag with GPS
// coordinates, and upload to wigle.net every CSV_BUFFER_UPLOAD_THRESHOLD
// observations.
//
// Hardware: Raspberry Pi Pico 2 W (RP2350 + CYW43439) plus an external
// UART GPS module wired to GP5 (UART1 RX). See gps.h for the pinout.

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "csv_buffer.h"
#include "gps.h"
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
    printf("Waiting for GPS fix on UART1 (GP5)...\n");

    absolute_time_t next_scan = make_timeout_time_ms(SCAN_INTERVAL_MS);
    bool announced_fix = false;

    while (true) {
        gps_poll();

        if (!announced_fix && gps_has_fix()) {
            announced_fix = true;
            const gps_state_t *g = gps_get_state();
            printf("GPS fix: %.6f,%.6f  alt=%.1fm sats=%d\n",
                   g->latitude, g->longitude, g->altitude_m, g->satellites);
        }

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

        // Yield to lwip / cyw43 work queues. In threadsafe_background
        // mode this is a no-op but keeping it here means the same
        // source compiles under the poll variant too.
        cyw43_arch_poll();
        sleep_ms(50);
    }
}
