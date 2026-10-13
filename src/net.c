#include "net.h"

#include <stdio.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"

void net_poll(uint32_t ms) {
    cyw43_arch_poll();
    watchdog_update();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(ms));
}

// DNS state is static because lwIP may call back after we gave up; the
// generation number makes such a late answer harmless.
static struct {
    uint32_t gen;
    bool done;
    bool ok;
    ip_addr_t addr;
} dns;

static void on_dns(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name;
    if ((uintptr_t)arg != dns.gen) return;
    if (addr) {
        dns.addr = *addr;
        dns.ok = true;
    }
    dns.done = true;
}

static int resolve(const char *host, ip_addr_t *addr) {
    dns.gen++;
    dns.done = dns.ok = false;
    err_t err = dns_gethostbyname(host, addr, on_dns, (void *)(uintptr_t)dns.gen);
    if (err == ERR_OK) return 0;
    if (err != ERR_INPROGRESS) return -1;
    absolute_time_t deadline = make_timeout_time_ms(NET_TIMEOUT_MS);
    while (!dns.done) {
        if (time_reached(deadline)) return -1;
        net_poll(10);
    }
    if (!dns.ok) return -1;
    *addr = dns.addr;
    return 0;
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)pcb;
    (void)err; // always ERR_OK; failures go to on_err
    ((net_conn_t *)arg)->connected = true;
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)pcb;
    (void)err;
    net_conn_t *c = arg;
    if (!p) {
        c->eof = true;
    } else if (c->rx) {
        pbuf_cat(c->rx, p);
    } else {
        c->rx = p;
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    net_conn_t *c = arg;
    printf("net: connection error %d\n", err);
    c->pcb = NULL; // already freed by lwIP
}

int net_connect(net_conn_t *c, const char *host, uint16_t port) {
    memset(c, 0, sizeof *c);
    ip_addr_t addr;
    if (resolve(host, &addr) != 0) {
        printf("net: cannot resolve %s\n", host);
        return -1;
    }
    c->pcb = tcp_new_ip_type(IP_GET_TYPE(&addr));
    if (!c->pcb) return -1;
    tcp_arg(c->pcb, c);
    tcp_recv(c->pcb, on_recv);
    tcp_err(c->pcb, on_err);
    if (tcp_connect(c->pcb, &addr, port, on_connected) != ERR_OK) {
        net_close(c);
        return -1;
    }
    absolute_time_t deadline = make_timeout_time_ms(NET_TIMEOUT_MS);
    while (!c->connected) {
        if (!c->pcb || time_reached(deadline)) {
            printf("net: cannot connect to %s:%u\n", host, port);
            net_close(c);
            return -1;
        }
        net_poll(10);
    }
    return 0;
}

int net_write(net_conn_t *c, const void *buf, size_t len) {
    const uint8_t *p = buf;
    size_t left = len;
    absolute_time_t deadline = make_timeout_time_ms(NET_TIMEOUT_MS);
    while (c->pcb) {
        size_t n = MIN(left, (size_t)tcp_sndbuf(c->pcb));
        if (n > 0 && tcp_write(c->pcb, p, (u16_t)n, TCP_WRITE_FLAG_COPY) == ERR_OK) {
            p += n;
            left -= n;
            deadline = make_timeout_time_ms(NET_TIMEOUT_MS);
        }
        tcp_output(c->pcb);
        if (left == 0) return (int)len;
        if (time_reached(deadline)) {
            printf("net: write timeout\n");
            break;
        }
        net_poll(10);
    }
    return -1;
}

int net_read(net_conn_t *c, void *buf, size_t len) {
    absolute_time_t deadline = make_timeout_time_ms(NET_TIMEOUT_MS);
    while (!c->rx) {
        if (c->eof) return 0;
        if (!c->pcb) return -1;
        if (time_reached(deadline)) {
            printf("net: read timeout\n");
            return -1;
        }
        net_poll(10);
    }
    u16_t n = pbuf_copy_partial(c->rx, buf, (u16_t)MIN(len, 0xffff), 0);
    c->rx = pbuf_free_header(c->rx, n);
    if (c->pcb) tcp_recved(c->pcb, n);
    return n;
}

static int read_full(net_conn_t *c, void *buf, size_t len) {
    uint8_t *p = buf;
    while (len > 0) {
        int n = net_read(c, p, len);
        if (n <= 0) return -1;
        p += n;
        len -= n;
    }
    return 0;
}

void net_close(net_conn_t *c) {
    if (c->pcb) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
        c->pcb = NULL;
    }
    if (c->rx) {
        pbuf_free(c->rx);
        c->rx = NULL;
    }
}

// SOCKS5 CONNECT (RFC 1928) with optional username/password (RFC 1929).
// The host name is sent as is, so the proxy does the DNS lookup.
int net_socks5(net_conn_t *c, const char *host, uint16_t port,
               const char *user, const char *pass) {
    static const char *const replies[] = {
        "succeeded", "general failure", "not allowed by ruleset",
        "network unreachable", "host unreachable", "connection refused",
        "TTL expired", "command not supported", "address type not supported",
    };
    size_t hlen = strlen(host), ulen = strlen(user), plen = strlen(pass);
    uint8_t buf[3 + 255 + 255];
    size_t n = 0;

    if (hlen > 255 || ulen > 255 || plen > 255) {
        printf("socks5: host, user or password longer than 255 bytes\n");
        return -1;
    }

    // Offer "no authentication", plus username/password when configured.
    buf[n++] = 5;
    buf[n++] = ulen ? 2 : 1;
    buf[n++] = 0x00;
    if (ulen) buf[n++] = 0x02;
    if (net_write(c, buf, n) < 0 || read_full(c, buf, 2) < 0) return -1;
    if (buf[0] != 5 || !(buf[1] == 0x00 || (ulen && buf[1] == 0x02))) {
        printf("socks5: no acceptable authentication method\n");
        return -1;
    }
    if (buf[1] == 0x02) {
        n = 0;
        buf[n++] = 1;
        buf[n++] = (uint8_t)ulen;
        memcpy(buf + n, user, ulen);
        n += ulen;
        buf[n++] = (uint8_t)plen;
        memcpy(buf + n, pass, plen);
        n += plen;
        if (net_write(c, buf, n) < 0 || read_full(c, buf, 2) < 0) return -1;
        if (buf[1] != 0) {
            printf("socks5: authentication failed\n");
            return -1;
        }
    }

    n = 0;
    buf[n++] = 5;    // VER
    buf[n++] = 1;    // CMD: CONNECT
    buf[n++] = 0;    // RSV
    buf[n++] = 3;    // ATYP: domain name
    buf[n++] = (uint8_t)hlen;
    memcpy(buf + n, host, hlen);
    n += hlen;
    buf[n++] = port >> 8;
    buf[n++] = port & 0xff;
    if (net_write(c, buf, n) < 0 || read_full(c, buf, 4) < 0) return -1;
    if (buf[0] != 5 || buf[1] != 0) {
        printf("socks5: %s\n", buf[1] < count_of(replies) ? replies[buf[1]] : "unknown error");
        return -1;
    }

    // Skip BND.ADDR and BND.PORT.
    switch (buf[3]) {
    case 1: n = 4; break;
    case 4: n = 16; break;
    case 3:
        if (read_full(c, buf, 1) < 0) return -1;
        n = buf[0];
        break;
    default: return -1;
    }
    return read_full(c, buf, n + 2);
}
