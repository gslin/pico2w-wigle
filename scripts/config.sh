#!/bin/sh
# Interactive setup for pico2w-wigle: asks for the device settings, creates
# a WiGLE API token from the account username/password, and writes
# GNUmakefile.local. Current values come from the environment (exported by
# GNUmakefile) and are offered as defaults.
set -eu

out=${1:-GNUmakefile.local}
vars='LOCAL_PROXY WIFI_SSID WIFI_PASSWORD GPS_LAT GPS_LON GPS_ALT GPS_ACC PICO_PROXY WIGLE_API_NAME WIGLE_API_TOKEN'
for v in $vars; do
    eval ": \"\${$v:=}\""
done

die() {
    echo "error: $*" >&2
    exit 1
}

[ -t 0 ] || die "device settings are missing; run 'make config' in a terminal."

umask 077
tmp=$(mktemp -d)
trap 'stty echo 2>/dev/null || true; rm -rf "$tmp"' EXIT
trap 'exit 1' INT TERM

# ask VAR PROMPT: Enter keeps the current value, "none" clears it.
ask() {
    eval "cur=\$$1"
    if [ -n "$cur" ]; then
        printf '%s [%s]: ' "$2" "$cur"
    else
        printf '%s: ' "$2"
    fi
    IFS= read -r ans || exit 1
    case $ans in
        '') ans=$cur ;;
        none) ans= ;;
    esac
    eval "$1=\$ans"
}

# ask_secret VAR PROMPT: read without echo.
ask_secret() {
    printf '%s: ' "$2"
    stty -echo
    IFS= read -r ans || ans=
    stty echo
    echo
    eval "$1=\$ans"
}

# ask_num VAR PROMPT MIN MAX [int]
ask_num() {
    re='^-?[0-9]+(\.[0-9]+)?$'
    [ "${5:-}" = int ] && re='^-?[0-9]+$'
    while :; do
        ask "$1" "$2"
        eval "v=\$$1"
        if printf '%s\n' "$v" | grep -Eq "$re" &&
            awk -v v="$v" -v lo="$3" -v hi="$4" 'BEGIN { exit !(v + 0 >= lo && v + 0 <= hi) }'; then
            return
        fi
        echo "Please enter a value between $3 and $4${5:+ (integer)}." >&2
        eval "$1="
    done
}

wigle_curl() {
    if [ -n "$LOCAL_PROXY" ]; then
        curl --proxy "$LOCAL_PROXY" "$@"
    else
        curl "$@"
    fi
}

# json_str KEY FILE: value of the first "KEY":"value" pair.
json_str() {
    sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$2" | head -n 1
}

# Make syntax for the right-hand side of `VAR := value`.
mk_quote() {
    printf '%s' "$1" | sed -e 's/\$/$$/g' -e 's/#/\\#/g'
}

cat <<'EOF'
pico2w-wigle setup
Press Enter to keep the value in [brackets], or type "none" to clear it.

EOF

ask LOCAL_PROXY 'Proxy for this machine, curl syntax (e.g. socks5h://127.0.0.1:1080; empty = direct)'

echo
echo 'Wi-Fi network the Pico joins for uploads (2.4 GHz, WPA2-PSK or open):'
while :; do
    ask WIFI_SSID '  SSID'
    [ -z "$WIFI_SSID" ] || break
done
if [ -n "$WIFI_PASSWORD" ]; then
    ask_secret pw '  Password (empty = keep current)'
    [ -z "$pw" ] || WIFI_PASSWORD=$pw
else
    ask_secret WIFI_PASSWORD '  Password (empty = open network)'
fi

echo
echo 'Fixed location of the device:'
ask_num GPS_LAT '  Latitude in decimal degrees' -90 90
ask_num GPS_LON '  Longitude in decimal degrees' -180 180
ask_num GPS_ALT '  Altitude in meters' -1000 10000 int
ask_num GPS_ACC '  Accuracy in meters' 0 100000

echo
while :; do
    ask PICO_PROXY 'SOCKS5 proxy for the Pico (socks5://[user:pass@]host:port; empty = direct)'
    case $PICO_PROXY in
        '' | socks5://?*:* | socks5h://?*:*) break ;;
    esac
    echo 'Please use the form socks5://host:port.' >&2
    PICO_PROXY=
done

echo
echo 'WiGLE account (the password is only used here, to create an API token):'
if [ -n "$WIGLE_API_TOKEN" ]; then
    printf '  Username (empty = keep the token of %s): ' "$WIGLE_API_NAME"
else
    printf '  Username: '
fi
IFS= read -r user || exit 1
if [ -n "$user" ] || [ -z "$WIGLE_API_TOKEN" ]; then
    [ -n "$user" ] || die 'a WiGLE username is required.'
    ask_secret pass '  Password'
    printf '%s' "$pass" > "$tmp/pass"
    echo 'Creating an API token on api.wigle.net ...'
    code=$(wigle_curl -sS --max-time 30 -o "$tmp/resp" -w '%{http_code}' \
        --data-urlencode "credential_0=$user" \
        --data-urlencode "credential_1@$tmp/pass" \
        --data-urlencode "type=${WIGLE_TOKEN_TYPE:-API}" \
        https://api.wigle.net/api/v2/activate) || die 'cannot reach api.wigle.net; is a local proxy needed?'
    name=$(json_str authname "$tmp/resp")
    token=$(json_str token "$tmp/resp")
    if [ "$code" != 200 ] || [ -z "$name" ] || [ -z "$token" ]; then
        die "WiGLE login failed (HTTP $code); check the username and password."
    fi
    WIGLE_API_NAME=$name
    WIGLE_API_TOKEN=$token
fi

echo 'Checking the API token ...'
printf 'user = "%s:%s"\n' "$WIGLE_API_NAME" "$WIGLE_API_TOKEN" > "$tmp/auth"
code=$(wigle_curl -sS --max-time 30 -K "$tmp/auth" -o /dev/null -w '%{http_code}' \
    https://api.wigle.net/api/v2/profile/user) || die 'cannot reach api.wigle.net; is a local proxy needed?'
[ "$code" = 200 ] || die "WiGLE rejected the API token (HTTP $code)."
echo "OK, API name is $WIGLE_API_NAME."

{
    echo '# Written by `make config`. Write $ as $$ and # as \#.'
    for v in $vars; do
        eval "val=\$$v"
        printf '%s := %s\n' "$v" "$(mk_quote "$val")"
    done
    # Keep settings that were added by hand.
    if [ -f "$out" ]; then
        grep -v -E "^(# Written by|($(echo $vars | tr ' ' '|'))[[:space:]]*[:?]?=)" "$out" || true
    fi
} > "$tmp/local"
mv "$tmp/local" "$out"
echo "Wrote $out."
