#include <bearssl.h>

#include "core/tls_client.h"
#include "core/clock.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/rng.h"

#define TLS_CLIENT_IO_SIZE BR_SSL_BUFSIZE_MONO
#define TLS_CLIENT_ENTROPY_SIZE 32U
#define TLS_CLIENT_UNIX_EPOCH_DAYS 719528U
#define TLS_CLIENT_SECONDS_PER_DAY 86400U
#define TLS_CLIENT_APPLICATION_ATTEMPTS 8U

static const unsigned char tls_sectigo_e36_dn[] = {
    0x30, 0x60, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x47, 0x42, 0x31, 0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x0F, 0x53, 0x65, 0x63, 0x74, 0x69, 0x67, 0x6F, 0x20, 0x4C, 0x69,
    0x6D, 0x69, 0x74, 0x65, 0x64, 0x31, 0x37, 0x30, 0x35, 0x06, 0x03, 0x55,
    0x04, 0x03, 0x13, 0x2E, 0x53, 0x65, 0x63, 0x74, 0x69, 0x67, 0x6F, 0x20,
    0x50, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65,
    0x72, 0x20, 0x41, 0x75, 0x74, 0x68, 0x65, 0x6E, 0x74, 0x69, 0x63, 0x61,
    0x74, 0x69, 0x6F, 0x6E, 0x20, 0x43, 0x41, 0x20, 0x44, 0x56, 0x20, 0x45,
    0x33, 0x36
};

static const unsigned char tls_sectigo_e36_key[] = {
    0x04, 0x68, 0xA1, 0xA7, 0x6C, 0x05, 0x27, 0x05, 0x89, 0x63, 0x1C, 0x39,
    0xA7, 0xFF, 0x25, 0x21, 0xC5, 0xED, 0xD3, 0x2F, 0x12, 0x98, 0xBB, 0x2C,
    0xDC, 0xF5, 0x55, 0xE8, 0x49, 0xA0, 0x84, 0x57, 0x91, 0x7B, 0xDC, 0x58,
    0x5F, 0x6B, 0xF4, 0xA2, 0xFD, 0x13, 0x2C, 0x9B, 0x04, 0xC5, 0x5B, 0x74,
    0x7D, 0xB3, 0xB2, 0x7A, 0x26, 0x96, 0x19, 0x16, 0x6B, 0xB8, 0x76, 0x8D,
    0xEF, 0x93, 0xF5, 0x60, 0x97
};

static const unsigned char tls_letsencrypt_yr1_dn[] = {
    0x30, 0x33, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55,
    0x04, 0x0A, 0x13, 0x0D, 0x4C, 0x65, 0x74, 0x27, 0x73, 0x20, 0x45,
    0x6E, 0x63, 0x72, 0x79, 0x70, 0x74, 0x31, 0x0C, 0x30, 0x0A, 0x06,
    0x03, 0x55, 0x04, 0x03, 0x13, 0x03, 0x59, 0x52, 0x31
};

