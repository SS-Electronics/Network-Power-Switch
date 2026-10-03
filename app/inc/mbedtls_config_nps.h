/**
 * @file        mbedtls_config_nps.h
 * @brief       mbedTLS 3.6 configuration: TLS 1.2 client for MQTT over TLS
 *
 * @info        Selected with -DMBEDTLS_CONFIG_FILE (app/Makefile). Minimal
 *              client profile:
 *                - TLS 1.2 only. 1.3 in 3.6 needs the PSA crypto core, which
 *                  roughly doubles the footprint for no gain on this link.
 *                - ECDHE key exchange with ECDSA or RSA server certificates,
 *                  AES-GCM records. P-256 / P-384 curves.
 *                - Entropy from the STM32H7 TRNG (app/src/entropy_hw.c).
 *                - Heap from lwIP's mem_malloc via MBEDTLS_PLATFORM_MEMORY
 *                  (lwIP's altcp_tls_mbedtls_mem.c installs the hooks); the
 *                  lwIP heap lives in AXI SRAM.
 *                - No wall clock: certificate validity dates are NOT checked.
 *                  Trust rests on the pinned private CA instead.
 */

#ifndef APP_MBEDTLS_CONFIG_NPS_H_
#define APP_MBEDTLS_CONFIG_NPS_H_

/* lwIP's TLS adapter reads ssl_context.out_left directly. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

/* ── Platform ───────────────────────────────────────────────────────────── */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_DEPRECATED_REMOVED

/* ── Big numbers / EC ───────────────────────────────────────────────────── */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

/* ── Symmetric / hashes ─────────────────────────────────────────────────── */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

/* ── RNG ────────────────────────────────────────────────────────────────── */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* ── Certificates ───────────────────────────────────────────────────────── */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* ── TLS ────────────────────────────────────────────────────────────────── */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET

/* Broker records can be up to 16 KB; outbound MQTT frames are tiny. */
#define MBEDTLS_SSL_IN_CONTENT_LEN      16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN     4096

/*
 * Server Name Indication is left out: the broker is reached by IP, and an IP
 * literal is not a valid SNI name (RFC 6066). Re-enable if the broker moves
 * to a hostname behind a shared TLS front-end.
 */

#endif /* APP_MBEDTLS_CONFIG_NPS_H_ */
