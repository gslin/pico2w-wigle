#!/usr/bin/env bash
# Interactive configuration for pico2w-wigle.
#
# Collects:
#   - Wigle API name + token (verified against the Wigle API)
#   - Wi-Fi SSID + password used briefly during upload bursts
# And generates:
#   - src/secrets.h       (mode 0600) with the credentials
#   - src/wigle_ca_pem.h               with ISRG Root X1 (+ X2 if found)
#                                      extracted from the build host's CA
#                                      store, after verifying that
#                                      api.wigle.net currently chains to
#                                      those roots.
# Both generated files are gitignored.

set -eu

cd "$(dirname "$0")/.."

SECRETS_HEADER="src/secrets.h"
CA_HEADER="src/wigle_ca_pem.h"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: '$1' is required but not installed." >&2
        exit 1
    fi
}

require_cmd curl
require_cmd openssl
require_cmd awk
require_cmd sed

escape_c() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

read_secret() {
    local prompt="$1"
    if [ -t 0 ]; then
        printf '%s' "$prompt" >&2
        stty -echo
        IFS= read -r REPLY
        stty echo
        printf '\n' >&2
    else
        IFS= read -r REPLY
    fi
}

cat <<'EOF'
pico2w-wigle — initial configuration
====================================

This script will:
  1. Ask for your Wigle API credentials and verify them against
     https://api.wigle.net.
  2. Ask for the SSID + password of a Wi-Fi network the Pico can
     briefly join when uploading batches (a phone hotspot is fine).
  3. Locate ISRG Root X1 (and X2, if present) in the host CA store,
     verify that api.wigle.net currently chains to them, and pin the
     extracted PEM(s) into the firmware. ISRG Root X1 is valid until
     2035-06-04, X2 until 2040-09-17 — so this normally needs to be
     refreshed only if Wigle ever moves off Let's Encrypt.
  4. Write everything to src/secrets.h (chmod 0600, gitignored) and
     src/wigle_ca_pem.h (gitignored).

Wigle does NOT expose a password-to-token API; tokens are issued via
the website. Visit:

    https://wigle.net/account

and copy the values shown under "API Name" and "API Token" (you may
need to click "Show my Token" first).

EOF

# ---- step 1: credentials -------------------------------------------------

printf 'Wigle API name : ' >&2
IFS= read -r WIGLE_API_NAME
read_secret 'Wigle API token: '
WIGLE_API_TOKEN="$REPLY"

if [ -z "$WIGLE_API_NAME" ] || [ -z "$WIGLE_API_TOKEN" ]; then
    echo 'ERROR: API name and token must both be non-empty.' >&2
    exit 1
fi

TMP_BODY="$TMP_DIR/wigle-resp.json"

echo
echo 'Verifying credentials with https://api.wigle.net/api/v2/profile/user ...'
HTTP_CODE=$(curl -sS \
    -o "$TMP_BODY" \
    -w '%{http_code}' \
    --max-time 20 \
    -u "${WIGLE_API_NAME}:${WIGLE_API_TOKEN}" \
    'https://api.wigle.net/api/v2/profile/user' \
    || echo 'curl-failed')

case "$HTTP_CODE" in
    200)
        IDENT=$(grep -o '"user"[[:space:]]*:[[:space:]]*"[^"]*"' "$TMP_BODY" \
            | head -n1 | sed 's/.*"\([^"]*\)"$/\1/')
        echo "OK — authenticated as ${IDENT:-(unknown)}."
        ;;
    401|403)
        echo "ERROR: Wigle rejected the credentials (HTTP $HTTP_CODE)." >&2
        echo "       Double-check the values copied from wigle.net/account." >&2
        exit 1
        ;;
    curl-failed)
        echo 'ERROR: curl could not reach api.wigle.net.' >&2
        echo '       Check your network connection and try again.' >&2
        exit 1
        ;;
    *)
        echo "ERROR: unexpected response from Wigle (HTTP $HTTP_CODE)." >&2
        echo '       Response body:' >&2
        sed 's/^/  /' "$TMP_BODY" >&2 || true
        exit 1
        ;;
