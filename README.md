# pico2w-wigle

A [WiGLE](https://wigle.net/) collector for the Raspberry Pi Pico 2 W. It
scans Wi-Fi from a fixed location, keeps one record per BSSID, and uploads
the records to WiGLE as a WigleWifi-1.6 CSV file over HTTPS every 6 hours.

This repository was generated with [Claude Code](https://claude.com/claude-code).

## Requirements

* Raspberry Pi Pico 2 W and a USB data cable.
* Build host: GNU Make, git, CMake, `arm-none-eabi-gcc`, a host C compiler,
  and curl. `picotool` is optional (see `make install` below).
* A WiGLE account.
* A 2.4 GHz Wi-Fi network (WPA2-PSK or open) for the uploads.

The first build clones pico-sdk 2.3.1 into `.deps/pico-sdk`. Set
`PICO_SDK_PATH` to use an existing checkout instead.

## Usage

```sh
make install
```

On the first run this asks for the settings and writes them to
`GNUmakefile.local`:

* Proxy for the build host (curl syntax, optional).
* Wi-Fi SSID and password used for uploads.
* Latitude, longitude, altitude, and accuracy of the device.
* SOCKS5 proxy for the Pico (optional).
* WiGLE username and password. They are used once, to create an API token
  through `https://api.wigle.net/api/v2/activate`; only the token is stored.

It then downloads the Let's Encrypt root certificates, builds the firmware,
and copies `build/pico2w-wigle.uf2` to the Pico. Hold BOOTSEL while plugging
in the board so that the `RP2350` drive appears. If the drive is not found,
`picotool load` is used when available, which can also reboot a running
pico2w-wigle into BOOTSEL by itself.

Other targets:

| Target           | Description                                      |
| ---------------- | ------------------------------------------------ |
| `make config`    | Ask for the settings again                       |
| `make`           | Build only                                       |
| `make clean`     | Remove `build/`                                  |
| `make distclean` | Remove `build/` and `.deps/`                     |

## Settings

`GNUmakefile.local` is a makefile fragment (git-ignored, mode 0600) and can
be edited by hand; write `$` as `$$` and `#` as `\#`. Any setting can also
be given on the command line, e.g. `make install UPLOAD_INTERVAL=600`.

| Variable          | Default        | Description                                  |
| ----------------- | -------------- | -------------------------------------------- |
| `WIFI_SSID`       |                | Wi-Fi network for uploads                    |
| `WIFI_PASSWORD`   |                | Empty for an open network                    |
| `WIFI_COUNTRY`    | `XX`           | Two-letter country code; `XX` is worldwide   |
| `GPS_LAT`         |                | Latitude, decimal degrees                    |
| `GPS_LON`         |                | Longitude, decimal degrees                   |
| `GPS_ALT`         | `0`            | Altitude in meters (integer)                 |
| `GPS_ACC`         | `10`           | Accuracy in meters                           |
| `WIGLE_API_NAME`  |                | WiGLE API name (`AID...`)                    |
| `WIGLE_API_TOKEN` |                | WiGLE API token                              |
| `LOCAL_PROXY`     |                | Proxy for the build host, curl syntax        |
| `PICO_PROXY`      |                | `socks5://[user:pass@]host:port`             |
| `NTP_SERVER`      | `pool.ntp.org` | NTP server used by the Pico                  |
| `SCAN_INTERVAL`   | `10`           | Seconds between scans                        |
| `UPLOAD_INTERVAL` | `21600`        | Seconds between uploads                      |
| `PICO_SDK_PATH`   | `.deps/pico-sdk` | pico-sdk checkout                          |
| `PICO_MOUNT`      | auto           | Mount point of the `RP2350` drive            |

`make config` requests a token of type `API`. Set `WIGLE_TOKEN_TYPE` in the
environment (`API`, `COMMAPI`, or `ANDROID`) to request another type.

## Proxies

* `LOCAL_PROXY` is passed to curl and git as `ALL_PROXY`. It covers the WiGLE
  login in `make config`, the certificate download, and the pico-sdk and
  picotool downloads. Use `socks5h://` to resolve host names through the
  proxy, e.g. `socks5h://127.0.0.1:1080`.
* `PICO_PROXY` is used by the firmware for the HTTPS upload. The Pico opens
  a SOCKS5 tunnel to `api.wigle.net:443` (username/password authentication
  is supported) and runs TLS through it. The host name is always resolved
  by the proxy, for both `socks5://` and `socks5h://`. The proxy must be
  reachable from the upload Wi-Fi, so `127.0.0.1` does not work here.

NTP (UDP port 123) does not go through the proxy.

## Runtime behavior

* Wi-Fi is scanned every `SCAN_INTERVAL` seconds. Each BSSID is stored once,
  with its first-seen time and strongest RSSI, up to 2048 BSSIDs.
* The first upload runs 60 seconds after boot, so that a new installation
  can be checked quickly; after that it runs every `UPLOAD_INTERVAL`
  seconds. A failed upload is retried after 30 minutes.
* For an upload the Pico joins the Wi-Fi network, sets its clock with NTP,
  POSTs the CSV to `https://api.wigle.net/api/v2/file/upload`, and leaves
  the network. The table is cleared after WiGLE accepts the file.
* The server certificate is verified against the Let's Encrypt roots
  (ISRG Root X1, X2, YR, and YE) embedded at build time, including the
  validity period.
* The on-board LED blinks when new BSSIDs are found and stays on during
  uploads.

## Debugging

The firmware logs to the USB serial console, e.g. `tio /dev/ttyACM0` or
`screen /dev/ttyACM0` (`/dev/cu.usbmodem*` on macOS). At boot it waits up to
3 seconds for a terminal, so keep one open (tio reconnects by itself) to see
the boot messages. Each line starts with the seconds since boot; an example:

```
[     1.020] pico2w-wigle 1.0, built Oct 13 2026 22:10:00, pico-sdk 2.3.1
[     1.020] reset: power-on or reset
[     1.390] wifi: MAC 2c:cf:67:00:00:01, country XX
[     1.390] config: upload network "MyHotspot" (WPA2)
[     1.390] config: location 25.0335,121.5645, altitude 0 m, accuracy 10 m
[     1.390] config: scan every 10 s, upload every 21600 s, first upload after 60 s
[     1.390] config: WiGLE API name AID0123456789, NTP server pool.ntp.org
[     1.390] config: no proxy
[     3.050] scan: new 00:11:22:33:44:55 ch  6  -48 dBm [WPA2][ESS]      "MyHotspot"
[     3.920] scan: 14 results in 2530 ms, 12 new, 12 in table
[    61.400] status: 6 scans, 31 networks in table, next upload in 0 s, last upload none yet, heap 1024 of 367964 bytes in use
[    61.410] wifi: joining "MyHotspot"
[    61.410] wifi: joining (0 ms)
[    63.200] wifi: joined, waiting for DHCP (1790 ms)
[    64.010] wifi: up (2600 ms)
[    64.010] wifi: IP 192.168.1.23, netmask 255.255.255.0, gateway 192.168.1.1, DNS 192.168.1.1, RSSI -48 dBm
[    64.010] ntp: asking pool.ntp.org
[    64.300] ntp: 2026-10-13 14:11:05 UTC from 203.0.113.5 (290 ms)
[    64.300] upload: to https://api.wigle.net/api/v2/file/upload as AID0123456789
[    64.360] net: api.wigle.net is 203.0.113.80 (60 ms)
[    64.560] net: connected to api.wigle.net:443 (200 ms)
[    65.900] tls: chain 2: C=US, O=ISRG, CN=Root YR
[    65.900] tls: chain 1: C=US, O=Let's Encrypt, CN=YR2
[    65.900] tls: chain 0: CN=wigle.net
[    66.700] tls: TLSv1.2 TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256 (2140 ms, heap 48000 of 367964 bytes in use)
[    66.710] upload: sending 31 networks, 3900 bytes
[    67.900] upload: HTTP 200, 512 bytes: {"success":true,...}
[    67.910] net: closed, 5800 bytes sent, 6100 received
[    67.910] upload: accepted in 3610 ms
[    67.950] upload: next one in 21600 s
```

The prefix tells which part is talking: `wifi`, `scan`, `ntp`, `net` (DNS
and TCP), `socks5`, `tls`, `upload`, and `status` (once a minute). Messages
that point at common problems:

| Message                                         | Likely cause                                         |
| ----------------------------------------------- | ---------------------------------------------------- |
| `wifi: wrong password`, `network not found`     | `WIFI_SSID` / `WIFI_PASSWORD`, or a 5 GHz-only AP    |
| `ntp: no reply ...`                             | UDP port 123 blocked; try another `NTP_SERVER`       |
| `net: cannot resolve ...`                       | DNS from DHCP does not work                          |
| `socks5: ...`                                   | `PICO_PROXY` address or credentials                  |
| `tls: certificate: ...`                         | Wrong clock, or WiGLE moved off Let's Encrypt        |
| `upload: HTTP 401`                              | API token rejected; run `make config`                |
| `reset: watchdog timeout`                       | The firmware hung; the lines before the reset matter |

To try the upload without waiting, the first one already runs 60 seconds
after boot; `make install UPLOAD_INTERVAL=600` shortens the interval too.

## Limitations

* The records are kept in RAM only and are lost on power loss or reset.
* The CYW43439 radio only sees 2.4 GHz networks.
* The capability column only distinguishes open, WEP, WPA, and WPA2/WPA3,
  because the Wi-Fi driver reports nothing finer.
* The Wi-Fi password, WiGLE token, and proxy credentials are stored in the
  firmware image, and in `build/`. Anyone with the board can read them.
