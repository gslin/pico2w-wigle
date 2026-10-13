// Wall clock from SNTP. Once synced, time() returns UTC.
#pragma once

#include <stdbool.h>

// Query NTP_SERVER; needs a network connection.
bool clock_sync(void);

// True once any sync succeeded.
bool clock_valid(void);
