/**
 * @file        conf_secrets.example.h
 * @brief       Template for conf_secrets.h (Wi-Fi + MQTT credentials)
 *
 * @info        Copy to conf_secrets.h and fill in. tools/broker/gen_certs.sh
 *              creates conf_secrets.h from this template on first run and
 *              keeps the MQTT lines in sync with the bench broker; the Wi-Fi
 *              lines are yours. conf_secrets.h is gitignored: never commit
 *              real credentials.
 *
 *              Characters '"', ',' and '\' are fine here: the firmware
 *              escapes them for the AT command line.
 */

#ifndef APP_CONF_SECRETS_H_
#define APP_CONF_SECRETS_H_

/** Wi-Fi access point the ESP8266 joins (2.4 GHz only). */
#define NPS_WIFI_SSID       "your-ssid"
#define NPS_WIFI_PASS       "your-wifi-password"

/** Broker username / password for this device (username = device id). */
#define NPS_MQTT_USER       "nps-0001"
#define NPS_MQTT_PASS       "change-me"

#endif /* APP_CONF_SECRETS_H_ */
