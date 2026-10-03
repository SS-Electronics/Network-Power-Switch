# Network-Power-Switch

Internet-controlled 4-channel mains relay on an **STM32F401CDU6** with an
**ESP8266** Wi-Fi module on UART.
A button press in the Android app → the server publishes an MQTT command → the
ESP8266 delivers it → the STM32 switches the relay and publishes the confirmed
state back.

- MQTT 3.1.1 over **TLS** (terminated on the ESP8266, Espressif ESP-AT ≥ 2.2), broker CA verified, username/password auth
- Board dials out to the broker: works behind NAT, no port forwarding
- Relays **OFF on every boot/reset**; **held** through Wi-Fi/broker loss
- Local KEY button toggles channel 1; the app sees it as `src:"button"`
- MCU resets the ESP8266 when it stops answering; IWDG resets the MCU if a task hangs
- FreeRTOS-OS submodule **unmodified**: everything board-specific is configured from `app/`

The server/app contract is in **[docs/mqtt_api.md](docs/mqtt_api.md)**.
ESP8266 firmware and CA provisioning: **[tools/esp8266/README.md](tools/esp8266/README.md)**.

## Hardware

| Function | Pin | Note |
|---|---|---|
| Relay CH1..CH4 | PB12, PB13, PB14, PB15 | open-drain, active-LOW (5 V tolerant) |
| ESP8266 UART | USART1: PA9 → ESP RX, PA10 ← ESP TX | 115200 8N1 |
| ESP8266 RST | PB0 | open-drain + pull-up; optional |
| Console + OS shell | USART2: PA2 TX, PA3 RX | 3.3 V USB-UART, 115200 8N1 |
| Status LED | PC13 (on-board, active-LOW) | blip/2 s = ESP init · 1 Hz = joining Wi-Fi · 5 Hz = connecting MQTT · solid = online |
| KEY button | PA0 (to GND, pull-up) | toggles CH1 |
| SWD | PA13 / PA14 | ST-LINK |

**Relay module:** a common 4-channel opto-isolated **active-LOW** board.
Inputs are driven **open-drain**: LOW = relay ON, released = the module's own
pull-up = OFF. That lets a 5 V-powered module switch fully off. Wire module
`GND` to board `GND`; power `VCC`/`JD-VCC` from 5 V per the module's jumper.

**ESP8266:** needs ESP-AT ≥ 2.2 (MQTT AT commands), ≥ 2 MB flash, a solid
3.3 V supply (≥ 500 mA peak), and the broker CA flashed into its `mqtt_ca`
partition. See [tools/esp8266/README.md](tools/esp8266/README.md).

## Build, flash, test

Prerequisites: `arm-none-eabi-gcc` 13.x, `openocd` or STM32CubeProgrammer, `python3`, Docker (bench broker).

```bash
git submodule update --init --recursive    # FreeRTOS-OS

tools/broker/gen_certs.sh     # bench CA + broker cert + MQTT creds → app/inc/conf_secrets.h
$EDITOR app/inc/conf_secrets.h    # set NPS_WIFI_SSID / NPS_WIFI_PASS
tools/broker/run_broker.sh    # Mosquitto TLS broker on :8883 (docker "nps-broker")

make                          # → FreeRTOS-OS/build/nps.elf
make flash                    # OpenOCD over ST-LINK (see note below)
make test                     # host unit tests: protocol codec + AT parser

tools/broker/nps_ctl.sh watch # watch nps/<id>/#
tools/broker/nps_ctl.sh on 1  # what the server does when the app presses ON
```

`make rebuild` after editing any **header** (`conf_app.h`, …): the
FreeRTOS-OS build does not track header dependencies.