static const unsigned char tls_letsencrypt_yr1_key[] = {
    0xA1, 0x58, 0xBC, 0x5F, 0x6C, 0x42, 0x62, 0x03, 0x17, 0xBC, 0x9C,
    0x4D, 0x3C, 0xAA, 0x7F, 0xA0, 0x5D, 0x77, 0x50, 0xC8, 0x26, 0x3C,
    0x00, 0x41, 0xD3, 0xB5, 0x42, 0x2C, 0xD0, 0xED, 0xA1, 0x79, 0xAD,
    0xF6, 0x5B, 0x84, 0x64, 0xD2, 0x52, 0x05, 0x5D, 0x74, 0x57, 0x24,
    0xF5, 0x3F, 0x3E, 0x8B, 0x0F, 0xC6, 0x6A, 0xD5, 0xDD, 0xA0, 0x73,
    0x8D, 0xD6, 0x36, 0x5D, 0x40, 0x75, 0x1A, 0xE3, 0xE2, 0xC1, 0x27,
    0x6B, 0x36, 0xCE, 0x8B, 0x3D, 0x27, 0x3E, 0x99, 0x86, 0x06, 0x9D,
    0xA1, 0xBA, 0x2E, 0x3D, 0xA1, 0x9B, 0x43, 0x21, 0x8E, 0xB0, 0x74,
    0xA7, 0x99, 0x02, 0x2E, 0x41, 0x6C, 0xD5, 0x36, 0xD0, 0x21, 0xFA,
    0x45, 0x20, 0x41, 0x30, 0xB4, 0x90, 0xF5, 0xC5, 0xEB, 0x1F, 0xC7,
    0x71, 0x76, 0xFE, 0x0F, 0x00, 0x9E, 0x39, 0xB2, 0xC1, 0x8C, 0x2F,
    0xAF, 0x14, 0x48, 0x4D, 0xC1, 0xB2, 0x30, 0xB6, 0x6B, 0xA7, 0x00,
    0x37, 0xD2, 0xF9, 0x0A, 0x05, 0xD5, 0x21, 0x08, 0xB9, 0xB5, 0xF3,
    0x62, 0x4C, 0xFA, 0x05, 0x65, 0x9D, 0xD2, 0xD0, 0xE9, 0x57, 0xA8,
    0xB9, 0xA6, 0xB5, 0xD5, 0x7D, 0xE6, 0xE7, 0x56, 0x01, 0xBE, 0x5B,
    0xE9, 0xEF, 0xC5, 0xE3, 0xD0, 0xE2, 0x0A, 0xC1, 0xF4, 0x63, 0x4D,
    0x00, 0x83, 0xBD, 0x81, 0x54, 0x86, 0xF5, 0x87, 0x3C, 0xA7, 0x98,
    0xA7, 0xBE, 0x5F, 0x49, 0x84, 0x44, 0x14, 0xED, 0xB1, 0xBD, 0xC1,
    0xC2, 0x6A, 0x55, 0xF6, 0x2A, 0x5E, 0x06, 0x33, 0x0B, 0xA1, 0x1F,
    0xC3, 0xF5, 0xC1, 0xF1, 0x1D, 0x00, 0x96, 0xA2, 0x2E, 0x65, 0x4E,
    0x65, 0xF0, 0x63, 0x4D, 0x79, 0x27, 0x9A, 0xC6, 0x7A, 0x5C, 0xF5,
    0x9D, 0x98, 0xC9, 0xBB, 0x17, 0x36, 0x92, 0x2A, 0x1A, 0x5F, 0x6F,
    0x57, 0x24, 0x19, 0xE1, 0x32, 0x43, 0xF9, 0xDF, 0x5A, 0xD2, 0x74,
    0x6E, 0x6E, 0x33
};

static const unsigned char tls_letsencrypt_yr1_exponent[] = {
    0x01, 0x00, 0x01
};

/* Sectigo Public Server Authentication CA DV E36, DER SHA-256:
 * 873F0BA80E3AC222656DFD04158CC15C2927D42D5D05F01DEE4A47EB43A916DF. */
/* Let's Encrypt YR1, DER SHA-256:
 * 13949634D99CD6FD6AA80BC034FEFACCEB1969FEEF986586713ECDBB05758D3F. */
static const br_x509_trust_anchor tls_trust_anchors[] = {
    {
        { (unsigned char*)tls_sectigo_e36_dn,
          sizeof(tls_sectigo_e36_dn) },
        BR_X509_TA_CA,
        {
            BR_KEYTYPE_EC,
            { .ec = { BR_EC_secp256r1,
                      (unsigned char*)tls_sectigo_e36_key,
                      sizeof(tls_sectigo_e36_key) } }
        }
    },
    {
        { (unsigned char*)tls_letsencrypt_yr1_dn,
          sizeof(tls_letsencrypt_yr1_dn) },
        BR_X509_TA_CA,
        {
            BR_KEYTYPE_RSA,
            { .rsa = { (unsigned char*)tls_letsencrypt_yr1_key,
                       sizeof(tls_letsencrypt_yr1_key),
                       (unsigned char*)tls_letsencrypt_yr1_exponent,
                       sizeof(tls_letsencrypt_yr1_exponent) } }
        }
    }
};

static br_ssl_client_context tls_client_context;
static br_x509_minimal_context tls_x509_context;
static unsigned char tls_client_io[TLS_CLIENT_IO_SIZE];
static unsigned char tls_client_entropy[TLS_CLIENT_ENTROPY_SIZE];
static tls_client_status_t tls_client_status;
static net_socket_handle_t tls_client_socket;

