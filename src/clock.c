#include "clock.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "lwip/apps/sntp.h"
#include "pico/time.h"

#include "config.h"
#include "net.h"

#define SYNC_TIMEOUT_MS 15000

static bool valid;
static bool got_reply;

// lwIP SNTP hands over the time here (SNTP_SET_SYSTEM_TIME in lwipopts.h).
void clock_sntp_set(uint32_t sec) {
    struct timeval tv = {.tv_sec = sec};
    settimeofday(&tv, NULL);
    valid = got_reply = true;
}

bool clock_sync(void) {
    got_reply = false;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, NTP_SERVER);
    sntp_init();
    absolute_time_t deadline = make_timeout_time_ms(SYNC_TIMEOUT_MS);
    while (!got_reply && !time_reached(deadline)) net_poll(10);
    sntp_stop();
    if (got_reply) {
        printf("ntp: time is %lu\n", (unsigned long)time(NULL));
    } else {
        printf("ntp: no reply from %s\n", NTP_SERVER);
    }
    return got_reply;
}

bool clock_valid(void) {
    return valid;
}
