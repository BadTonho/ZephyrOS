#ifndef TLS_H
#define TLS_H

#include "types.h"

#define TLS_VERSION_1_2 0x0303U
#define TLS_VERSION_1_3 0x0304U

typedef enum {
    TLS_STATE_UNINITIALIZED = 0,
    TLS_STATE_POLICY_ONLY,
    TLS_STATE_UNAVAILABLE
} tls_state_t;

typedef enum {
    TLS_REASON_NONE = 0,
    TLS_REASON_NOT_INITIALIZED,
    TLS_REASON_TIME_UNAVAILABLE,
    TLS_REASON_CERT_NOT_YET_VALID,
    TLS_REASON_CERT_EXPIRED,
    TLS_REASON_UNTRUSTED_CHAIN,
    TLS_REASON_TRUST_REVOKED,
    TLS_REASON_HOSTNAME_MISMATCH,
    TLS_REASON_PIN_MISMATCH,
    TLS_REASON_POLICY,
    TLS_REASON_UNSUPPORTED
} tls_reason_t;

typedef struct {
    uint16_t minimum_version;
    uint8_t require_static_ca;
    uint8_t require_hostname_san;
    uint8_t require_validity_window;
    uint8_t allow_spki_pin;
    uint8_t allow_http_fallback;
    uint32_t trust_current_version;
    uint32_t trust_next_version;
    uint32_t trust_revocation_version;
} tls_policy_t;

/* O parser X.509 futuro preenchera estes campos antes da politica final. */
typedef struct {
    uint64_t not_before;
    uint64_t not_after;
    uint8_t chain_trusted;
    uint8_t hostname_san_match;
    uint8_t pin_configured;
    uint8_t pin_match;
    uint32_t trust_version;
    uint8_t trust_revoked;
} tls_peer_identity_t;

typedef struct {
    uint8_t initialized;
    uint8_t policy_ready;
    uint8_t handshake_available;
    uint8_t x509_available;
    uint8_t trusted_time_available;
    uint8_t static_ca_required;
    uint8_t spki_pinning_optional;
    uint8_t http_fallback_forbidden;
    tls_state_t state;
    tls_reason_t last_reason;
    uint32_t policy_checks;
    uint32_t policy_rejections;
    int last_error;
} tls_status_t;

typedef struct {
    uint8_t valid_identity;
    uint8_t time_unavailable;
    uint8_t certificate_future;
    uint8_t certificate_expired;
    uint8_t untrusted_chain;
    uint8_t san_mismatch;
    uint8_t pin_absent;
    uint8_t pin_match;
    uint8_t pin_mismatch;
    uint8_t trust_rotation;
    uint8_t trust_revocation;
    uint8_t invariants;
    uint8_t passed;
    uint8_t failed;
} tls_self_test_result_t;

int tls_init(void);
int tls_get_policy(tls_policy_t* out_policy);
int tls_policy_validate(const tls_peer_identity_t* identity,
                        tls_reason_t* out_reason);
int tls_get_status(tls_status_t* out_status);
int tls_capability_available(void);
int tls_validate_state(void);
int tls_self_test(tls_self_test_result_t* out_result);
const char* tls_state_name(tls_state_t state);
const char* tls_reason_name(tls_reason_t reason);

#endif
