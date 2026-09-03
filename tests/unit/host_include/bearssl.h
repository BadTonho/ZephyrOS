#ifndef HOST_BEARSSL_H
#define HOST_BEARSSL_H

#include "types.h"

#define BR_TLS12 0x0303U
#define BR_SSL_BUFSIZE_MONO 256U

#define BR_SSL_CLOSED 0x0001U
#define BR_SSL_SENDREC 0x0002U
#define BR_SSL_RECVREC 0x0004U
#define BR_SSL_SENDAPP 0x0008U
#define BR_SSL_RECVAPP 0x0010U

#define BR_ERR_BAD_STATE 2
#define BR_ERR_BAD_VERSION 4
#define BR_ERR_NO_RANDOM 8
#define BR_ERR_BAD_HANDSHAKE 14
#define BR_ERR_IO 31
#define BR_ERR_X509_TIME_UNKNOWN 37
#define BR_ERR_X509_EXPIRED 38
#define BR_ERR_X509_NOT_TRUSTED 48
#define BR_ERR_X509_INVALID_VALUE 50
#define BR_ERR_X509_WEAK_PUBLIC_KEY 62
#define BR_ERR_X509_BAD_SERVER_NAME 66

#define BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 0xC02BU
#define BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 0xC02FU

#define BR_KEYTYPE_EC 2U
#define BR_KEYTYPE_RSA 1U
#define BR_X509_TA_CA 1U
#define BR_EC_secp256r1 23U

typedef struct br_hash_class_ {
    uint32_t marker;
} br_hash_class;

typedef struct br_prf_class_ {
    uint32_t marker;
} br_prf_class;

typedef struct br_x509_class_ {
    uint32_t marker;
} br_x509_class;

typedef struct {
    uint8_t* data;
    size_t len;
} br_x509_dn;

typedef struct {
    uint32_t curve;
    uint8_t* q;
    size_t qlen;
} br_ec_public_key;

typedef struct {
    uint8_t* n;
    size_t nlen;
    uint8_t* e;
    size_t elen;
} br_rsa_public_key;

typedef struct {
    uint32_t key_type;
    union {
        br_ec_public_key ec;
        br_rsa_public_key rsa;
    } key;
} br_x509_pkey;

typedef struct {
    br_x509_dn dn;
    uint32_t flags;
    br_x509_pkey pkey;
} br_x509_trust_anchor;

typedef struct br_x509_minimal_context_ {
    br_x509_class vtable;
} br_x509_minimal_context;

typedef struct {
    uint16_t version;
    uint16_t cipher_suite;
} br_ssl_session_parameters;

typedef struct br_ssl_engine_context_ {
    int err;
    unsigned state;
    unsigned char* buffer;
    size_t buffer_length;
    size_t application_length;
    uint8_t application_mode;
    uint8_t application_data[32];
} br_ssl_engine_context;

typedef struct br_ssl_client_context_ {
    br_ssl_engine_context eng;
} br_ssl_client_context;

enum {
    br_sha1_ID = 2,
    br_sha256_ID = 4,
    br_sha384_ID = 5,
    br_sha512_ID = 6
};

extern const br_hash_class br_sha1_vtable;
extern const br_hash_class br_sha256_vtable;
extern const br_hash_class br_sha384_vtable;
extern const br_hash_class br_sha512_vtable;
extern const br_prf_class br_tls12_sha256_prf;
extern const br_prf_class br_tls12_sha384_prf;

void br_ssl_client_zero(br_ssl_client_context* context);
void br_ssl_engine_set_versions(br_ssl_engine_context* engine,
                                uint16_t minimum, uint16_t maximum);
void br_x509_minimal_init(br_x509_minimal_context* context,
                          const br_hash_class* digest,
                          const br_x509_trust_anchor* anchors,
                          size_t anchor_count);
void br_x509_minimal_set_hash(br_x509_minimal_context* context, int id,
                              const br_hash_class* digest);
void br_x509_minimal_set_time(br_x509_minimal_context* context,
                              uint32_t days, uint32_t seconds);
void br_ssl_engine_set_suites(br_ssl_engine_context* engine,
                              const uint16_t* suites, size_t suite_count);
void br_ssl_engine_set_hash(br_ssl_engine_context* engine, int id,
                            const br_hash_class* digest);
void br_ssl_client_set_default_rsapub(br_ssl_client_context* context);
void br_ssl_engine_set_default_rsavrfy(br_ssl_engine_context* engine);
void br_ssl_engine_set_default_ec(br_ssl_engine_context* engine);
void br_ssl_engine_set_default_ecdsa(br_ssl_engine_context* engine);
const void* br_ssl_engine_get_rsavrfy(br_ssl_engine_context* engine);
const void* br_ssl_engine_get_ec(br_ssl_engine_context* engine);
const void* br_ssl_engine_get_ecdsa(br_ssl_engine_context* engine);
void br_x509_minimal_set_rsa(br_x509_minimal_context* context,
                             const void* implementation);
void br_x509_minimal_set_ecdsa(br_x509_minimal_context* context,
                               const void* ec_implementation,
                               const void* ecdsa_implementation);
void br_ssl_engine_set_default_aes_gcm(br_ssl_engine_context* engine);
void br_ssl_engine_set_x509(br_ssl_engine_context* engine,
                            const br_x509_class* implementation);
void br_ssl_engine_set_prf_sha256(br_ssl_engine_context* engine,
                                  const br_prf_class* implementation);
void br_ssl_engine_set_prf_sha384(br_ssl_engine_context* engine,
                                  const br_prf_class* implementation);
void br_ssl_engine_set_buffer(br_ssl_engine_context* engine,
                              unsigned char* buffer, size_t length,
                              size_t min_length);
void br_ssl_engine_inject_entropy(br_ssl_engine_context* engine,
                                  const void* entropy, size_t length);
int br_ssl_client_reset(br_ssl_client_context* context,
                        const char* hostname, int resume);
unsigned br_ssl_engine_current_state(const br_ssl_engine_context* engine);
int br_ssl_engine_last_error(const br_ssl_engine_context* engine);
unsigned char* br_ssl_engine_sendrec_buf(const br_ssl_engine_context* engine,
                                          size_t* length);
void br_ssl_engine_sendrec_ack(br_ssl_engine_context* engine, size_t length);
unsigned char* br_ssl_engine_recvrec_buf(const br_ssl_engine_context* engine,
                                          size_t* length);
void br_ssl_engine_recvrec_ack(br_ssl_engine_context* engine, size_t length);
void br_ssl_engine_get_session_parameters(
    const br_ssl_engine_context* engine, br_ssl_session_parameters* parameters);
void br_ssl_engine_flush(br_ssl_engine_context* engine, int force);
unsigned char* br_ssl_engine_sendapp_buf(const br_ssl_engine_context* engine,
                                          size_t* length);
void br_ssl_engine_sendapp_ack(br_ssl_engine_context* engine, size_t length);
unsigned char* br_ssl_engine_recvapp_buf(const br_ssl_engine_context* engine,
                                          size_t* length);
void br_ssl_engine_recvapp_ack(br_ssl_engine_context* engine, size_t length);

#endif
