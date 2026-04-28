// HTTPS multipart upload to wigle.net.
//
// Joins the configured Wi-Fi (UPLOAD_WIFI_SSID/PASSWORD), POSTs the
// CSV body to https://api.wigle.net/api/v2/file/upload, then leaves
// the network so scanning can resume on a clean state.

#pragma once

#include <stdbool.h>
#include <stddef.h>

// Returns 0 on success (HTTP 2xx), negative on any failure (Wi-Fi
// association, DNS, TLS handshake, HTTP non-2xx, transport error).
int wigle_upload_run(const char *csv, size_t csv_len);