static uint32_t tls_client_divide_word(uint32_t word, uint32_t divisor,
                                       uint32_t* remainder) {
    uint32_t quotient = 0U;

    for (uint32_t bit = 32U; bit > 0U; bit--) {
        uint32_t carry = *remainder >> 31U;
        uint32_t shifted = (*remainder << 1U) |
                           ((word >> (bit - 1U)) & 1U);

        if (carry || shifted >= divisor) {
            *remainder = shifted - divisor;
            quotient |= 1U << (bit - 1U);
        } else {
            *remainder = shifted;
        }
    }
    return quotient;
}

static uint64_t tls_client_divide_u64(uint64_t value, uint32_t divisor,
                                      uint32_t* remainder) {
    uint32_t high = (uint32_t)(value >> 32U);
    uint32_t low = (uint32_t)value;
    uint32_t quotient_high;
    uint32_t quotient_low;

    *remainder = 0U;
    quotient_high = tls_client_divide_word(high, divisor, remainder);
    quotient_low = tls_client_divide_word(low, divisor, remainder);
    return ((uint64_t)quotient_high << 32U) | quotient_low;
}

static tls_reason_t tls_client_reason_for_error(int error) {
    if (error == BR_ERR_X509_TIME_UNKNOWN) return TLS_REASON_TIME_UNAVAILABLE;
    if (error == BR_ERR_X509_EXPIRED) return TLS_REASON_CERT_EXPIRED;
    if (error == BR_ERR_X509_BAD_SERVER_NAME) {
        return TLS_REASON_HOSTNAME_MISMATCH;
    }
    if (error == BR_ERR_X509_NOT_TRUSTED ||
        (error >= BR_ERR_X509_INVALID_VALUE &&
         error <= BR_ERR_X509_WEAK_PUBLIC_KEY)) {
        return TLS_REASON_UNTRUSTED_CHAIN;
    }
    if (error == BR_ERR_NO_RANDOM) return TLS_REASON_ENTROPY_UNAVAILABLE;
    return TLS_REASON_HANDSHAKE;
}

static int tls_client_fail(int error) {
    tls_client_status.state = TLS_CLIENT_STATE_FAILED;
    tls_client_status.reason = tls_client_reason_for_error(error);
    tls_client_status.bearssl_error = (uint16_t)error;
    tls_client_status.last_error = error == BR_ERR_NO_RANDOM ?
                                   ERR_UNAVAILABLE : ERR_INVALID;
    LOG_ERROR_CODE("TLS", error,
                   "Handshake TLS falhou; canal HTTPS encerrado");
    return tls_client_status.last_error;
}

