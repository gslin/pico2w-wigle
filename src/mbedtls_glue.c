// Pico-side glue called by mbedtls 3.x's library code.
//
// MBEDTLS_PLATFORM_MS_TIME_ALT in mbedtls_config.h tells the library
// to skip its own mbedtls_ms_time() definition and link against the
// one we provide here. Any monotonically advancing millisecond
// counter is acceptable; mbedtls only uses it for DRBG reseed
// bookkeeping and TLS session timestamps, neither of which need a
// real wall clock.

#include "pico/stdlib.h"
#include "mbedtls/platform_time.h"

mbedtls_ms_time_t mbedtls_ms_time(void) {
    // RP2350 hardware timer: 64-bit microseconds since boot. Divide
    // to get ms; cast to mbedtls's signed 64-bit time type. Wraps in
    // ~292 million years, which is well past the lifetime of any AP.
    return (mbedtls_ms_time_t)(to_us_since_boot(get_absolute_time()) / 1000ULL);
}
