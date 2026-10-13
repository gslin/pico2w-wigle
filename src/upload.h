// Upload the scan table to WiGLE as a WigleWifi-1.6 CSV file over HTTPS.
#pragma once

#include <stdbool.h>

#define APP_VERSION "1.0"

// Needs a network connection and a valid clock. True when WiGLE accepted
// the file.
bool upload_run(void);
