#!/usr/bin/env bash
# run_broker.sh — start (or restart) the bench Mosquitto broker on :8883.
#
# Usage: tools/broker/run_broker.sh          start, detached, container "nps-broker"
#        tools/broker/run_broker.sh stop
#        docker logs -f nps-broker           follow connection log

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="nps-broker"

docker rm -f "$NAME" >/dev/null 2>&1 || true
[[ "${1:-}" == "stop" ]] && exit 0

[[ -f "$HERE/out/server.crt" ]] || { echo "run tools/broker/gen_certs.sh first"; exit 1; }

docker run -d --name "$NAME" -p 8883:8883 \
    -v "$HERE/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro" \
    -v "$HERE/acl:/mosquitto/config/acl:ro" \
    -v "$HERE/out:/mosquitto/config/out:ro" \
    eclipse-mosquitto:2 >/dev/null

sleep 1
docker logs "$NAME" 2>&1 | tail -3
