#include <stdio.h>

#include <bearssl.h>

#include "core/clock.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/tls_client.h"
#include "drivers/rng.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define TLS_NOW 1700000000ULL
#define TLS_SOCKET 7U

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
    printf("ZCOV_BEGIN|case=host:security:tls-client|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:security:tls-client|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:security:tls-client|value=0x%08X\n",
           (uint32_t)result);
}

static clock_status_t fake_clock;
static rng_status_t fake_rng;
static int fake_clock_status_result;
static int fake_clock_utc_result;
static int fake_rng_status_result;
static int fake_rng_bytes_result;
static int fake_socket_send_result;
static int fake_socket_receive_result;
static uint8_t fake_receive_eof;
static int fake_reset_result;
static int fake_reset_error;

static void reset_fakes(void) {
    kmemset(&fake_clock, 0, sizeof(fake_clock));
    fake_clock.initialized = 1U;
    fake_clock.utc_available = 1U;
    fake_clock.monotonic_available = 1U;
    fake_clock.source = CLOCK_SOURCE_RTC;
    fake_clock.frequency = 1000U;
    fake_clock_status_result = OK;
    fake_clock_utc_result = OK;
    kmemset(&fake_rng, 0, sizeof(fake_rng));
    fake_rng.initialized = 1U;
    fake_rng.cpuid_available = 1U;
    fake_rng.rdrand_available = 1U;
    fake_rng_status_result = OK;
    fake_rng_bytes_result = OK;
    fake_socket_send_result = OK;
    fake_socket_receive_result = OK;
    fake_receive_eof = 0U;
    fake_reset_result = 1;
    fake_reset_error = 0;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

int clock_get_status(clock_status_t* output) {
    if (!output) return ERR_NULL;
    if (fake_clock_status_result != OK) return fake_clock_status_result;
    *output = fake_clock;
    return OK;
}

int clock_get_utc(uint64_t* output) {
    if (!output) return ERR_NULL;
    if (fake_clock_utc_result != OK) return fake_clock_utc_result;
    *output = TLS_NOW;
    return OK;
}

int rng_get_status(rng_status_t* output) {
    if (!output) return ERR_NULL;
    if (fake_rng_status_result != OK) return fake_rng_status_result;
    *output = fake_rng;
    return OK;
}

int rng_get_bytes(uint8_t* output, uint32_t length) {
    if (!output && length) return ERR_NULL;
    if (fake_rng_bytes_result != OK) return fake_rng_bytes_result;
    for (uint32_t index = 0U; index < length; index++) {
        output[index] = (uint8_t)(index + 1U);
    }
    return OK;
}

int net_socket_send(net_socket_handle_t handle, const uint8_t* data,
                    uint16_t length, uint16_t* output) {
    (void)handle;
    (void)data;
    if (!output) return ERR_NULL;
    *output = 0U;
    if (fake_socket_send_result != OK) return fake_socket_send_result;
    *output = length;
    return OK;
}

int net_socket_receive(net_socket_handle_t handle, uint8_t* buffer,
                       uint16_t capacity, uint16_t* output, uint8_t* eof) {
    (void)handle;
    if (!buffer || !output || !eof) return ERR_NULL;
    *output = 0U;
    *eof = 0U;
    if (fake_socket_receive_result != OK) return fake_socket_receive_result;
    if (fake_receive_eof) {
        *eof = 1U;
        return OK;
    }
    if (capacity) {
        buffer[0] = 0x42U;
        *output = 1U;
    }
    return OK;
}

const br_hash_class br_sha1_vtable = {0U};
const br_hash_class br_sha256_vtable = {0U};
const br_hash_class br_sha384_vtable = {0U};
const br_hash_class br_sha512_vtable = {0U};
const br_prf_class br_tls12_sha256_prf = {0U};
const br_prf_class br_tls12_sha384_prf = {0U};

void br_ssl_client_zero(br_ssl_client_context* context) {
    kmemset(context, 0U, sizeof(*context));
}

void br_ssl_engine_set_versions(br_ssl_engine_context* engine,
                                uint16_t minimum, uint16_t maximum) {
    (void)engine;
    (void)minimum;
    (void)maximum;
}

void br_x509_minimal_init(br_x509_minimal_context* context,
                          const br_hash_class* digest,
                          const br_x509_trust_anchor* anchors,
                          size_t anchor_count) {
    (void)digest;
    (void)anchors;
    (void)anchor_count;
    context->vtable.marker = 1U;
}

void br_x509_minimal_set_hash(br_x509_minimal_context* context, int id,
                              const br_hash_class* digest) {
    (void)context;
    (void)id;
    (void)digest;
}

void br_x509_minimal_set_time(br_x509_minimal_context* context,
                              uint32_t days, uint32_t seconds) {
    (void)context;
    (void)days;
    (void)seconds;
}

void br_ssl_engine_set_suites(br_ssl_engine_context* engine,
                              const uint16_t* suites, size_t suite_count) {
    (void)engine;
    (void)suites;
    (void)suite_count;
}

void br_ssl_engine_set_hash(br_ssl_engine_context* engine, int id,
                            const br_hash_class* digest) {
    (void)engine;
    (void)id;
    (void)digest;
}

void br_ssl_client_set_default_rsapub(br_ssl_client_context* context) {
    (void)context;
}

void br_ssl_engine_set_default_rsavrfy(br_ssl_engine_context* engine) {
    (void)engine;
}

void br_ssl_engine_set_default_ec(br_ssl_engine_context* engine) {
    (void)engine;
}

void br_ssl_engine_set_default_ecdsa(br_ssl_engine_context* engine) {
    (void)engine;
}

const void* br_ssl_engine_get_rsavrfy(br_ssl_engine_context* engine) {
    return engine;
}

const void* br_ssl_engine_get_ec(br_ssl_engine_context* engine) {
    return engine;
}

const void* br_ssl_engine_get_ecdsa(br_ssl_engine_context* engine) {
    return engine;
}

void br_x509_minimal_set_rsa(br_x509_minimal_context* context,
                             const void* implementation) {
    (void)context;
    (void)implementation;
}

void br_x509_minimal_set_ecdsa(br_x509_minimal_context* context,
                               const void* ec_implementation,
                               const void* ecdsa_implementation) {
    (void)context;
    (void)ec_implementation;
    (void)ecdsa_implementation;
}

void br_ssl_engine_set_default_aes_gcm(br_ssl_engine_context* engine) {
    (void)engine;
}

void br_ssl_engine_set_x509(br_ssl_engine_context* engine,
                            const br_x509_class* implementation) {
    (void)engine;
    (void)implementation;
}

void br_ssl_engine_set_prf_sha256(br_ssl_engine_context* engine,
                                  const br_prf_class* implementation) {
    (void)engine;
    (void)implementation;
}

void br_ssl_engine_set_prf_sha384(br_ssl_engine_context* engine,
                                  const br_prf_class* implementation) {
    (void)engine;
    (void)implementation;
}

void br_ssl_engine_set_buffer(br_ssl_engine_context* engine,
                              unsigned char* buffer, size_t length,
                              size_t min_length) {
    (void)min_length;
    engine->buffer = buffer;
    engine->buffer_length = length;
}

void br_ssl_engine_inject_entropy(br_ssl_engine_context* engine,
                                  const void* entropy, size_t length) {
    (void)engine;
    (void)entropy;
    (void)length;
}

int br_ssl_client_reset(br_ssl_client_context* context,
                        const char* hostname, int resume) {
    (void)hostname;
    (void)resume;
    context->eng.err = fake_reset_error;
    context->eng.state = BR_SSL_SENDREC;
    context->eng.application_mode = 0U;
    context->eng.application_length = 0U;
    if (!fake_reset_result) context->eng.state = BR_SSL_CLOSED;
    return fake_reset_result;
}

unsigned br_ssl_engine_current_state(const br_ssl_engine_context* engine) {
    return engine->state;
}

int br_ssl_engine_last_error(const br_ssl_engine_context* engine) {
    return engine->err;
}

unsigned char* br_ssl_engine_sendrec_buf(const br_ssl_engine_context* engine,
                                          size_t* length) {
    if (engine->state != BR_SSL_SENDREC) {
        *length = 0U;
        return NULL;
    }
    *length = engine->buffer_length > 8U ? 8U : engine->buffer_length;
    return engine->buffer;
}

void br_ssl_engine_sendrec_ack(br_ssl_engine_context* engine, size_t length) {
    (void)length;
    engine->state = BR_SSL_RECVREC;
}

unsigned char* br_ssl_engine_recvrec_buf(const br_ssl_engine_context* engine,
                                          size_t* length) {
    if (engine->state != BR_SSL_RECVREC) {
        *length = 0U;
        return NULL;
    }
    *length = engine->buffer_length > 8U ? 8U : engine->buffer_length;
    return engine->buffer;
}

void br_ssl_engine_recvrec_ack(br_ssl_engine_context* engine, size_t length) {
    (void)length;
    if (engine->application_mode) {
        engine->state = BR_SSL_RECVAPP;
        engine->application_length = 5U;
        engine->application_data[0] = 'r';
        engine->application_data[1] = 'e';
        engine->application_data[2] = 'p';
        engine->application_data[3] = 'l';
        engine->application_data[4] = 'y';
    } else {
        engine->state = BR_SSL_SENDAPP;
    }
}

void br_ssl_engine_get_session_parameters(
    const br_ssl_engine_context* engine, br_ssl_session_parameters* parameters) {
    (void)engine;
    parameters->version = BR_TLS12;
    parameters->cipher_suite = 0xC02FU;
}

void br_ssl_engine_flush(br_ssl_engine_context* engine, int force) {
    (void)force;
    if (engine->state == BR_SSL_SENDAPP) {
        engine->application_mode = 1U;
        engine->state = BR_SSL_RECVREC;
    }
}

unsigned char* br_ssl_engine_sendapp_buf(const br_ssl_engine_context* engine,
                                          size_t* length) {
    if (engine->state != BR_SSL_SENDAPP) {
        *length = 0U;
        return NULL;
    }
    *length = engine->buffer_length > 32U ? 32U : engine->buffer_length;
    return engine->buffer;
}

void br_ssl_engine_sendapp_ack(br_ssl_engine_context* engine, size_t length) {
    (void)engine;
    (void)length;
}

unsigned char* br_ssl_engine_recvapp_buf(const br_ssl_engine_context* engine,
                                          size_t* length) {
    if (engine->state != BR_SSL_RECVAPP) {
        *length = 0U;
        return NULL;
    }
    *length = engine->application_length;
    return (unsigned char*)engine->application_data;
}

void br_ssl_engine_recvapp_ack(br_ssl_engine_context* engine, size_t length) {
    if (length <= engine->application_length) {
        engine->application_length -= length;
    }
}

static int check_initial_state(void) {
    uint8_t data[4] = {0U};
    uint16_t transferred = 0U;
    uint8_t eof = 0U;

    if (tls_client_close() != ERR_STATE ||
        tls_client_get_status(NULL) != ERR_NULL ||
        tls_client_maintain() != ERR_STATE ||
        tls_client_send(NULL, 1U, &transferred) != ERR_NULL ||
        tls_client_receive(data, sizeof(data), NULL, &eof) != ERR_NULL ||
        tls_client_receive(data, sizeof(data), &transferred, NULL) != ERR_NULL) {
        return 1;
    }
    fake_rng.rdrand_available = 0U;
    if (tls_client_init() != ERR_UNAVAILABLE) return 2;
    return 0;
}

static int check_start_guards(void) {
    char long_hostname[TLS_CLIENT_HOST_SIZE + 1U];

    kmemset(long_hostname, 'a', sizeof(long_hostname));
    long_hostname[TLS_CLIENT_HOST_SIZE] = '\0';
    reset_fakes();
    if (tls_client_init() != OK ||
        tls_client_start(0U, "example.test") != ERR_NULL ||
        tls_client_start(TLS_SOCKET, NULL) != ERR_NULL ||
        tls_client_start(TLS_SOCKET, "") != ERR_NULL ||
        tls_client_start(TLS_SOCKET, long_hostname) != ERR_OVERFLOW) {
        return 1;
    }
    fake_clock.utc_available = 0U;
    if (tls_client_start(TLS_SOCKET, "example.test") != ERR_UNAVAILABLE) return 2;
    reset_fakes();
    if (tls_client_init() != OK) return 3;
    fake_rng_bytes_result = ERR_UNAVAILABLE;
    if (tls_client_start(TLS_SOCKET, "example.test") != ERR_UNAVAILABLE) return 4;
    reset_fakes();
    if (tls_client_init() != OK) return 5;
    fake_reset_result = 0;
    fake_reset_error = BR_ERR_NO_RANDOM;
    if (tls_client_start(TLS_SOCKET, "example.test") != ERR_UNAVAILABLE) return 6;
    return 0;
}

static int check_session(void) {
    uint8_t request[] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t response[16] = {0U};
    uint16_t written = 0U;
    uint16_t read = 0U;
    uint8_t eof = 0U;
    tls_client_status_t status;

    reset_fakes();
    if (tls_client_init() != OK ||
        tls_client_start(TLS_SOCKET, "example.test") != OK ||
        tls_client_start(TLS_SOCKET, "other.test") != ERR_STATE ||
        tls_client_validate_state() != OK ||
        tls_client_maintain() != OK || tls_client_maintain() != OK ||
        tls_client_maintain() != OK || tls_client_get_status(&status) != OK ||
        status.state != TLS_CLIENT_STATE_READY || !status.handshake_complete ||
        !status.x509_verified || !status.hostname_verified ||
        status.negotiated_version != BR_TLS12 ||
        tls_client_send(NULL, sizeof(request), &written) != ERR_NULL ||
        tls_client_send(request, sizeof(request), NULL) != ERR_NULL ||
        tls_client_send(request, sizeof(request), &written) != OK ||
        written != sizeof(request) ||
        tls_client_receive(NULL, sizeof(response), &read, &eof) != ERR_NULL ||
        tls_client_receive(response, sizeof(response), &read, &eof) != OK ||
        read != 5U || eof || kstrcmp((char*)response, "reply") != 0 ||
        tls_client_close() != OK || tls_client_validate_state() != OK ||
        tls_client_send(request, sizeof(request), &written) != ERR_STATE) {
        return 1;
    }
    return 0;
}

static int check_failures(void) {
    uint8_t response[4] = {0U};
    uint16_t read = 0U;
    uint8_t eof = 0U;

    reset_fakes();
    if (tls_client_init() != OK) return 1;
    fake_reset_result = 0;
    fake_reset_error = BR_ERR_X509_EXPIRED;
    if (tls_client_start(TLS_SOCKET, "expired.test") != ERR_INVALID) return 2;
    reset_fakes();
    if (tls_client_init() != OK ||
        tls_client_start(TLS_SOCKET, "io.test") != OK ||
        tls_client_maintain() != OK) return 3;
    fake_socket_receive_result = ERR_UNAVAILABLE;
    if (tls_client_maintain() != ERR_INVALID) return 4;
    if (tls_client_close() != OK) return 5;
    reset_fakes();
    if (tls_client_init() != OK ||
        tls_client_start(TLS_SOCKET, "eof.test") != OK ||
        tls_client_maintain() != OK) return 6;
    fake_receive_eof = 1U;
    if (tls_client_maintain() != ERR_INVALID) return 7;
    if (tls_client_close() != OK) return 8;
    reset_fakes();
    if (tls_client_init() != OK ||
        tls_client_start(TLS_SOCKET, "closed.test") != OK ||
        tls_client_maintain() != OK || tls_client_maintain() != OK ||
        tls_client_maintain() != OK || tls_client_receive(response,
                                                           sizeof(response),
                                                           &read, &eof) != OK) {
        return 9;
    }
    return 0;
}

int main(void) {
    int result;

    reset_fakes();
    coverage_active = 1U;
    result = check_initial_state();
    if (result == 0) result = check_start_guards();
    if (result == 0) result = check_session();
    if (result == 0) result = check_failures();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
