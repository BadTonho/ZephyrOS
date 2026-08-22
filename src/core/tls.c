#include "core/tls.h"
#include "core/clock.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/tls_client.h"
#include "drivers/rng.h"

static tls_policy_t tls_policy;
static tls_status_t tls_status;

#define TLS_SELF_TEST_NOW 1700000000ULL

static int tls_reject(tls_reason_t reason, int error, const char* message,
                      tls_reason_t* out_reason) {
    tls_status.last_reason = reason;
    tls_status.last_error = error;
    tls_status.policy_rejections++;
    if (out_reason) *out_reason = reason;
    if (message) LOG_WARN("TLS", message);
    return error;
}

static int tls_policy_failure(tls_reason_t reason, int error,
                              const char* message, tls_reason_t* out_reason,
                              uint8_t record) {
    if (record) return tls_reject(reason, error, message, out_reason);
    *out_reason = reason;
    return error;
}

static int tls_policy_validate_at(const tls_peer_identity_t* identity,
                                  uint8_t time_available, uint64_t now,
                                  tls_reason_t* out_reason, uint8_t record) {
    if (tls_policy.require_static_ca && !identity->chain_trusted) {
        return tls_policy_failure(TLS_REASON_UNTRUSTED_CHAIN, ERR_INVALID,
                                  "Cadeia TLS nao confiavel", out_reason,
                                  record);
    }
    if (identity->trust_revoked ||
        (identity->trust_version != 0U &&
         identity->trust_version < tls_policy.trust_revocation_version)) {
        return tls_policy_failure(TLS_REASON_TRUST_REVOKED, ERR_INVALID,
                                  "Versao da raiz TLS revogada", out_reason,
                                  record);
    }
    if (identity->trust_version != 0U &&
        identity->trust_version != tls_policy.trust_current_version &&
        identity->trust_version != tls_policy.trust_next_version) {
        return tls_policy_failure(TLS_REASON_POLICY, ERR_INVALID,
                                  "Versao da raiz TLS fora da janela", out_reason,
                                  record);
    }
    if (tls_policy.require_hostname_san && !identity->hostname_san_match) {
        return tls_policy_failure(TLS_REASON_HOSTNAME_MISMATCH, ERR_INVALID,
                                  "SAN TLS nao corresponde ao host", out_reason,
                                  record);
    }
    if (tls_policy.require_validity_window) {
        if (!time_available) {
            return tls_policy_failure(TLS_REASON_TIME_UNAVAILABLE,
                                      ERR_UNAVAILABLE,
                                      "Tempo UTC indisponivel para certificado",
                                      out_reason, record);
        }
        if (identity->not_after < identity->not_before) {
            return tls_policy_failure(TLS_REASON_POLICY, ERR_INVALID,
                                      "Janela de validade TLS invertida",
                                      out_reason, record);
        }
        if (now < identity->not_before) {
            return tls_policy_failure(TLS_REASON_CERT_NOT_YET_VALID,
                                      ERR_INVALID,
                                      "Certificado TLS ainda nao e valido",
                                      out_reason, record);
        }
        if (now > identity->not_after) {
            return tls_policy_failure(TLS_REASON_CERT_EXPIRED, ERR_INVALID,
                                      "Certificado TLS expirado", out_reason,
                                      record);
        }
    }
    if (identity->pin_configured &&
        (!tls_policy.allow_spki_pin || !identity->pin_match)) {
        return tls_policy_failure(TLS_REASON_PIN_MISMATCH, ERR_INVALID,
                                  "Pin SPKI TLS nao corresponde", out_reason,
                                  record);
    }
    *out_reason = TLS_REASON_NONE;
    return OK;
}