static int tls_client_configure(const char* hostname, uint64_t now) {
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    };
    uint32_t seconds;
    uint32_t days = (uint32_t)tls_client_divide_u64(
                        now, TLS_CLIENT_SECONDS_PER_DAY,
                        &seconds) + TLS_CLIENT_UNIX_EPOCH_DAYS;
    int result;

    br_ssl_client_zero(&tls_client_context);
    br_ssl_engine_set_versions(&tls_client_context.eng,
                               BR_TLS12, BR_TLS12);
    br_x509_minimal_init(
        &tls_x509_context, &br_sha256_vtable,
        tls_trust_anchors, sizeof(tls_trust_anchors) /
                           sizeof(tls_trust_anchors[0]));
    br_x509_minimal_set_hash(
        &tls_x509_context, br_sha1_ID, &br_sha1_vtable);
    br_x509_minimal_set_hash(
        &tls_x509_context, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(
        &tls_x509_context, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(
        &tls_x509_context, br_sha512_ID, &br_sha512_vtable);
    br_x509_minimal_set_time(&tls_x509_context, days, seconds);
    br_ssl_engine_set_suites(
        &tls_client_context.eng, suites, sizeof(suites) / sizeof(suites[0]));
    br_ssl_engine_set_hash(
        &tls_client_context.eng, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(
        &tls_client_context.eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(
        &tls_client_context.eng, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_hash(
        &tls_client_context.eng, br_sha512_ID, &br_sha512_vtable);
    br_ssl_client_set_default_rsapub(&tls_client_context);
    br_ssl_engine_set_default_rsavrfy(&tls_client_context.eng);
    /* As suites permitidas usam ECDHE e exigem curvas no engine. */
    br_ssl_engine_set_default_ec(&tls_client_context.eng);
    br_ssl_engine_set_default_ecdsa(&tls_client_context.eng);
    br_x509_minimal_set_rsa(
        &tls_x509_context,
        br_ssl_engine_get_rsavrfy(&tls_client_context.eng));
    br_x509_minimal_set_ecdsa(
        &tls_x509_context,
        br_ssl_engine_get_ec(&tls_client_context.eng),
        br_ssl_engine_get_ecdsa(&tls_client_context.eng));
    br_ssl_engine_set_default_aes_gcm(&tls_client_context.eng);
    br_ssl_engine_set_x509(&tls_client_context.eng, &tls_x509_context.vtable);
    br_ssl_engine_set_prf_sha256(
        &tls_client_context.eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(
        &tls_client_context.eng, &br_tls12_sha384_prf);
    br_ssl_engine_set_buffer(
        &tls_client_context.eng, tls_client_io, sizeof(tls_client_io), 0);
    result = rng_get_bytes(tls_client_entropy, sizeof(tls_client_entropy));
    if (result != OK) return result;
    br_ssl_engine_inject_entropy(
        &tls_client_context.eng, tls_client_entropy,
        sizeof(tls_client_entropy));
    if (!br_ssl_client_reset(&tls_client_context, hostname, 0)) {
        return tls_client_fail(
            br_ssl_engine_last_error(&tls_client_context.eng));
    }
    return OK;
}

int tls_client_init(void) {
    rng_status_t rng;

    LOG_INFO("TLS", "Inicializando adaptador BearSSL TLS 1.2");
    kmemset(&tls_client_status, 0, sizeof(tls_client_status));
    tls_client_socket = 0;
    if (rng_get_status(&rng) != OK || !rng.initialized ||
        !rng.rdrand_available) {
        tls_client_status.reason = TLS_REASON_ENTROPY_UNAVAILABLE;
        tls_client_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("TLS", "RDRAND indisponivel para o adaptador BearSSL");
        return ERR_UNAVAILABLE;
    }
    tls_client_status.initialized = 1U;
    tls_client_status.state = TLS_CLIENT_STATE_IDLE;
    tls_client_status.reason = TLS_REASON_NONE;
    tls_client_status.last_error = OK;
    LOG_INFO("TLS", "Adaptador BearSSL inicializado com sucesso");
    return OK;
}

int tls_client_start(net_socket_handle_t socket, const char* hostname) {
    clock_status_t clock;
    uint64_t now;
    int result;

    if (!socket || !hostname || !*hostname) {
        LOG_ERROR("TLS", "Socket ou SNI nulo no inicio TLS");
        return ERR_NULL;
    }
    if (!tls_client_status.initialized) {
        LOG_ERROR("TLS", "Inicio TLS antes da inicializacao");
        return ERR_STATE;
    }
    if (kstrlen(hostname) >= TLS_CLIENT_HOST_SIZE) {
        LOG_ERROR("TLS", "SNI excede o limite do cliente TLS");
        return ERR_OVERFLOW;
    }
    if (tls_client_status.active) {
        LOG_ERROR("TLS", "Ja existe uma sessao TLS ativa");
        return ERR_STATE;
    }
    if (clock_get_status(&clock) != OK || !clock.utc_available ||
        clock_get_utc(&now) != OK) {
        tls_client_status.reason = TLS_REASON_TIME_UNAVAILABLE;
        tls_client_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("TLS", "Tempo UTC indisponivel para X.509");
        return ERR_UNAVAILABLE;
    }
    result = tls_client_configure(hostname, now);
    if (result != OK) {
        tls_client_status.state = TLS_CLIENT_STATE_FAILED;
        if (tls_client_status.reason == TLS_REASON_NONE) {
            tls_client_status.reason = result == ERR_UNAVAILABLE ?
                                        TLS_REASON_ENTROPY_UNAVAILABLE :
                                        TLS_REASON_HANDSHAKE;
        }
        tls_client_status.last_error = result;
        LOG_ERROR("TLS", "Nao foi possivel preparar a sessao BearSSL");
        return result;
    }
    tls_client_status.reason = TLS_REASON_NONE;
    tls_client_status.bearssl_error = 0U;
    tls_client_socket = socket;
    tls_client_status.active = 1U;
    tls_client_status.state = TLS_CLIENT_STATE_HANDSHAKING;
    tls_client_status.handshake_complete = 0U;
    tls_client_status.x509_verified = 0U;
    tls_client_status.hostname_verified = 0U;
    tls_client_status.negotiated_version = 0U;
    tls_client_status.negotiated_suite = 0U;
    tls_client_status.handshake_count++;
    tls_client_status.last_error = OK;
    return OK;
}

int tls_client_maintain(void) {
    br_ssl_engine_context* engine = &tls_client_context.eng;
    unsigned state;

    if (!tls_client_status.active) {
        LOG_ERROR("TLS", "Manutencao TLS sem sessao ativa");
        return ERR_STATE;
    }
    state = br_ssl_engine_current_state(engine);
    if (state & BR_SSL_CLOSED) {
        int error = br_ssl_engine_last_error(engine);

        if (error) return tls_client_fail(error);
        if (!tls_client_status.handshake_complete) {
            return tls_client_fail(BR_ERR_BAD_HANDSHAKE);
        }
        tls_client_status.state = TLS_CLIENT_STATE_READY;
        return OK;
    }
    if (state & BR_SSL_SENDREC) {
        unsigned char* data;
        size_t length;
        uint16_t written = 0;
        uint16_t request;
        int result;

        data = br_ssl_engine_sendrec_buf(engine, &length);
        request = (uint16_t)(length > 2048U ? 2048U : length);
        result = net_socket_send(
            tls_client_socket, data, request, &written);
        if (result != OK) return tls_client_fail(BR_ERR_IO);
        if (written) {
            br_ssl_engine_sendrec_ack(engine, written);
            tls_client_status.bytes_sent += written;
        }
        return OK;
    }
    if (((state & BR_SSL_RECVAPP) || (state & BR_SSL_SENDAPP)) &&
        !tls_client_status.handshake_complete) {
        br_ssl_session_parameters session;

        /* BearSSL may expose SENDAPP together with RECVREC after the
         * peer Finished. Prefer the ready application state once, so HTTP
         * can send its request; subsequent calls must service RECVREC. */
        tls_client_status.handshake_complete = 1U;
        tls_client_status.x509_verified = 1U;
        tls_client_status.hostname_verified = 1U;
        tls_client_status.state = TLS_CLIENT_STATE_READY;
        br_ssl_engine_get_session_parameters(engine, &session);
        if (session.version != BR_TLS12) {
            kmemset(&session, 0, sizeof(session));
            return tls_client_fail(BR_ERR_BAD_VERSION);
        }
        tls_client_status.negotiated_version = session.version;
        tls_client_status.negotiated_suite = session.cipher_suite;
        kmemset(&session, 0, sizeof(session));
        return OK;
    }
    if (state & BR_SSL_RECVREC) {
        unsigned char* data;
        size_t length;
        uint16_t read = 0;
        uint8_t eof = 0;
        uint16_t capacity;
        int result;

        data = br_ssl_engine_recvrec_buf(engine, &length);
        capacity = (uint16_t)(length > NET_SOCKET_RX_CAPACITY ?
                              NET_SOCKET_RX_CAPACITY : length);
        result = net_socket_receive(
            tls_client_socket, data, capacity, &read, &eof);
        if (result != OK) return tls_client_fail(BR_ERR_IO);
        if (read) {
            br_ssl_engine_recvrec_ack(engine, read);
            tls_client_status.bytes_received += read;
        } else if (eof) {
            return tls_client_fail(BR_ERR_IO);
        }
        return OK;
    }
    if ((state & BR_SSL_RECVAPP) || (state & BR_SSL_SENDAPP)) return OK;
    return tls_client_fail(BR_ERR_BAD_STATE);
}

static int tls_client_prepare_application(unsigned wanted_state) {
    br_ssl_engine_context* engine = &tls_client_context.eng;

    for (uint8_t attempt = 0U;
         attempt < TLS_CLIENT_APPLICATION_ATTEMPTS; attempt++) {
        unsigned before = br_ssl_engine_current_state(engine);
        unsigned after;
        int result;

        if (before & wanted_state) return OK;
        if (before & BR_SSL_CLOSED) return ERR_STATE;
        if (wanted_state == BR_SSL_RECVAPP && (before & BR_SSL_SENDAPP)) {
            /* A request may leave plaintext buffered in a shared mono buffer;
             * flush it before asking BearSSL for the server response. */
            br_ssl_engine_flush(engine, 0);
        }
        result = tls_client_maintain();
        if (result != OK) return result;
        after = br_ssl_engine_current_state(engine);
        if (after == before) return OK;
    }
    return OK;
}

int tls_client_send(const uint8_t* data, uint16_t length,
                    uint16_t* out_written) {
    br_ssl_engine_context* engine = &tls_client_context.eng;
    unsigned char* destination;
    size_t capacity;
    int result;

    if (!data || !out_written) {
        LOG_ERROR("TLS", "Buffer nulo no envio TLS");
        return ERR_NULL;
    }
    *out_written = 0;
    if (!tls_client_status.active) {
        LOG_ERROR("TLS", "Envio TLS sem sessao ativa");
        return ERR_STATE;
    }
    result = tls_client_prepare_application(BR_SSL_SENDAPP);
    if (result != OK) return result;
    if (br_ssl_engine_current_state(engine) & BR_SSL_CLOSED) return ERR_STATE;
    if (!(br_ssl_engine_current_state(engine) & BR_SSL_SENDAPP)) return OK;
    destination = br_ssl_engine_sendapp_buf(engine, &capacity);
    if (!destination || !capacity) return OK;
    if (capacity > length) capacity = length;
    kmemcpy(destination, data, (uint32_t)capacity);
    br_ssl_engine_sendapp_ack(engine, capacity);
    br_ssl_engine_flush(engine, 0);
    *out_written = (uint16_t)capacity;
    return OK;
}

int tls_client_receive(uint8_t* data, uint16_t capacity,
                       uint16_t* out_read, uint8_t* out_eof) {
    br_ssl_engine_context* engine = &tls_client_context.eng;
    unsigned char* source;
    size_t available;
    int result;

    if (!data || !out_read || !out_eof) {
        LOG_ERROR("TLS", "Destino nulo na recepcao TLS");
        return ERR_NULL;
    }
    *out_read = 0;
    *out_eof = 0;
    if (!tls_client_status.active) {
        LOG_ERROR("TLS", "Recepcao TLS sem sessao ativa");
        return ERR_STATE;
    }
    result = tls_client_prepare_application(BR_SSL_RECVAPP);
    if (result != OK &&
        !(br_ssl_engine_current_state(engine) & BR_SSL_CLOSED)) {
        return result;
    }
    if (br_ssl_engine_current_state(engine) & BR_SSL_CLOSED) {
        if (br_ssl_engine_last_error(engine)) {
            return tls_client_fail(br_ssl_engine_last_error(engine));
        }
        *out_eof = 1U;
        return OK;
    }
    source = br_ssl_engine_recvapp_buf(engine, &available);
    if (!source || !available) return OK;
    if (available > capacity) available = capacity;
    kmemcpy(data, source, (uint32_t)available);
    br_ssl_engine_recvapp_ack(engine, available);
    *out_read = (uint16_t)available;
    return OK;
}

int tls_client_close(void) {
    if (!tls_client_status.initialized) {
        LOG_ERROR("TLS", "Fechamento TLS antes da inicializacao");
        return ERR_STATE;
    }
    tls_client_socket = 0;
    tls_client_status.active = 0U;
    tls_client_status.state = TLS_CLIENT_STATE_IDLE;
    tls_client_status.handshake_complete = 0U;
    return OK;
}

int tls_client_get_status(tls_client_status_t* output) {
    if (!output) {
        LOG_ERROR("TLS", "Destino nulo ao consultar cliente TLS");
        return ERR_NULL;
    }
    *output = tls_client_status;
    return OK;
}

int tls_client_validate_state(void) {
    if (tls_client_status.active && !tls_client_socket) {
        LOG_ERROR("TLS", "Cliente TLS ativo sem socket");
        return ERR_STATE;
    }
    if (tls_client_status.handshake_complete &&
        (!tls_client_status.x509_verified ||
         !tls_client_status.hostname_verified)) {
        LOG_ERROR("TLS", "Handshake TLS publicado sem validacao X.509");
        return ERR_STATE;
    }
    return OK;
}
