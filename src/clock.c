#include "clock.h"

#include <sys/time.h>
#include <time.h>

#include "lwip/apps/sntp.h"

#include "config.h"
#include "log.h"
#include "net.h"

#define SYNC_TIMEOUT_MS 15000

static bool valid;
static bool got_reply;

// lwIP SNTP hands over the time here (SNTP_SET_SYSTEM_TIME in lwipopts.h).
void clock_sntp_set(uint32_t sec) {
    struct timeval tv = {.tv_sec = sec};
    settimeofday(&tv, NULL);
    got_reply = true;
}

bool clock_sync(void) {
    uint32_t start = now_ms();
    time_t before = time(NULL);
    LOG("ntp: asking %s", NTP_SERVER);
    got_reply = false;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, NTP_SERVER);
    sntp_init();
    absolute_time_t deadline = make_timeout_time_ms(SYNC_TIMEOUT_MS);
    while (!got_reply && !time_reached(deadline)) net_poll(10);
    sntp_stop();

    uint32_t elapsed = now_ms() - start;
    if (!got_reply) {
        LOG("ntp: no reply in %lu ms; is UDP port 123 blocked?", (unsigned long)elapsed);
        return false;
    }
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    LOG("ntp: %04d-%02d-%02d %02d:%02d:%02d UTC from %s (%lu ms)",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
        ipaddr_ntoa(sntp_getserver(0)), (unsigned long)elapsed);
    if (valid) {
        LOG("ntp: clock moved by %ld s", (long)(now - before) - (long)(elapsed / 1000));
    }
    valid = true;
    return true;
}

bool clock_valid(void) {
    return valid;
}