If OpenOCD cannot halt the target ("timed out while waiting for target
halted"), flash with connect-under-reset:
`STM32_Programmer_CLI -c port=SWD mode=UR -d FreeRTOS-OS/build/nps.elf.hex -v -rst`.

**No console on the ST-LINK/V2:** connect a USB-UART to PA2/PA3 for logs and
the OS shell, or read the link diagnostics over SWD: the `g_nps_diag` struct
(state, Wi-Fi/MQTT up, ESP resets, ESP version string, last ESP line) in
`app/src/mqtt_link.c`.
`rx_line_low = 1` means the ESP8266 is not running at all (its TX idles HIGH
when alive): check EN/CH_PD → 3V3 (10 kΩ), RST → 3V3 or PB0, GPIO0 HIGH /
GPIO15 LOW, and a 3.3 V supply that holds up under ~300 mA peaks.

## Configuration

| What | Where |
|---|---|
| Device id, broker host/port, TLS scheme, static IP, relay/RST pins, timings | `app/inc/conf_app.h` |
| Wi-Fi SSID/password, MQTT username/password | `app/inc/conf_secrets.h` (gitignored; template `conf_secrets.example.h`) |
| UART/LED/button pins | `app/board/nps_f401.xml` (+ `irq_table.xml`) |
| OS features (Kconfig preset) | `app/kconfig_f401.conf` |
| Broker CA | ESP8266 `mqtt_ca` partition (not in the MCU image) |

Every unit needs a unique `NPS_DEVICE_ID` and `NPS_WIFI_STATIC_IP` (or comment
the static IP out for DHCP), plus its own broker credentials.

## How FreeRTOS-OS is configured without modifying it

| Gap in the OS | Handled in the app |
|---|---|
| No maintained STM32F401 target | Built through the `STM32F411xE` path (F401 is a peripheral subset; same core and AF map) |
| F411 memory map (512 K / 128 K) | `app/board/nps_f401cd.ld` (384 K / 96 K) via `LINKER_SCRIPT` override in `app/Makefile` |
| Generator clock tree is 100 MHz (F401 max 84 MHz) | Top-level `make gen` rewrites the generated `board_config.h` to 84 MHz / 2 WS, and fails the build if that ever stops matching |
| IWDG HAL source not built on the F4 path | compiled from `app/Makefile` |
| GPIO outputs start LOW, `active_state` ignored | relays and ESP RST driven by app code (latch level, then open-drain); LED polarity inverted in `mqtt_link.c` |
| Only the shell UART gets an RX buffer | `esp_at.c` subscribes to `IRQ_ID_UART_RX(UART_ESP)` into its own stream buffer |
| UART transmit budget 10 ms | AT commands sent in 64-byte chunks |
| printk over-reads lines ≥ buffer, silent first ~2 s | `CONFIG_ITM_PRINT_BUFF_LENGTH=128`, short log lines |
| OS requires I2C1 + SPI1 entries | declared (PB6/PB9, PA5–PA7), unused |

## Layout

```
Makefile                 top-level entry (gen + 84 MHz fix → config → build, flash, test)
FreeRTOS-OS/             OS + HAL (submodule, unmodified)
app/
  app_main.c             boot order
  src/relay.c            relay outputs (HAL direct, glitch-free, open-drain)
  src/mqtt_link.c        ESP-AT state machine: ESP init → Wi-Fi → MQTT → online
  src/esp_at.c           ESP8266 UART transport (RX stream, AT command engine, RST)
  src/at_parse.c         AT line framing/classification/escaping (host-tested)
  src/cmd_codec.c        topic + JSON codec (host-tested)
  src/button.c           KEY → toggle CH1
  board/                 board XML, IRQ table, linker script
  inc/                   conf_app.h, conf_secrets*.h, headers
  test/                  host unit tests
docs/mqtt_api.md         device ⇄ server contract
tools/broker/            bench Mosquitto (TLS, ACL) + CLI helper
tools/esp8266/           ESP-AT flashing + CA provisioning guide
```

## Known limitations

- **TLS trust lives in the ESP8266.** The CA sits in the ESP's flash, and the UART link between MCU and ESP carries plaintext MQTT payloads and credentials (inside the enclosure).
- ESP8266 mbedTLS 2.x: broker reached by IP needs a `DNS:<ip>` SAN; certificate dates are not checked (no SNTP configured).
- Static IP by default (set in `conf_app.h`); no DNS server configured, so a hostname broker also needs DHCP or `AT+CIPDNS`.
- ESP-AT does not tell auth failures apart from TLS failures (both `ERROR`). See `g_nps_diag.last_line` / console for the error code line.
- `/set` must never be retained (the device cannot detect it). See the API doc.
