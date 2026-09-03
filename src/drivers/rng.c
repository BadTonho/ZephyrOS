#include "drivers/rng.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define RNG_CPUID_RDRAND_BIT (1U << 30U)
#define RNG_RDRAND_RETRIES 10U

static rng_status_t rng_status;

static uint8_t rng_cpu_has_cpuid(void) {
#if defined(ZEPHYROS_HOST_TEST)
    extern uint8_t rng_host_cpuid_available;

    return rng_host_cpuid_available;
#else
    uint32_t original;
    uint32_t toggled;
    uint32_t current;

    asm volatile("pushfl\n\tpopl %0" : "=r"(original));
    toggled = original ^ (1U << 21U);
    asm volatile("pushl %0\n\tpopfl" : : "r"(toggled) : "cc");
    asm volatile("pushfl\n\tpopl %0" : "=r"(current));
    asm volatile("pushl %0\n\tpopfl" : : "r"(original) : "cc");
    return (uint8_t)(((current ^ original) & (1U << 21U)) != 0U);
#endif
}

static int rng_cpuid(uint32_t leaf, uint32_t* out_ecx) {
#if defined(ZEPHYROS_HOST_TEST)
    extern uint8_t rng_host_rdrand_available;

    if (!out_ecx) return ERR_NULL;
    *out_ecx = (leaf == 1U && rng_host_rdrand_available) ?
               RNG_CPUID_RDRAND_BIT : 0U;
    return OK;
#else
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    if (!out_ecx) return ERR_NULL;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(leaf));
    (void)eax;
    (void)ebx;
    (void)edx;
    *out_ecx = ecx;
    return OK;
#endif
}

static int rng_read_word(uint32_t* output) {
#if defined(ZEPHYROS_HOST_TEST)
    extern uint8_t rng_host_rdrand_ready;
    extern uint32_t rng_host_word;
#else
    uint8_t ready;
#endif

    if (!output) return ERR_NULL;
#if defined(ZEPHYROS_HOST_TEST)
    if (!rng_host_rdrand_ready) {
        rng_status.hardware_failures++;
        rng_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("RNG", "RDRAND nao forneceu uma palavra apos as tentativas");
        return ERR_UNAVAILABLE;
    }
    *output = rng_host_word++;
    rng_status.words_generated++;
    return OK;
#else
    for (uint32_t attempt = 0; attempt < RNG_RDRAND_RETRIES; attempt++) {
        asm volatile(".byte 0x0f, 0xc7, 0xf0; setc %0"
                     : "=m"(ready), "=a"(*output) : : "cc");
        if (ready) {
            rng_status.words_generated++;
            return OK;
        }
    }
    rng_status.hardware_failures++;
    rng_status.last_error = ERR_UNAVAILABLE;
    LOG_ERROR("RNG", "RDRAND nao forneceu uma palavra apos as tentativas");
    return ERR_UNAVAILABLE;
#endif
}

int rng_init(void) {
    uint32_t max_leaf = 0;
    uint32_t features = 0;

    LOG_INFO("RNG", "Inicializando fonte de entropia RDRAND");
    kmemset(&rng_status, 0, sizeof(rng_status));
    if (!rng_cpu_has_cpuid()) {
        rng_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("RNG", "CPU sem instrucao CPUID; TLS sera bloqueado");
        return ERR_UNAVAILABLE;
    }
#if defined(ZEPHYROS_HOST_TEST)
    max_leaf = 1U;
#else
    asm volatile("cpuid"
                 : "=a"(max_leaf)
                 : "a"(0)
                 : "ebx", "ecx", "edx");
#endif
    rng_status.cpuid_available = (uint8_t)(max_leaf >= 1U);
    if (!rng_status.cpuid_available || rng_cpuid(1U, &features) != OK) {
        rng_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("RNG", "CPUID nao permite consultar RDRAND");
        return ERR_UNAVAILABLE;
    }
    rng_status.rdrand_available =
        (uint8_t)((features & RNG_CPUID_RDRAND_BIT) != 0U);
    if (!rng_status.rdrand_available) {
        rng_status.last_error = ERR_UNAVAILABLE;
        LOG_ERROR("RNG", "CPU sem suporte RDRAND; TLS sera bloqueado");
        return ERR_UNAVAILABLE;
    }
    rng_status.initialized = 1U;
    rng_status.last_error = OK;
    LOG_INFO("RNG", "RDRAND inicializado com sucesso");
    return OK;
}

int rng_get_bytes(uint8_t* output, uint32_t length) {
    if (!output && length) {
        LOG_ERROR("RNG", "Buffer nulo para entropia");
        return ERR_NULL;
    }
    if (!rng_status.initialized || !rng_status.rdrand_available) {
        LOG_ERROR("RNG", "Entropia solicitada antes de RDRAND estar pronto");
        return ERR_UNAVAILABLE;
    }
    for (uint32_t offset = 0; offset < length;) {
        uint32_t word = 0;
        uint32_t chunk = length - offset;
        int result = rng_read_word(&word);

        if (result != OK) return result;
        if (chunk > sizeof(word)) chunk = sizeof(word);
        for (uint32_t index = 0; index < chunk; index++) {
            output[offset + index] = (uint8_t)(word >> (index * 8U));
        }
        offset += chunk;
    }
    rng_status.last_error = OK;
    return OK;
}

int rng_get_status(rng_status_t* output) {
    if (!output) {
        LOG_ERROR("RNG", "Destino nulo ao consultar RDRAND");
        return ERR_NULL;
    }
    *output = rng_status;
    return OK;
}

int rng_validate_state(void) {
    if (!rng_status.initialized &&
        (rng_status.rdrand_available || rng_status.words_generated)) {
        LOG_ERROR("RNG", "Estado RDRAND inconsistente");
        return ERR_STATE;
    }
    if (rng_status.initialized &&
        (!rng_status.cpuid_available || !rng_status.rdrand_available)) {
        LOG_ERROR("RNG", "RDRAND publicado sem capacidade de hardware");
        return ERR_STATE;
    }
    return OK;
}
