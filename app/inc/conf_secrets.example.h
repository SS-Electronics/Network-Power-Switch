/**
 * @file        conf_secrets.example.h
 * @brief       Template for conf_secrets.h (MQTT credentials + broker CA)
 *
 * @info        Copy to conf_secrets.h and fill in, or run
 *              tools/broker/gen_certs.sh, which writes conf_secrets.h for the
 *              local test broker. conf_secrets.h is gitignored: never commit
 *              real credentials.
 */

#ifndef APP_CONF_SECRETS_H_
#define APP_CONF_SECRETS_H_

/** Broker username / password for this device. */
#define NPS_MQTT_USER       "nps-0001"
#define NPS_MQTT_PASS       "change-me"

/**
 * PEM of the CA that signed the broker certificate. The device trusts only
 * this CA. Keep the trailing "\n"; the NUL terminator is part of the length
 * passed to mbedTLS (sizeof).
 */
#define NPS_BROKER_CA_PEM                                   \
    "-----BEGIN CERTIFICATE-----\n"                         \
    "...\n"                                                 \
    "-----END CERTIFICATE-----\n"

#endif /* APP_CONF_SECRETS_H_ */
