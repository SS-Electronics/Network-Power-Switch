# Network-Power-Switch

Internet-controlled 4-channel mains relay on a **NUCLEO-H723ZG**, wired Ethernet.
A button press in the Android app → the server publishes an MQTT command → this
board switches the relay and publishes the confirmed state back.

- MQTT 3.1.1 over **TLS 1.2** (mbedTLS 3.6), broker CA pinned, username/password auth
- Board dials out to the broker: works behind NAT, no port forwarding
- Relays **OFF on every boot/reset**; **held** through network/broker loss
- Local button (B1) toggles channel 1; the app sees it as `src:"button"`
- Hardware watchdog (IWDG) fed only while the MQTT task *and* the tcpip thread are alive
- Runs behind the STM32H723 Ethernet bootloader (image at `0x08020000`)

The server/app contract is in **[docs/mqtt_api.md](docs/mqtt_api.md)**.

## Hardware

| Function | Pin | Nucleo header |
|---|---|---|
| Relay CH1 | PF15 | D2 |
| Relay CH2 | PE13 | D3 |
| Relay CH3 | PF14 | D4 |
| Relay CH4 | PE11 | D5 |
| LD1 green: network | PB0 | on-board. Short blip = offline, fast blink = connecting, solid = online |
| LD2 yellow: CH1 mirror | PE1 | on-board |
| LD3 red: fault | PB14 | on-board. Bad credentials / ACL refused / bad CA |
| Button B1: toggle CH1 | PC13 | on-board |
| Console + shell | USART3 VCP | `/dev/ttyACM0`, 115200 8N1 |
| Ethernet | LAN8742 RMII | on-board RJ45 |

**Relay module:** a common 4-channel opto-isolated **active-LOW** board.
Inputs are driven **open-drain**: LOW = relay ON, released = the module's own
pull-up = OFF. That lets a 5 V-powered module switch fully off (a 3.3 V
push-pull HIGH can leave the opto partly on). Wire module `GND` to Nucleo
`GND`, and power its `VCC` / `JD-VCC` from 5 V per the module's jumper.

## Build, flash, test

Prerequisites: `arm-none-eabi-gcc` 13.x, `openocd`, `python3`, Docker (bench broker).

```bash
git submodule update --init --recursive    # FreeRTOS-OS, lib/mbedtls

tools/broker/gen_certs.sh     # bench CA + broker cert + passwords → app/inc/conf_secrets.h
tools/broker/run_broker.sh    # Mosquitto TLS broker on :8883 (docker "nps-broker")

make                          # → FreeRTOS-OS/build/nps.elf (linked at 0x08020000)
make flash                    # SWD; the bootloader in sector 0 is not touched
make test                     # host unit tests for the protocol codec

tools/broker/nps_ctl.sh watch # watch nps/<id>/#
tools/broker/nps_ctl.sh on 1  # what the server does when the app presses ON
```

`make rebuild` after editing any **header** (`lwipopts.h`, `conf_app.h`, …):
the FreeRTOS-OS build does not track header dependencies.

Flashing over the network instead of SWD should work through the bootloader's
`boot-host` tool with `FreeRTOS-OS/build/nps.elf.hex` (the bootloader listens
for 5 s after reset). Not yet exercised with this image; SWD is verified.

## Configuration

| What | Where |
|---|---|
| Device id, broker IP/port, topics, relay pins, timings | `app/inc/conf_app.h` |
| MQTT username/password, broker CA PEM | `app/inc/conf_secrets.h` (gitignored; template `conf_secrets.example.h`) |
| Board IP / netmask / gateway / MAC | `app/board/lwipopts.h` (`NET_*`) |
| TLS profile | `app/inc/mbedtls_config_nps.h` |
| OS features (Kconfig preset) | `app/kconfig_h723.conf` |

Every unit needs a unique `NPS_DEVICE_ID`, `NET_IP_ADDR*` and `NET_MAC_ADDR*`,
plus its own broker credentials.

## Layout

```
Makefile                 top-level entry (gen → config → build, flash, test)
FreeRTOS-OS/             OS + HAL + lwIP (submodule, unmodified)
lib/mbedtls/             mbedTLS v3.6.7 LTS (submodule, shallow)
app/
  app_main.c             boot order, net guard
  src/relay.c            relay outputs (HAL direct, glitch-free, open-drain)
  src/mqtt_link.c        MQTT/TLS session state machine, liveness watchdog
  src/cmd_codec.c        topic + JSON codec (host-tested)
  src/button.c           B1 → toggle CH1
  src/entropy_hw.c       TRNG → mbedTLS entropy
  third_party/lwip_altcp_tls/   lwIP TLS adapter ported to mbedTLS 3.6
  board/                 board XML, IRQ table, lwipopts.h, linker script
  inc/                   conf_app.h, conf_secrets*.h, mbedtls config, headers
  test/                  host unit tests
docs/mqtt_api.md         device ⇄ server contract
tools/broker/            bench Mosquitto (TLS, ACL) + CLI helper
```

## Known limitations (v1)

- **Static IP, broker by IP.** No DHCP/DNS. The broker cert must carry the broker IP as a SAN.
- **Certificate dates not checked:** no wall clock (no SNTP/RTC). Trust rests on the pinned CA.
- **Cable unplugged at boot:** FreeRTOS-OS only brings Ethernet up if the PHY links during boot. The app reboots after 30 s to retry, but only while all relays are OFF.
- **Link speed change after replug:** the OS sets MAC speed/duplex once at boot; replugging into a port that negotiates differently needs a reset.
- **CPU at 64 MHz** (FreeRTOS-OS H7 default, no PLL): TLS connect takes ~0.84 s, during which lower-priority tasks (button) wait; this logs one tolerated watchdog miss per connect.
- `/set` must never be retained (the device cannot detect it). See the API doc.
