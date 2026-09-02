#include <stdint.h>
#include <stdio.h>

#include "core/clock.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/tls.h"
#include "core/tls_client.h"
#include "core/video.h"
#include "drivers/rng.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define TLS_NOW 1700000000ULL

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:security:tls|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:security:tls|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:security:tls|value=0x%08X\n",
           (uint32_t)result);
}

static int clock_status_result;
static int clock_utc_result;
static clock_status_t fake_clock;
static int rng_status_result;
static int rng_init_result;
static rng_status_t fake_rng;
static int client_init_result;
static tls_client_status_t fake_client;
static int client_validate_result;

uint32_t timer_get_ticks(void) {
    return 1U;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

int clock_get_status(clock_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (clock_status_result != OK) return clock_status_result;
    *out_status = fake_clock;
    return OK;
}

int clock_get_utc(uint64_t* out_unix_seconds) {
    if (!out_unix_seconds) return ERR_NULL;
    if (clock_utc_result != OK) return clock_utc_result;
    *out_unix_seconds = TLS_NOW;
    return OK;
}

int rng_get_status(rng_status_t* output) {
    if (!output) return ERR_NULL;
    if (rng_status_result != OK) return rng_status_result;
    *output = fake_rng;
    return OK;
}

int rng_init(void) {
    if (rng_init_result == OK) {
        fake_rng.initialized = 1U;
        fake_rng.cpuid_available = 1U;
        fake_rng.rdrand_available = 1U;
        fake_rng.last_error = OK;
    }
    return rng_init_result;
}

int rng_validate_state(void) {
    return fake_rng.initialized && fake_rng.rdrand_available ? OK : ERR_STATE;
}

int tls_client_init(void) {
    if (client_init_result == OK) {
        fake_client.initialized = 1U;
        fake_client.state = TLS_CLIENT_STATE_IDLE;
        fake_client.reason = TLS_REASON_NONE;
        fake_client.last_error = OK;
    }
    return client_init_result;
}

int tls_client_get_status(tls_client_status_t* output) {
    if (!output) return ERR_NULL;
    *output = fake_client;
    return OK;
}

int tls_client_validate_state(void) {
    return client_validate_result;
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static void reset_fakes(void) {
    kmemset(&fake_clock, 0, sizeof(fake_clock));
    fake_clock.initialized = 1U;
    fake_clock.utc_available = 1U;
    fake_clock.monotonic_available = 1U;
    fake_clock.source = CLOCK_SOURCE_RTC;
    fake_clock.frequency = 1000U;
    clock_status_result = OK;
    clock_utc_result = OK;
    kmemset(&fake_rng, 0, sizeof(fake_rng));
    rng_status_result = OK;
    rng_init_result = OK;
    kmemset(&fake_client, 0, sizeof(fake_client));
    client_init_result = OK;
    client_validate_result = OK;
}

static tls_peer_identity_t valid_identity(void) {
    tls_peer_identity_t identity;

    kmemset(&identity, 0, sizeof(identity));
    identity.not_before = TLS_NOW - 10U;
    identity.not_after = TLS_NOW + 10U;
    identity.chain_trusted = 1U;
    identity.hostname_san_match = 1U;
    identity.trust_version = 1U;
    return identity;
}

static int check_tls(void) {
    tls_policy_t policy;
    tls_status_t status;
    tls_reason_t reason;
    tls_self_test_result_t self_test;
    tls_peer_identity_t identity;

    reset_fakes();
    if (tls_get_policy(&policy) != ERR_STATE ||
        tls_policy_validate(&identity, &reason) != ERR_STATE ||
        reason != TLS_REASON_NOT_INITIALIZED ||
        tls_policy_validate(NULL, &reason) != ERR_NULL ||
        tls_policy_validate(&identity, NULL) != ERR_NULL ||
        tls_get_status(NULL) != ERR_NULL ||
        tls_self_test(NULL) != ERR_NULL || tls_capability_available() ||
        tls_validate_state() != ERR_STATE ||
        kstrcmp(tls_state_name(TLS_STATE_UNINITIALIZED), "UNINITIALIZED") != 0 ||
        kstrcmp(tls_state_name(TLS_STATE_POLICY_ONLY), "POLICY_ONLY") != 0 ||
        kstrcmp(tls_state_name(TLS_STATE_UNAVAILABLE), "UNAVAILABLE") != 0 ||
        kstrcmp(tls_state_name(TLS_STATE_READY), "READY") != 0 ||
        kstrcmp(tls_state_name((tls_state_t)99), "UNINITIALIZED") != 0) return 1;
    if (kstrcmp(tls_reason_name(TLS_REASON_NONE), "NONE") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_NOT_INITIALIZED),
                "NOT_INITIALIZED") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_TIME_UNAVAILABLE),
                "TIME_UNAVAILABLE") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_CERT_NOT_YET_VALID),
                "CERT_NOT_YET_VALID") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_CERT_EXPIRED), "CERT_EXPIRED") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_UNTRUSTED_CHAIN),
                "UNTRUSTED_CHAIN") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_TRUST_REVOKED), "TRUST_REVOKED") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_HOSTNAME_MISMATCH),
                "HOSTNAME_MISMATCH") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_PIN_MISMATCH), "PIN_MISMATCH") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_POLICY), "POLICY") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_UNSUPPORTED), "UNSUPPORTED") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_ENTROPY_UNAVAILABLE),
                "ENTROPY_UNAVAILABLE") != 0 ||
        kstrcmp(tls_reason_name(TLS_REASON_HANDSHAKE), "HANDSHAKE") != 0 ||
        kstrcmp(tls_reason_name((tls_reason_t)99), "UNKNOWN") != 0) return 2;
    reset_fakes();
    fake_clock.utc_available = 0U;
    if (tls_init() != ERR_UNAVAILABLE || tls_get_status(&status) != OK ||
        status.state != TLS_STATE_UNAVAILABLE ||
        status.last_reason != TLS_REASON_TIME_UNAVAILABLE ||
        tls_get_policy(&policy) != OK || policy.minimum_version != TLS_VERSION_1_2 ||
        tls_capability_available() || tls_validate_state() != OK) return 3;
    reset_fakes();
    clock_status_result = ERR_STATE;
    if (tls_init() != ERR_STATE || tls_get_status(&status) != OK ||
        status.last_error != ERR_STATE) return 4;
    reset_fakes();
    fake_rng.initialized = 1U;
    fake_rng.rdrand_available = 0U;
    if (tls_init() != ERR_UNAVAILABLE || tls_get_status(&status) != OK ||
        status.last_reason != TLS_REASON_ENTROPY_UNAVAILABLE) return 5;
    reset_fakes();
    rng_status_result = ERR_STATE;
    rng_init_result = ERR_UNAVAILABLE;
    if (tls_init() != ERR_UNAVAILABLE || tls_get_status(&status) != OK ||
        status.last_reason != TLS_REASON_ENTROPY_UNAVAILABLE) return 6;
    reset_fakes();
    client_init_result = ERR_UNAVAILABLE;
    if (tls_init() != ERR_UNAVAILABLE || tls_get_status(&status) != OK ||
        status.last_reason != TLS_REASON_HANDSHAKE) return 7;
    reset_fakes();
    if (tls_init() != OK || tls_get_status(&status) != OK ||
        status.state != TLS_STATE_READY || !status.handshake_available ||
        !status.x509_available || !status.entropy_available ||
        !status.trusted_time_available || !tls_capability_available() ||
        tls_validate_state() != OK || tls_get_policy(&policy) != OK ||
        policy.require_static_ca != 1U || policy.require_hostname_san != 1U ||
        policy.require_validity_window != 1U || policy.allow_http_fallback != 0U) {
        return 8;
    }
    identity = valid_identity();
    if (tls_policy_validate(&identity, &reason) != OK ||
        reason != TLS_REASON_NONE) return 9;
    identity.chain_trusted = 0U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_UNTRUSTED_CHAIN) return 10;
    identity = valid_identity();
    identity.hostname_san_match = 0U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_HOSTNAME_MISMATCH) return 11;
    identity = valid_identity();
    identity.not_before = TLS_NOW + 1U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_CERT_NOT_YET_VALID) return 12;
    identity = valid_identity();
    identity.not_after = TLS_NOW - 1U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_CERT_EXPIRED) return 13;
    identity = valid_identity();
    clock_utc_result = ERR_UNAVAILABLE;
    if (tls_policy_validate(&identity, &reason) != ERR_UNAVAILABLE ||
        reason != TLS_REASON_TIME_UNAVAILABLE) return 14;
    clock_utc_result = OK;
    identity = valid_identity();
    identity.trust_revoked = 1U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_TRUST_REVOKED) return 15;
    identity = valid_identity();
    identity.trust_version = 99U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_POLICY) return 16;
    identity = valid_identity();
    identity.pin_configured = 1U;
    identity.pin_match = 0U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_PIN_MISMATCH) return 17;
    identity.pin_match = 1U;
    if (tls_policy_validate(&identity, &reason) != OK ||
        reason != TLS_REASON_NONE) return 18;
    identity = valid_identity();
    identity.not_after = identity.not_before - 1U;
    if (tls_policy_validate(&identity, &reason) != ERR_INVALID ||
        reason != TLS_REASON_POLICY) return 19;
    if (tls_self_test(&self_test) != OK || self_test.passed != 12U ||
        self_test.failed != 0U || !self_test.valid_identity ||
        !self_test.time_unavailable || !self_test.certificate_future ||
        !self_test.certificate_expired || !self_test.untrusted_chain ||
        !self_test.san_mismatch || !self_test.pin_absent ||
        !self_test.pin_match || !self_test.pin_mismatch ||
        !self_test.trust_rotation || !self_test.trust_revocation ||
        !self_test.invariants) return 20;
    client_validate_result = ERR_STATE;
    if (tls_validate_state() != ERR_STATE) return 21;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_tls();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