int tls_init(void) {
    clock_status_t clock;
    rng_status_t rng;
    tls_client_status_t client;
    int result;

    LOG_INFO("TLS", "Inicializando contrato de politica TLS");
    kmemset(&tls_policy, 0, sizeof(tls_policy));
    kmemset(&tls_status, 0, sizeof(tls_status));
    kmemset(&client, 0, sizeof(client));
    tls_policy.minimum_version = TLS_VERSION_1_2;
    tls_policy.require_static_ca = 1U;
    tls_policy.require_hostname_san = 1U;
    tls_policy.require_validity_window = 1U;
    tls_policy.allow_spki_pin = 1U;
    tls_policy.allow_http_fallback = 0U;
    tls_policy.trust_current_version = 1U;
    tls_policy.trust_next_version = 2U;
    tls_policy.trust_revocation_version = 0U;
    tls_status.initialized = 1U;
    tls_status.policy_ready = 1U;
    tls_status.handshake_available = 0U;
    tls_status.x509_available = 0U;
    tls_status.static_ca_required = 1U;
    tls_status.spki_pinning_optional = 1U;
    tls_status.http_fallback_forbidden = 1U;
    if (clock_get_status(&clock) != OK) {
        tls_status.state = TLS_STATE_UNAVAILABLE;
        tls_status.last_reason = TLS_REASON_TIME_UNAVAILABLE;
        tls_status.last_error = ERR_STATE;
        LOG_ERROR("TLS", "Nao foi possivel consultar relogio confiavel");
        return ERR_STATE;
    }
    tls_status.trusted_time_available = clock.utc_available;
    if (!clock.utc_available) {
        tls_status.state = TLS_STATE_UNAVAILABLE;
        tls_status.last_reason = TLS_REASON_TIME_UNAVAILABLE;
        tls_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("TLS", "Tempo UTC indisponivel para validacao X.509");
        return ERR_UNAVAILABLE;
    }
    if (rng_get_status(&rng) != OK || !rng.initialized) {
        result = rng_init();
        if (result != OK) {
            tls_status.state = TLS_STATE_UNAVAILABLE;
            tls_status.last_reason = TLS_REASON_ENTROPY_UNAVAILABLE;
            tls_status.last_error = result;
            LOG_ERROR("TLS", "Fonte RDRAND indisponivel para HTTPS");
            return result;
        }
        if (rng_get_status(&rng) != OK) {
            tls_status.state = TLS_STATE_UNAVAILABLE;
            tls_status.last_reason = TLS_REASON_ENTROPY_UNAVAILABLE;
            tls_status.last_error = ERR_STATE;
            LOG_ERROR("TLS", "Nao foi possivel consultar estado RDRAND");
            return ERR_STATE;
        }
    }
    tls_status.entropy_available =
        (uint8_t)(rng.initialized && rng.rdrand_available);
    if (!tls_status.entropy_available) {
        tls_status.state = TLS_STATE_UNAVAILABLE;
        tls_status.last_reason = TLS_REASON_ENTROPY_UNAVAILABLE;
        tls_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("TLS", "RDRAND nao atende a politica de entropia HTTPS");
        return ERR_UNAVAILABLE;
    }
    result = tls_client_init();
    if (result != OK || tls_client_get_status(&client) != OK ||
        !client.initialized) {
        tls_status.state = TLS_STATE_UNAVAILABLE;
        tls_status.last_reason = client.reason == TLS_REASON_NONE ?
                                  TLS_REASON_HANDSHAKE : client.reason;
        tls_status.last_error = result == OK ? ERR_UNAVAILABLE : result;
        LOG_ERROR("TLS", "Adaptador BearSSL nao ficou disponivel");
        return tls_status.last_error;
    }
    tls_status.handshake_available = 1U;
    tls_status.x509_available = 1U;
    tls_status.certificate_validation_available = 1U;
    tls_status.state = TLS_STATE_READY;
    tls_status.last_reason = TLS_REASON_NONE;
    tls_status.last_error = OK;
    LOG_INFO("TLS", "BearSSL TLS 1.2 e validacao X.509 prontos");
    return OK;
}

int tls_get_policy(tls_policy_t* out_policy) {
    if (!out_policy) {
        LOG_ERROR("TLS", "Destino nulo ao consultar politica");
        return ERR_NULL;
    }
    if (!tls_status.initialized || !tls_status.policy_ready) {
        LOG_ERROR("TLS", "Politica TLS antes da inicializacao");
        return ERR_STATE;
    }
    *out_policy = tls_policy;
    return OK;
}

int tls_policy_validate(const tls_peer_identity_t* identity,
                        tls_reason_t* out_reason) {
    uint64_t now;
    int result;

    if (!identity || !out_reason) {
        LOG_ERROR("TLS", "Argumento nulo na validacao de identidade");
        return ERR_NULL;
    }
    *out_reason = TLS_REASON_NONE;
    if (!tls_status.initialized || !tls_status.policy_ready) {
        LOG_ERROR("TLS", "Validacao TLS antes da inicializacao");
        return tls_reject(TLS_REASON_NOT_INITIALIZED, ERR_STATE,
                          "Politica TLS nao inicializada", out_reason);
    }
    tls_status.policy_checks++;
    result = clock_get_utc(&now);
    result = tls_policy_validate_at(identity, (uint8_t)(result == OK), now,
                                    out_reason, 1U);
    if (result != OK) return result;
    tls_status.last_reason = TLS_REASON_NONE;
    tls_status.last_error = OK;
    return OK;
}