esac

# ---- step 2: upload Wi-Fi ------------------------------------------------

echo
printf 'Upload Wi-Fi SSID    : ' >&2
IFS= read -r UPLOAD_SSID
read_secret 'Upload Wi-Fi password: '
UPLOAD_PASSWORD="$REPLY"

if [ -z "$UPLOAD_SSID" ]; then
    echo 'ERROR: SSID must be non-empty.' >&2
    exit 1
fi

# ---- step 3: extract ISRG Root X1 (+ X2) from host CA store -------------

# Most distros ship ISRG Root X1/X2 as either a per-cert file or as
# part of a consolidated bundle. Try the well-known per-cert layouts
# first because they're zero-cost; fall back to scanning a bundle.
extract_isrg_roots() {
    local out="$1"
    : > "$out"

    local x1_paths=(
        /etc/ssl/certs/ISRG_Root_X1.pem
        /usr/share/ca-certificates/mozilla/ISRG_Root_X1.crt
        /etc/pki/ca-trust/source/anchors/ISRG_Root_X1.pem
        /opt/homebrew/etc/ca-certificates/cert.pem
    )
    local x2_paths=(
        /etc/ssl/certs/ISRG_Root_X2.pem
        /usr/share/ca-certificates/mozilla/ISRG_Root_X2.crt
        /etc/pki/ca-trust/source/anchors/ISRG_Root_X2.pem
    )

    local x1="" x2="" p
    for p in "${x1_paths[@]}"; do
        if [ -f "$p" ] && grep -q 'BEGIN CERTIFICATE' "$p"; then
            x1="$p"; break
        fi
    done
    for p in "${x2_paths[@]}"; do
        if [ -f "$p" ] && grep -q 'BEGIN CERTIFICATE' "$p"; then
            x2="$p"; break
        fi
    done

    if [ -n "$x1" ]; then
        awk '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/' \
            "$x1" >> "$out"
        echo "  - ISRG Root X1 from $x1"
        if [ -n "$x2" ]; then
            awk '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/' \
                "$x2" >> "$out"
            echo "  - ISRG Root X2 from $x2"
        else
            echo "  - ISRG Root X2 not found at standard paths (X1 alone is enough)."
        fi
        return 0
    fi

    # Fall back: walk a consolidated PEM bundle, split it, and pick out
    # the certs whose subject contains "ISRG Root X1" or "X2".
    local bundle=""
    local b
    for b in /etc/ssl/certs/ca-certificates.crt \
             /etc/pki/tls/certs/ca-bundle.crt \
             /etc/ssl/cert.pem
    do
        if [ -f "$b" ]; then bundle="$b"; break; fi
    done

    if [ -z "$bundle" ]; then
        echo "ERROR: no system CA bundle found." >&2
        return 1
    fi
    echo "  scanning $bundle ..."

    local sub_dir="$TMP_DIR/bundle-split"
    mkdir -p "$sub_dir"
    awk -v dir="$sub_dir" '
        /-----BEGIN CERTIFICATE-----/ { i++; f = sprintf("%s/c-%04d.pem", dir, i) }
        f                              { print > f }
        /-----END CERTIFICATE-----/    { close(f); f = "" }
    ' "$bundle"

    local found_x1=0 found_x2=0 f subj
    for f in "$sub_dir"/c-*.pem; do
        [ -f "$f" ] || continue
        subj=$(openssl x509 -in "$f" -noout -subject 2>/dev/null) || continue
        case "$subj" in
            *"ISRG Root X1"*)
                cat "$f" >> "$out"
                found_x1=1
                echo "  - ISRG Root X1 from $bundle"
                ;;
            *"ISRG Root X2"*)
                cat "$f" >> "$out"
                found_x2=1
                echo "  - ISRG Root X2 from $bundle"
                ;;
        esac
    done

    if [ "$found_x1" = 0 ]; then
        echo "ERROR: ISRG Root X1 not found in $bundle." >&2
        return 1
    fi
    if [ "$found_x2" = 0 ]; then
        echo "  - ISRG Root X2 not present in $bundle (X1 alone is enough)."
    fi
    return 0
}

