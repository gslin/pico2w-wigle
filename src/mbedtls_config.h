// mbedtls configuration for pico2w-wigle.
//
// Goal: TLS 1.2 client capable of validating an api.wigle.net cert
// chain (ECDHE + RSA, AES-GCM / AES-CBC + SHA-256). We deliberately
// skip server-only ciphersuites and most diagnostic features to keep
// the binary small enough for the Pico's flash.

#pragma once

// pico-sdk's vendored copy of altcp_tls_mbedtls.c (in
// src/rp2_common/pico_lwip/) reaches into mbedtls_ssl_context fields
// like out_left and mbedtls_ssl_session::start, which mbedtls 3.x
// hides behind the MBEDTLS_PRIVATE() macro. Setting this flag
// reverts MBEDTLS_PRIVATE() to a no-op so those struct members are
// visible again. It's a documented (if begrudged) migration escape
// hatch — drop it once pico-sdk's altcp glue is updated to use the
// public accessors.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

// Platform glue
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_NO_PLATFORM_ENTROPY

// HAVE_TIME has to be on because pico-sdk's vendored
// altcp_tls_mbedtls.c reaches into mbedtls_ssl_session::start, which
// only exists when HAVE_TIME is defined. mbedtls 3.x's own
// platform_util.c then #errors on non-POSIX targets because it has
// no embedded ms_time backend, so we opt out of mbedtls's
// implementation via MBEDTLS_PLATFORM_MS_TIME_ALT and provide our
// own mbedtls_ms_time() in src/mbedtls_glue.c (Pico uptime in ms).
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_MS_TIME_ALT

// Bignum and ASN.1
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_OID_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

// Symmetric crypto
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC

// Hashes
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

// Public key crypto
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

// X.509
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C

// Random / entropy: the mbedtls_hardware_poll provided by pico_mbedtls
// supplies entropy from RP2350's hardware RNG. CTR_DRBG is required
// because pico-sdk's vendored altcp_tls_mbedtls.c calls
// mbedtls_ctr_drbg_init / _seed / _random / _free directly; HMAC_DRBG
// is kept for any internal mbedtls path that picks the lighter option.
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_HMAC_DRBG_C

// TLS
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

// Tuning: these dominate static RAM use. 16 KiB matches the TLS record
// limit and is the minimum to interoperate with most servers.
#define MBEDTLS_SSL_OUT_CONTENT_LEN 16384
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384

// NOTE: do not #include "mbedtls/check_config.h" here. Mbed TLS 3.x
// runs that check itself, after build_info.h has had a chance to
// auto-derive flags like MBEDTLS_MD_CAN_SHA256 from MBEDTLS_SHA256_C.
// Including it manually fires the check too early and produces
// spurious "MBEDTLS_MD_C defined, but no hash algorithm" errors.
