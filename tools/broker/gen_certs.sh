#!/usr/bin/env bash
# gen_certs.sh — bench PKI + credentials for the local Mosquitto test broker.
#
# Creates (in tools/broker/out/, gitignored):
#   ca.key / ca.crt          private CA (EC P-256), the only CA the device trusts
#   server.key / server.crt  broker cert, SAN = IP:<ip>, DNS:<ip>, IP:127.0.0.1, DNS:localhost
#                            (DNS:<ip> because the ESP8266's mbedTLS 2.x matches the
#                            host string against dNSName SANs only)
#   passwd                   Mosquitto password file (device + server users)
#   creds.env                plaintext test credentials for the helper scripts
# and keeps the MQTT credentials in app/inc/conf_secrets.h (gitignored) in
# sync, creating it from conf_secrets.example.h on first run. The Wi-Fi lines
# in that file are yours and are never touched. The CA (out/ca.crt) is not
# compiled into the MCU: flash it into the ESP8266 (tools/esp8266/README.md).
#
# Usage: tools/broker/gen_certs.sh [broker_ip]      (default 192.168.0.100)
#        FORCE=1 tools/broker/gen_certs.sh          regenerate everything
#
# This is a bench CA. A production broker should use certificates from the
# operator's PKI; the ESP8266 only needs the issuing CA in its mqtt_ca partition.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"
BROKER_IP="${1:-192.168.0.100}"
DEVICE_ID="$(sed -n 's/^#define NPS_DEVICE_ID[[:space:]]*"\(.*\)"/\1/p' "$ROOT/app/inc/conf_app.h")"
IMAGE="eclipse-mosquitto:2"

mkdir -p "$OUT"
cd "$OUT"

if [[ "${FORCE:-0}" == "1" ]]; then
    rm -f ca.* server.* passwd creds.env
fi

if [[ ! -f ca.crt ]]; then
    echo "### CA"
    openssl ecparam -name prime256v1 -genkey -noout -out ca.key
    openssl req -x509 -new -key ca.key -sha256 -days 3650 \
        -subj "/CN=NPS Bench CA" -out ca.crt
fi

# A cert from an older run without the DNS:<ip> SAN fails on the ESP8266.
if [[ -f server.crt ]] && ! openssl x509 -in server.crt -noout -ext subjectAltName \
        | grep -q "DNS:$BROKER_IP"; then
    echo "### broker cert lacks DNS:$BROKER_IP SAN, reissuing"
    rm -f server.crt server.key
fi

if [[ ! -f server.crt ]]; then
    echo "### broker certificate for $BROKER_IP"
    openssl ecparam -name prime256v1 -genkey -noout -out server.key
    openssl req -new -key server.key -subj "/CN=$BROKER_IP" -out server.csr
    printf 'subjectAltName=IP:%s,DNS:%s,IP:127.0.0.1,DNS:localhost\nextendedKeyUsage=serverAuth\n' \
        "$BROKER_IP" "$BROKER_IP" > server.ext
    openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
        -days 825 -sha256 -extfile server.ext -out server.crt
    rm -f server.csr server.ext
fi

if [[ ! -f creds.env ]]; then
    echo "### credentials"
    DEV_PASS="$(openssl rand -hex 16)"
    SRV_PASS="$(openssl rand -hex 16)"
    cat > creds.env <<EOF
DEVICE_ID=$DEVICE_ID
DEVICE_USER=$DEVICE_ID
DEVICE_PASS=$DEV_PASS
SERVER_USER=nps-server
SERVER_PASS=$SRV_PASS
BROKER_IP=$BROKER_IP
EOF
    rm -f passwd
    docker run --rm -u "$(id -u):$(id -g)" -v "$OUT:/m" "$IMAGE" \
        mosquitto_passwd -b -c /m/passwd "$DEVICE_ID" "$DEV_PASS"
    docker run --rm -u "$(id -u):$(id -g)" -v "$OUT:/m" "$IMAGE" \
        mosquitto_passwd -b /m/passwd nps-server "$SRV_PASS"
fi

# Mosquitto runs as uid 1883 inside the container and must read the key.
chmod 644 server.key passwd

# shellcheck disable=SC1091
source creds.env

SECRETS="$ROOT/app/inc/conf_secrets.h"
if [[ ! -f "$SECRETS" ]]; then
    echo "### creating app/inc/conf_secrets.h from the template (fill in Wi-Fi!)"
    cp "$ROOT/app/inc/conf_secrets.example.h" "$SECRETS"
fi
echo "### app/inc/conf_secrets.h: MQTT credentials"
sed -i -E \
    -e "s|^#define NPS_MQTT_USER .*|#define NPS_MQTT_USER       \"$DEVICE_USER\"|" \
    -e "s|^#define NPS_MQTT_PASS .*|#define NPS_MQTT_PASS       \"$DEVICE_PASS\"|" \
    "$SECRETS"
grep -q '"your-ssid"' "$SECRETS" && echo "### NOTE: set NPS_WIFI_SSID / NPS_WIFI_PASS in app/inc/conf_secrets.h"

echo "### done: CA $(openssl x509 -in ca.crt -noout -fingerprint -sha256 | cut -d= -f2)"