echo
echo 'Locating ISRG roots in the host CA store...'
ISRG_PEM="$TMP_DIR/isrg.pem"
if ! extract_isrg_roots "$ISRG_PEM"; then
    echo
    echo '       Update your CA bundle (e.g. `apt install --reinstall ca-certificates`,' >&2
    echo '       `dnf reinstall ca-certificates`, or `brew install ca-certificates`)' >&2
    echo '       and re-run `make init`.' >&2
    exit 1
fi

# ---- step 4: verify Wigle currently chains to ISRG ----------------------

echo
echo 'Verifying api.wigle.net currently chains to ISRG Root X1/X2 ...'

# -no-CAfile and -no-CApath disable the system trust store, so the
# only acceptable trust anchors are the ones we just extracted. This
# is what catches the case where Wigle quietly switches CAs.
if ! openssl s_client \
        -CAfile "$ISRG_PEM" \
        -no-CAfile -no-CApath \
        -verify_return_error \
        -servername api.wigle.net \
        -connect api.wigle.net:443 \
        </dev/null >"$TMP_DIR/verify.log" 2>&1
then
    echo 'ERROR: api.wigle.net does NOT chain to ISRG Root X1/X2.' >&2
    echo '       Wigle may have switched off Let'\''s Encrypt. You can either' >&2
    echo '       wait for them to switch back, or modify scripts/init.sh to' >&2
    echo '       pin the new root they use. Last 20 lines of openssl output:' >&2
    tail -n 20 "$TMP_DIR/verify.log" | sed 's/^/  /' >&2
    exit 1
fi
echo 'OK — Wigle chains to ISRG.'

# ---- step 5: write headers atomically -----------------------------------

umask 077

cat > "$SECRETS_HEADER" <<EOF
// Auto-generated by \`make init\`. Do not edit by hand and do not commit.
#pragma once

#define WIGLE_API_NAME       "$(escape_c "$WIGLE_API_NAME")"
#define WIGLE_API_TOKEN      "$(escape_c "$WIGLE_API_TOKEN")"
#define UPLOAD_WIFI_SSID     "$(escape_c "$UPLOAD_SSID")"
#define UPLOAD_WIFI_PASSWORD "$(escape_c "$UPLOAD_PASSWORD")"
EOF
chmod 0600 "$SECRETS_HEADER"

GENERATED_AT="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

{
    echo "// Auto-generated by \`make init\` on ${GENERATED_AT}."
    echo "// Pinned trust anchors:"
    echo "//   - ISRG Root X1 — RSA-4096, valid until 2035-06-04"
    echo "//   - ISRG Root X2 — ECDSA P-384, valid until 2040-09-17 (if found)"
    echo "// Verified against api.wigle.net at init time."
    echo "// Re-run \`make init\` only if Wigle ever moves off Let's Encrypt."
    echo "#pragma once"
    echo ""
    echo "static const unsigned char WIGLE_CA_PEM[] ="
    # Each PEM line becomes one C string literal "...\n". PEM bodies
    # are base64 + ASCII headers — no backslashes or quotes — so the
    # escaping here is paranoia.
    sed 's/\\/\\\\/g; s/"/\\"/g; s/^/    "/; s/$/\\n"/' "$ISRG_PEM"
    echo "    ;"
} > "$CA_HEADER"

echo
echo "Wrote ${SECRETS_HEADER} (mode 0600)."
echo "Wrote ${CA_HEADER}."
echo "Next: run \`make build\` and then \`make install\`."
