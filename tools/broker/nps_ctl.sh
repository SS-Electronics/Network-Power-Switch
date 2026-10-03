#!/usr/bin/env bash
# nps_ctl.sh — act as the backend against the bench broker (what the server
# does when the Android app presses a button).
#
#   tools/broker/nps_ctl.sh on  <ch>      publish {"on":true}  to relay <ch>
#   tools/broker/nps_ctl.sh off <ch>      publish {"on":false} to relay <ch>
#   tools/broker/nps_ctl.sh raw <ch> '<payload>'
#   tools/broker/nps_ctl.sh watch [secs]  print every message under nps/<id>/#
#                                         (retained state shows immediately)

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/out/creds.env"

TLS=(-h 127.0.0.1 -p 8883 --cafile /mosquitto/config/out/ca.crt
     -u "$SERVER_USER" -P "$SERVER_PASS")
BASE="nps/$DEVICE_ID"

pub()
{
    docker exec nps-broker mosquitto_pub "${TLS[@]}" -q 1 -t "$BASE/relay/$1/set" -m "$2"
}

case "${1:-}" in
    on)    pub "$2" '{"on":true}' ;;
    off)   pub "$2" '{"on":false}' ;;
    raw)   pub "$2" "$3" ;;
    watch) docker exec nps-broker mosquitto_sub "${TLS[@]}" -v -t "$BASE/#" \
               -W "${2:-3600}" || true ;;
    *)     sed -n '2,11p' "$0"; exit 1 ;;
esac
