# Network-Power-Switch — MQTT API (device ⇄ server)

This is the contract between the relay board firmware and the backend that
the Android app talks to. Firmware version **1.0.0**. Source of truth for the
firmware side: `app/inc/conf_app.h`, `app/src/cmd_codec.c`,
`app/src/mqtt_link.c`.

```
 Android app ──HTTPS──▶ Server API ──MQTT/TLS──▶ Broker ◀──MQTT/TLS── Board (NUCLEO-H723ZG)
                         (publishes /set,                    (subscribes /set,
                          reads /state, /status)              publishes /state, /status)
```

The board only ever dials **out** to the broker, so it works behind any NAT
with no port forwarding. The app never talks to the broker directly; the
server holds the broker credentials.

---

## Transport

| Item | Value |
|---|---|
| Protocol | MQTT 3.1.1 |
| Port | 8883, TLS only (no plaintext listener needed) |
| TLS | 1.2. Cipher suites: ECDHE-ECDSA or ECDHE-RSA with AES-GCM |
| Broker cert | Must chain to the CA compiled into the firmware (`NPS_BROKER_CA_PEM`). With the broker reached by IP, the cert needs that IP as a `subjectAltName` iPAddress entry |
| Cert dates | **Not checked** (the board has no clock). Trust comes from the pinned private CA |
| Device auth | Username + password. Username = device id |
| Client id | Device id, e.g. `nps-0001` |
| Keep-alive | 30 s. The broker declares the device dead after ~45 s of silence |
| Clean session | Yes. The device re-subscribes on every connect |

## Topics

`<id>` is the device id (`NPS_DEVICE_ID`). Channels `<n>` are **1-based**:
1..4.

| Topic | Direction | QoS | Retain | Payload |
|---|---|---|---|---|
| `nps/<id>/relay/<n>/set` | server → device | 1 | **must be false** | `{"on":true}` / `{"on":false}` |
| `nps/<id>/relay/<n>/state` | device → server | 1 | true | `{"on":true,"src":"app"}` |
| `nps/<id>/status` | device → server | 1 | true | `{"online":true,"fw":"1.0.0","relays":4}`, or LWT `{"online":false}` |

### `relay/<n>/set`: command

```json
{"on": true}
```

- `on` (boolean, required) is the only field the device reads. Unknown fields
  are ignored, so it is safe to add e.g. `{"on":true,"req":"a1b2"}`.
- Strict booleans: `true`/`false`. `1`, `"true"` and `"ON"` are rejected.
- At most 128 bytes, and a flat object: nested objects/arrays are rejected.
- Rejected commands are dropped with a log line on the device UART. **No
  state message is sent for a rejected command.** The server should treat
  "no matching state within ~3 s" as a failure.
- Idempotent: setting a relay to the state it already has is fine, and still
  produces a state message (the confirmation the app waits for).
- **Never publish `/set` with retain.** The device cannot tell a retained
  command from a live one, so a retained `{"on":true}` would switch the load
  back on after every reboot or reconnect. Enforce this server-side.

### `relay/<n>/state`: confirmed state

```json
{"on": true, "src": "app"}
```

| `src` | Meaning |
|---|---|
| `boot` | Power-up default. Always `"on":false` |
| `app` | Changed by a `/set` command |
| `button` | Changed by the local button on the board (channel 1) |

Published (retained) when:
1. Any relay changes, whatever caused it.
2. A `/set` command is applied, even if the state did not change.
3. Every (re)connect: all four channels, so the retained values are never
   stale after an outage.

**The app should display `/state`, not what it asked for.** The state
message is the only proof the relay actually moved.

### `status`: online/offline

- On connect: retained `{"online":true,"fw":"1.0.0","relays":4}`.
- Last Will (set at CONNECT): retained `{"online":false}`. The broker
  publishes it when the device disappears without a clean disconnect. Power
  loss or a pulled cable: after ~45 s of keep-alive silence. A reset: as soon
  as the rebooted board reconnects (session takeover), immediately followed by
  the new online status (verified on hardware).

## Device behaviour the server/app must know

| Event | Relay outputs | What the server sees |
|---|---|---|
| Power-up / reset / watchdog | **All OFF** | `status` offline (LWT) → online → 4× `state {"on":false,"src":"boot"}` |
| Broker or network lost | **Held** (unchanged) | `status` offline (LWT) |
| Reconnect | Held | `status` online → 4× `state` with current values |
| Local button (B1) | Channel 1 toggles | `state {"on":…,"src":"button"}` (queued until online if offline) |

Reconnect backoff: 2 s, doubling to 60 s max. It resets after a successful
connect.

## Broker ACL (recommended)

Scope every device to its own subtree, so a leaked device password cannot
switch other units:

```
# device usernames == device ids
pattern read  nps/%u/relay/+/set
pattern write nps/%u/relay/+/state
pattern write nps/%u/status

user nps-server
topic readwrite nps/#
```

If the device's subscription is refused (SUBACK failure), it logs
`subscribe refused: check broker ACL`, lights the red LED (LD3), drops the
session and retries with backoff.

## Server implementation notes

- **Press button → relay on:** `PUBLISH nps/<id>/relay/<n>/set {"on":true}`
  (QoS 1, retain false). Then wait for `nps/<id>/relay/<n>/state`; time out
  after ~3 s and report the failure to the app.
- **Show current state:** subscribe to `nps/+/relay/+/state` and
  `nps/+/status`. Retained messages give the full picture instantly on
  (re)subscribe; cache them per device.
- **Offline device:** if `status.online == false`, fail the request fast
  instead of publishing a command nobody will receive. (Clean-session QoS 1
  messages are not queued for an offline device.)
- Per-device credentials: one broker user per device id, password provisioned
  into that unit's `app/inc/conf_secrets.h` at build time.

## Bench test

`tools/broker/` runs a local Mosquitto with exactly this configuration:

```bash
tools/broker/gen_certs.sh            # bench CA, broker cert, passwords, conf_secrets.h
tools/broker/run_broker.sh           # docker container nps-broker on :8883
tools/broker/nps_ctl.sh watch        # print everything under nps/<id>/#
tools/broker/nps_ctl.sh on 1         # = the app pressing "ON" for relay 1
tools/broker/nps_ctl.sh off 1
```
