// Console logging. Each line starts with the uptime in seconds, e.g.
// "[    12.345] wifi: up".
#pragma once

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/time.h"

#define LOG(fmt, ...)                                                   \
    do {                                                                \
        uint32_t log_ms_ = now_ms();                                    \
        printf("[%6lu.%03lu] " fmt "\n", (unsigned long)(log_ms_ / 1000), \
               (unsigned long)(log_ms_ % 1000), ##__VA_ARGS__);         \
    } while (0)

static inline uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// Heap in use and heap size, in bytes.
static inline uint32_t heap_used(void) {
    return mallinfo().uordblks;
}

static inline uint32_t heap_size(void) {
    extern char __end__, __HeapLimit;
    return (uint32_t)(&__HeapLimit - &__end__);
}
