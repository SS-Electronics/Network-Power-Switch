# ESP8266 setup (ESP-AT firmware + broker CA)

The STM32F401 talks to the ESP8266 with AT commands. Wi-Fi, TCP, TLS and the
MQTT client all run **on the ESP8266**, so the module needs:

1. **ESP-AT ≥ 2.2 for ESP8266** (the "ESP8266-IDF-AT" line). It is the
   release family with the `AT+MQTT…` commands. The old NonOS AT 1.x that
   most ESP-01 modules ship with has **no MQTT commands**. With it the
   firmware logs `MQTTUSERCFG failed: ESP-AT >= 2.2 with MQTT support required`.
2. **≥ 2 MB SPI flash** on the module (ESP-AT 2.x does not fit 1 MB; most
   ESP-01/ESP-01S boards are 1 MB). ESP-12E/F, ESP-07S and WROOM-02 modules
   are fine.
3. **The broker CA in the `mqtt_ca` partition**, because the firmware uses
   `AT+MQTTUSERCFG` scheme 3 (TLS, verify the broker certificate).

## Wiring (STM32F401CDU6)

| ESP8266 | STM32F401 | Note |
|---|---|---|
| TX (GPIO1) | PA10 (USART1 RX) | |
| RX (GPIO3) | PA9 (USART1 TX) | |
| RST | PB0 | open-drain, optional (else the MCU sends `AT+RST`) |
| EN / CH_PD | 3.3 V | via 10 kΩ |
| GPIO0 | 3.3 V | via 10 kΩ (LOW = ROM download mode) |
| GPIO15 | GND | via 10 kΩ (most modules do this on-board) |
| VCC | 3.3 V, **≥ 500 mA** | Wi-Fi TX peaks ~350 mA. Add 100–470 µF at the module |
| GND | GND | common ground |

UART: 115200 8N1, no flow control (ESP-AT default).

## 1. Flash ESP-AT

Download the ESP8266 AT release binaries from Espressif
(<https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Binary_Lists/ESP8266_AT_binaries.html>),
then put the module in download mode (GPIO0 LOW during reset) behind a
USB-UART adapter:

```bash
pip install esptool
esptool.py --chip esp8266 --port /dev/ttyUSB0 erase_flash
esptool.py --chip esp8266 --port /dev/ttyUSB0 --baud 460800 \
    write_flash -fm dio 0x0 factory/factory_WROOM-02.bin   # file name per release
```

Check it from a terminal (115200, CR+LF line endings):

```
AT            → OK
AT+GMR        → AT version:2.2.x ...
AT+MQTTUSERCFG=?   → +MQTTUSERCFG:... OK   (MQTT support present)
```

## 2. Put the broker CA into `mqtt_ca`

`tools/broker/gen_certs.sh` writes the bench CA to `tools/broker/out/ca.crt`
(for a production broker, use the CA that issued its certificate).

ESP-AT keeps MQTT certificates in a dedicated flash partition. Build the
partition image with the `AtPKI.py` tool from the ESP-AT sources (under
`tools/`), then flash it at the **`mqtt_ca` offset of your release**. Look
the offset up in that release's `at_customize.csv` / partition table.
It depends on the module flash size and release, so it is deliberately not
hard-coded here.

```bash
python AtPKI.py generate_bin -b mqtt_ca.bin cert tools/broker/out/ca.crt
esptool.py --chip esp8266 --port /dev/ttyUSB0 write_flash <mqtt_ca_offset> mqtt_ca.bin
```

Releases with `AT+SYSMFG` can write the CA over the AT port instead; ESP8266
2.2.x releases do not have it.

> **Bench shortcut (do not ship):** set `NPS_MQTT_SCHEME 2` in
> `app/inc/conf_app.h` for TLS **without** certificate verification while
> the CA step is pending. Anyone on the path can then impersonate the broker
> and switch your relays.

## 3. Broker certificate requirements

The ESP8266's TLS stack (mbedTLS 2.x) checks the name passed to `AT+MQTTCONN`
(`NPS_BROKER_HOST`) against the certificate's **dNSName** SANs and does not
understand iPAddress SANs. When the broker is reached by IP, the certificate
must list the IP as text in a DNS SAN too:

```
subjectAltName = IP:192.168.0.100, DNS:192.168.0.100
```

`gen_certs.sh` already does this. With a real hostname (`NPS_BROKER_HOST
"mqtt.example.com"`), a normal certificate for that name works.