int tls_get_status(tls_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("TLS", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    *out_status = tls_status;
    return OK;
}

int tls_capability_available(void) {
    return tls_status.initialized && tls_status.policy_ready &&
           tls_status.handshake_available &&
           tls_status.x509_available &&
           tls_status.certificate_validation_available &&
           tls_status.entropy_available &&
           tls_status.trusted_time_available &&
           tls_status.state == TLS_STATE_READY;
}

int tls_validate_state(void) {
    if (!tls_status.initialized || !tls_status.policy_ready ||
        tls_policy.minimum_version < TLS_VERSION_1_2 ||
        !tls_policy.require_static_ca ||
        !tls_policy.require_hostname_san ||
        !tls_policy.require_validity_window ||
        tls_policy.allow_http_fallback ||
        !tls_policy.trust_current_version ||
        tls_policy.trust_next_version < tls_policy.trust_current_version ||
        tls_policy.trust_revocation_version > tls_policy.trust_next_version ||
        (!tls_status.handshake_available && tls_status.x509_available) ||
        !tls_status.static_ca_required ||
        !tls_status.spki_pinning_optional ||
        !tls_status.http_fallback_forbidden) {
        LOG_ERROR("TLS", "Estado ou politica TLS invalido");
        return ERR_STATE;
    }
    if (tls_status.state != TLS_STATE_POLICY_ONLY &&
        tls_status.state != TLS_STATE_UNAVAILABLE &&
        tls_status.state != TLS_STATE_READY) {
        LOG_ERROR("TLS", "Estado TLS desconhecido");
        return ERR_STATE;
    }
    if (!tls_status.trusted_time_available &&
        tls_status.state != TLS_STATE_UNAVAILABLE) {
        LOG_ERROR("TLS", "TLS aceitou tempo UTC indisponivel");
        return ERR_STATE;
    }
    if (tls_status.state == TLS_STATE_READY) {
        if (!tls_status.handshake_available || !tls_status.x509_available ||
            !tls_status.certificate_validation_available ||
            !tls_status.entropy_available ||
            rng_validate_state() != OK ||
            tls_client_validate_state() != OK) {
            LOG_ERROR("TLS", "TLS pronto sem capacidades obrigatorias");
            return ERR_STATE;
        }
    }
    if (tls_status.state == TLS_STATE_POLICY_ONLY &&
        (tls_status.handshake_available || tls_status.x509_available)) {
        LOG_ERROR("TLS", "Politica TLS publicou capacidades indevidas");
        return ERR_STATE;
    }
    return OK;
}

const char* tls_state_name(tls_state_t state) {
    if (state == TLS_STATE_POLICY_ONLY) return "POLICY_ONLY";
    if (state == TLS_STATE_UNAVAILABLE) return "UNAVAILABLE";
    if (state == TLS_STATE_READY) return "READY";
    return "UNINITIALIZED";
}

const char* tls_reason_name(tls_reason_t reason) {
    switch (reason) {
        case TLS_REASON_NONE: return "NONE";
        case TLS_REASON_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case TLS_REASON_TIME_UNAVAILABLE: return "TIME_UNAVAILABLE";
        case TLS_REASON_CERT_NOT_YET_VALID: return "CERT_NOT_YET_VALID";
        case TLS_REASON_CERT_EXPIRED: return "CERT_EXPIRED";
        case TLS_REASON_UNTRUSTED_CHAIN: return "UNTRUSTED_CHAIN";
        case TLS_REASON_TRUST_REVOKED: return "TRUST_REVOKED";
        case TLS_REASON_HOSTNAME_MISMATCH: return "HOSTNAME_MISMATCH";
        case TLS_REASON_PIN_MISMATCH: return "PIN_MISMATCH";
        case TLS_REASON_POLICY: return "POLICY";
        case TLS_REASON_UNSUPPORTED: return "UNSUPPORTED";
        case TLS_REASON_ENTROPY_UNAVAILABLE: return "ENTROPY_UNAVAILABLE";
        case TLS_REASON_HANDSHAKE: return "HANDSHAKE";
        default: return "UNKNOWN";
    }
}

static uint8_t tls_self_test_case(const tls_peer_identity_t* identity,
                                  uint8_t time_available, uint64_t now,
                                  int expected_result,
                                  tls_reason_t expected_reason) {
    tls_reason_t reason = TLS_REASON_NONE;
    int result = tls_policy_validate_at(identity, time_available, now,
                                         &reason, 0U);

    return (uint8_t)(result == expected_result && reason == expected_reason);
}

int tls_self_test(tls_self_test_result_t* out_result) {
    tls_peer_identity_t identity;
    uint64_t now = TLS_SELF_TEST_NOW;

    if (!out_result) {
        LOG_ERROR("TLS", "Destino nulo no autoteste TLS");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    kmemset(&identity, 0, sizeof(identity));
    identity.not_before = now - 1U;
    identity.not_after = now + 1U;
    identity.chain_trusted = 1U;
    identity.hostname_san_match = 1U;

    out_result->valid_identity = tls_self_test_case(
        &identity, 1U, now, OK, TLS_REASON_NONE);
    out_result->time_unavailable = tls_self_test_case(
        &identity, 0U, now, ERR_UNAVAILABLE, TLS_REASON_TIME_UNAVAILABLE);
    identity.not_before = now + 1U;
    out_result->certificate_future = tls_self_test_case(
        &identity, 1U, now, ERR_INVALID, TLS_REASON_CERT_NOT_YET_VALID);
    identity.not_before = now - 1U;
    identity.not_after = now - 1U;
    out_result->certificate_expired = tls_self_test_case(
        &identity, 1U, now, ERR_INVALID, TLS_REASON_CERT_EXPIRED);
    identity.not_after = now + 1U;
    identity.chain_trusted = 0U;
    out_result->untrusted_chain = tls_self_test_case(
        &identity, 1U, now, ERR_INVALID, TLS_REASON_UNTRUSTED_CHAIN);
    identity.chain_trusted = 1U;
    identity.hostname_san_match = 0U;
    out_result->san_mismatch = tls_self_test_case(
        &identity, 1U, now, ERR_INVALID, TLS_REASON_HOSTNAME_MISMATCH);
    identity.hostname_san_match = 1U;
    identity.pin_configured = 0U;
    identity.trust_version = tls_policy.trust_current_version;
    out_result->trust_rotation = tls_self_test_case(
        &identity, 1U, now, OK, TLS_REASON_NONE);
    identity.trust_version = tls_policy.trust_next_version;
    out_result->trust_rotation = (uint8_t)(out_result->trust_rotation &&
        tls_self_test_case(&identity, 1U, now, OK, TLS_REASON_NONE));
    identity.trust_version = 0U;
    identity.trust_revoked = 1U;
    out_result->trust_revocation = tls_self_test_case(
        &identity, 1U, now, ERR_INVALID, TLS_REASON_TRUST_REVOKED);
    identity.trust_revoked = 0U;
    out_result->pin_absent = tls_self_test_case(
        &identity, 1U, now, OK, TLS_REASON_NONE);
    identity.pin_configured = 1U;
    identity.pin_match = 1U;
    out_result->pin_match = tls_self_test_case(
        &identity, 1U, now, OK, TLS_REASON_NONE);
    identity.pin_match = 0U;
    out_result->pin_mismatch = tls_self_test_case(
        &identity, 1U, now, ERR_INVALID, TLS_REASON_PIN_MISMATCH);
    out_result->invariants = (uint8_t)(tls_validate_state() == OK);
    out_result->passed = (uint8_t)(out_result->valid_identity +
        out_result->time_unavailable + out_result->certificate_future +
        out_result->certificate_expired + out_result->untrusted_chain +
        out_result->san_mismatch + out_result->pin_absent +
        out_result->pin_match + out_result->pin_mismatch +
        out_result->trust_rotation + out_result->trust_revocation +
        out_result->invariants);
    out_result->failed = (uint8_t)(12U - out_result->passed);
    if (out_result->failed) {
        LOG_ERROR("TLS", "Autoteste da politica TLS falhou");
        return ERR_STATE;
    }
    LOG_INFO("TLS", "Autoteste da politica TLS concluido");
    return OK;
}
