# pico2w-wigle

A Wi-Fi wardriving firmware for the Raspberry Pi Pico 2 W. Scans for
nearby access points, tags each observation with a GPS fix, buffers
[WigleWifi-1.4 CSV](https://api.wigle.net/swagger#/File%20upload/upload)
rows in RAM, and — every 1000 new observations — briefly joins a
preconfigured Wi-Fi network to multipart-POST the batch to
`https://api.wigle.net/api/v2/file/upload` over HTTPS.

## Hardware

* **Raspberry Pi Pico 2 W** (RP2350 + CYW43439).
* **A UART GPS module** (NEO-6M / NEO-7M / NEO-M8N / ATGM336H — anything
  that emits NMEA at 9600 8N1 by default).
* USB cable that carries data (not a charge-only one).

### Wiring

The Pico only listens to the GPS, so the GPS RX pin is left
unconnected.

| GPS pin | Pico 2 W pin           | Notes                                                                         |
| ------- | ---------------------- | ----------------------------------------------------------------------------- |
| `VCC`   | `3V3 OUT` (pin 36)     | Most modern modules accept 3.3 V. If yours needs 5 V, use `VBUS` instead and add a level shifter on TX → GP5. |
| `GND`   | `GND` (pin 38)         |                                                                               |
| `TX`    | `GP5` (pin 7, UART1 RX)| NMEA stream goes from the GPS into the Pico here.                             |
| `RX`    | _(not connected)_      |                                                                               |

To use a different UART/pin pair, edit `GPS_UART`, `GPS_UART_TX_PIN`
and `GPS_UART_RX_PIN` in `src/gps.c`.

## Build prerequisites

On the build host (Linux or macOS) you need:

* A [`pico-sdk`](https://github.com/raspberrypi/pico-sdk) checkout with
  submodules initialised (in particular `lib/cyw43-driver`, `lib/lwip`,
  and `lib/mbedtls`).
* `arm-none-eabi-gcc` toolchain.
* `cmake` ≥ 3.13.
* `curl` (used by `make init` to verify the Wigle credentials).
* `openssl` (used by `make init` to capture and validate the
  api.wigle.net certificate chain that gets pinned into the firmware).
* GNU Make.

Either symlink the SDK to `$HOME/pico-sdk` or pass it on the command
line: `make build PICO_SDK_PATH=/your/path`.

On the Wigle side: log in at <https://wigle.net/account>, generate an
API token, and note both the **API Name** and **API Token** strings.
Wigle does not expose a public password→token endpoint; tokens are
issued through the website only.

## Workflow

```sh
make init      # Configure Wigle credentials + upload Wi-Fi
make build     # Compile build/pico2w-wigle.uf2
make install   # Copy the .uf2 onto a Pico 2 W in BOOTSEL mode
```

### `make init`

Runs `scripts/init.sh`, which:

1. Prompts for the Wigle API Name and API Token.
2. Sends an HTTP Basic Auth request to
   `https://api.wigle.net/api/v2/profile/user`. A 200 response means
   the credentials are valid; 401/403 aborts the process.
3. Prompts for the SSID and password of the Wi-Fi the Pico will
   briefly join during upload bursts (a phone hotspot is the easiest
   choice).
4. Locates **ISRG Root X1** (and Root X2 if available) in the build
   host's CA store — first in the per-cert layouts used by Debian /
   Ubuntu / Fedora / Homebrew, then by walking the consolidated
   `ca-certificates.crt` bundle if needed.
5. Connects to `api.wigle.net:443` with `openssl s_client -CAfile <those
   roots> -no-CAfile -no-CApath -verify_return_error`, i.e. trusting
   *only* the extracted ISRG roots. This catches the day Wigle ever
   moves off Let's Encrypt — `make init` aborts loudly rather than
   silently pin something that won't validate on-device.
6. Writes credentials as C macros to `src/secrets.h` (mode `0600`)
   and the ISRG PEM(s) as a `static const unsigned char[]` to
   `src/wigle_ca_pem.h`. Both files are gitignored.

```sh
$ make init
pico2w-wigle — initial configuration
...
Wigle API name : AIDxxxxxxxxxxxxxxxxx
Wigle API token: ********
Verifying credentials with https://api.wigle.net/api/v2/profile/user ...
OK — authenticated as your_wigle_username.
Upload Wi-Fi SSID    : MyPhoneHotspot
Upload Wi-Fi password: ********
Locating ISRG roots in the host CA store...
  - ISRG Root X1 from /etc/ssl/certs/ISRG_Root_X1.pem
  - ISRG Root X2 from /etc/ssl/certs/ISRG_Root_X2.pem
Verifying api.wigle.net currently chains to ISRG Root X1/X2 ...
OK — Wigle chains to ISRG.
Wrote src/secrets.h (mode 0600).
Wrote src/wigle_ca_pem.h.
```

### `make build`

Configures CMake under `build/` with `-DPICO_BOARD=pico2_w`, then
compiles, producing `build/pico2w-wigle.uf2`. The first run takes a
while (CYW43 driver, lwIP, mbedTLS), incremental rebuilds are quick.

Override knobs on the command line as needed:

```sh
make build PICO_SDK_PATH=/opt/pico-sdk
make build PICO_BOARD=pico2_w
```

### `make install`

Hold BOOTSEL while plugging the Pico 2 W into USB. The board appears
as a USB mass-storage device labelled `RP2350`. `make install` looks
for it in the standard mount points
(`/media/$USER/RP2350`, `/run/media/$USER/RP2350`, `/Volumes/RP2350`)
and copies the `.uf2` over. The board reboots into the new firmware
automatically.

If your distro mounts the volume somewhere else:

```sh
make install PICO_MOUNT=/path/to/RP2350
```

## Runtime behaviour

Open the USB serial console (115200 8N1) to see something like:

```
pico2w-wigle starting up
Waiting for GPS fix on UART1 (GP5)...
GPS fix: 25.033964,121.564468  alt=12.4m sats=8
Buffer full (1000 rows, 142318 bytes). Uploading...
upload: associating with "MyPhoneHotspot"...
upload: HTTP 200 ok
Upload OK; clearing buffer.
```

On-board LED:

* Two short blinks — boot complete.
* Steady on — scan in progress.
* Off — idle between scans.
* Three short blinks — upload succeeded.
* Five short blinks — upload failed.

Main-loop state machine:

1. Wait for a GPS fix. Without a fix we don't scan, so we never emit
   rows tagged with `0.000000,0.000000`.
2. Every `SCAN_INTERVAL_MS` (default 8 s) trigger `cyw43_wifi_scan`.
3. Each scan-result callback inserts a row into the RAM buffer through
   `csv_buffer_append_observation`. Within one scan the same BSSID is
   recorded only once; observations from later scans (with newer
   timestamps / GPS fixes) are kept.
4. When `csv_buffer_count()` reaches 1000, the firmware connects to
   the configured Wi-Fi, multipart-POSTs the batch to
   `api.wigle.net/api/v2/file/upload`, parses the HTTP status line,
   and disconnects.
5. On a 2xx response the buffer is cleared. Otherwise the rows are
   kept and the next attempt happens on the next threshold crossing.

## Tunable constants

| Location                                            | Meaning                                             |
| --------------------------------------------------- | --------------------------------------------------- |
| `src/csv_buffer.h` `CSV_BUFFER_UPLOAD_THRESHOLD`    | Rows that trigger an upload (default `1000`).       |
| `src/csv_buffer.c` `CSV_BUFFER_CAPACITY`            | RAM buffer size (default 192 KiB).                  |
| `src/main.c` `SCAN_INTERVAL_MS`                     | Time between scans (default `8000`).                |
| `src/gps.c` `GPS_UART`, `GPS_BAUD`                  | UART instance and baud (default `uart1` / 9600).    |
| `src/wigle_upload.c` `BOUNDARY`                     | multipart boundary string (no reason to change it). |

## Known limitations and security notes

* **TLS pins ISRG Root X1 (and X2 if present).** Embedding the full
  Mozilla bundle (~145 roots, ~400 KB once parsed into mbedTLS x509
  structures) would blow the RP2350's RAM budget; pinning the leaf
  chain forces a re-flash every Let's Encrypt rotation. Pinning the
  ISRG roots is the sweet spot: ~3 KB of flash, no rotation churn
  (Root X1 valid until 2035-06-04, X2 until 2040-09-17). `wigle_upload.c`
  passes them to `altcp_tls_create_config_client(WIGLE_CA_PEM, sizeof
  WIGLE_CA_PEM)`, which flips lwIP's altcp_tls layer into
  `MBEDTLS_SSL_VERIFY_REQUIRED`. If Wigle ever switches off Let's
  Encrypt, the on-device handshake will fail and `make init` will
  refuse to write a new header — at that point edit `scripts/init.sh`
  to pin whatever new root they're using.
* **The buffer lives in RAM only.** A power loss before the threshold
  is reached drops up to 999 unsaved observations. If you scan for
  long stretches without uploading, persist the buffer to flash
  (`hardware/flash.h` with a custom sector layout, or littlefs).
* **Upload Wi-Fi assumes WPA2-PSK.** `wigle_upload.c` hardcodes
  `CYW43_AUTH_WPA2_AES_PSK`. Adjust if you need WPA3-SAE or open
  networks.
* **Auth-mode capability strings are coarse.** The CYW43 scan callback
  exposes only a one-byte hint, so `wifi_scan.c` distinguishes only
  "open" vs "encrypted (assume WPA2)". Wigle accepts either string —
  the field is descriptive — but if you want better fidelity,
  rewrite `auth_mode_to_string`.
* **Scanning and uploading are sequential.** During an upload (Wi-Fi
  associate → DNS → TLS → POST → leave, typically 10–30 s) no scan
  runs. For continuous track quality, refactor to scan while connected
  to the upload AP.
* **Position is GPS-only.** Observations without a fix are dropped to
  avoid polluting Wigle with `0,0` rows.

## Troubleshooting

* `make build` complains about `PICO_SDK_PATH`: place `pico-sdk` at
  `$HOME/pico-sdk` or pass `PICO_SDK_PATH=/your/path`.
* `make build` fails inside `pico_sdk_init()`: the SDK submodules
  aren't initialised. Inside the SDK directory run
  `git submodule update --init --recursive`.
* `make install` cannot find the mount point: BOOTSEL must be held
  *while plugging in USB* until `RP2350` appears. Pressing the on-board
  RESET alone does not enter the bootloader.
* GPS never gets a fix: cold-start typically takes 30 s to a few
  minutes outdoors. To inspect raw NMEA, add a `printf` inside
  `handle_line` and watch the USB serial output.
* Uploads keep failing: re-run `make init` to confirm the token is
  still valid, then verify the upload Wi-Fi has a working internet
  egress (e.g. `ping api.wigle.net` from a laptop on the same SSID).

## File layout

```
.
├── GNUmakefile               # Entry point (make init / build / install)
├── CMakeLists.txt            # Pico SDK build configuration
├── pico_sdk_import.cmake     # Standard SDK import shim
├── scripts/
│   └── init.sh               # Interactive configuration, run by make init
└── src/
    ├── main.c                # Main loop / state machine
    ├── csv_buffer.[ch]       # In-RAM Wigle CSV buffer
    ├── gps.[ch]              # NMEA $GxGGA / $GxRMC parser
    ├── wifi_scan.[ch]        # Wraps cyw43_wifi_scan
    ├── wigle_upload.[ch]     # HTTPS multipart POST
    ├── lwipopts.h            # lwIP build options
    ├── mbedtls_config.h      # mbedTLS build options
    ├── secrets.h.example     # Template (make init writes the real one)
    ├── secrets.h             # *git-ignored*, generated by make init
    └── wigle_ca_pem.h        # *git-ignored*, generated by make init
                              #   (pinned api.wigle.net cert chain)
```

## Legal and ethical note

Wigle is a public, crowd-sourced database of Wi-Fi and cellular signals.
In most jurisdictions, passive Wi-Fi reception (which is all this
firmware does) is legal, but some countries — notably parts of the EU —
restrict logging and republishing neighbouring SSIDs/BSSIDs. Check
local rules before driving around with this. Do not attempt to connect
to networks you do not own, and do not log management-frame client MAC
addresses.
