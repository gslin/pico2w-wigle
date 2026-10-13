// Blocking TCP client on the lwIP raw API, with an optional SOCKS5 hop.
// Each call gives up after NET_TIMEOUT_MS without progress.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_TIMEOUT_MS 30000

typedef struct {
    struct tcp_pcb *pcb; // NULL once closed or failed
    struct pbuf *rx;     // received, not yet read
    bool connected;
    bool eof;
} net_conn_t;

// Service the Wi-Fi chip and lwIP, feed the watchdog, then wait up to ms
// for more work.
void net_poll(uint32_t ms);

// All return 0 (or the byte count) on success and -1 on error or timeout.
int net_connect(net_conn_t *c, const char *host, uint16_t port);
int net_write(net_conn_t *c, const void *buf, size_t len);
int net_read(net_conn_t *c, void *buf, size_t len); // 0 at end of stream
void net_close(net_conn_t *c);

// Ask the SOCKS5 proxy that c is connected to for a tunnel to host:port.
// An empty user means no authentication.
int net_socks5(net_conn_t *c, const char *host, uint16_t port,
               const char *user, const char *pass);
