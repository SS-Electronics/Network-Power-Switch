# Network-Power-Switch — Firmware Context

Ethernet MQTT/TLS 4-channel relay controller. NUCLEO-H723ZG (STM32H723ZGTx,
Cortex-M7) on the FreeRTOS-OS submodule. Structure mirrors
`3_Consultant/Cookster/Cookster_Comms_PCB_Firmware` (top Makefile + `app/`
fragment built by `FreeRTOS-OS/Makefile` with `APP_DIR=../app`).

Read first: `README.md` (pins, build, limitations), `docs/mqtt_api.md`
(server contract: change it together with `conf_app.h` / `cmd_codec.c`).

## Build

```
make            # gen + config + build → FreeRTOS-OS/build/nps.elf (0x08020000)
make rebuild    # REQUIRED after any header edit: OS build has no header deps
make flash      # OpenOCD SWD
make test       # host unit tests (cmd_codec), ASan/UBSan
```

## Invariants (do not break)

- **Relays OFF before anything runs.** `relay_init()` is the first call in
  `app_main()`. Relay pins are NOT in the board XML: the OS gpio path sets
  ODR=0 and ignores `active_state` → active-LOW relays would energise at boot.
  `relay.c` writes ODR=OFF *then* configures open-drain.
- **Every lwIP call from a task holds `LOCK_TCPIP_CORE()`.** MQTT/altcp
  callbacks run in tcpip and only post packed-integer events to the mqtt mbox.
- **`mqtt_client_connect()` memsets the client:** set inpub callbacks after it.
- **mbedTLS ≥3.6.3 needs `mbedtls_ssl_set_hostname()`** before the handshake
  or verification fails. Done in `link_connect()` under the core lock.
- `ALTCP_MBEDTLS_AUTHMODE` must stay `MBEDTLS_SSL_VERIFY_REQUIRED` (lwIP's
  default is OPTIONAL = accepts any cert).
- `TCP_WND` ≥ 16 KB TLS record; `PBUF_POOL_SIZE` 16 satisfies lwIP's check.
- printk is **disabled for the first ~2 s** of boot (OS gate), and lines must
  stay **< 128 chars** (`CONFIG_ITM_PRINT_BUFF_LENGTH`; the OS over-reads
  past the buffer on longer lines).
- FreeRTOS-OS requires I2C1 + SPI1 entries in the board XML (BOARD_*_COUNT
  macros) even though the app does not use them.
- Board XML `name` must be a C identifier (it becomes an include guard).

## Third-party patches

- `app/third_party/lwip_altcp_tls/altcp_tls_mbedtls.c`: lwIP's TLS adapter
  ported 2.x → mbedTLS 3.6 (changes marked `NPS:`). The submodules are
  unmodified.

## Hardware-verified (2026-10-03, bench 192.168.0.x)

TLS 1.2 ECDHE-ECDSA-AES256-GCM-SHA384 to Mosquitto 2.1.2 in 838 ms at
64 MHz; set/state round trip; pin levels read back over SWD; malformed /
out-of-range / oversized commands rejected; broker restart → backoff
reconnect with relays held; reset → LWT offline → online → all OFF `boot`.
