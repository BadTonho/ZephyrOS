#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/arp.h"
#include "core/ethernet.h"
#include "core/route.h"
#include "core/log.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define ROUTE_INTERFACE "host-net0"
#define ROUTE_LOCAL_IP 0x0A00020FU
#define ROUTE_MASK 0xFFFFFF00U
#define ROUTE_GATEWAY 0x0A000202U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(void* function) {
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
    printf("ZCOV_BEGIN|case=host:network:route|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:route|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:route|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    static uint32_t tick;
    return ++tick;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint8_t serial_is_ready(void) {
    return 1;
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

int ethernet_register_handler(uint16_t ethertype,
                              ethernet_protocol_handler_fn handler) {
    (void)ethertype;
    (void)handler;
    return ERR_UNAVAILABLE;
}

int ethernet_send(const char* interface_id, const uint8_t* destination,
                  uint16_t ethertype, const uint8_t* payload,
                  uint16_t payload_length) {
    (void)interface_id;
    (void)destination;
    (void)ethertype;
    (void)payload;
    (void)payload_length;
    return ERR_UNAVAILABLE;
}

int arp_configure(const char* interface_id, const uint8_t* local_mac,
                  uint32_t local_ip) {
    (void)interface_id;
    (void)local_mac;
    (void)local_ip;
    return ERR_UNAVAILABLE;
}

int arp_clear(void) {
    return OK;
}

int arp_resolve(uint32_t ip_address, uint8_t* out_mac,
                uint8_t* out_resolved) {
    (void)ip_address;
    (void)out_mac;
    if (out_resolved) *out_resolved = 0U;
    return ERR_UNAVAILABLE;
}

int arp_get_status(arp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = (arp_status_t){0};
    return OK;
}

static int expect_initial_state(void) {
    route_status_t status;
    route_match_t match;
    route_entry_t entry;

    if (route_validate_state() != ERR_STATE) return 10;
    if (route_get_status(&status) != ERR_STATE) return 11;
    if (route_get_status(NULL) != ERR_NULL) return 12;
    if (route_get_entry(0U, &entry) != ERR_STATE) return 13;
    if (route_get_entry(ROUTE_TABLE_CAPACITY, &entry) != ERR_INVALID) return 14;
    if (route_get_entry(0U, NULL) != ERR_NULL) return 15;
    if (route_lookup(0x0A000201U, &match) != ERR_STATE) return 16;
    if (route_lookup(0x0A000201U, NULL) != ERR_NULL) return 17;
    if (route_add(0x0A000000U, ROUTE_MASK, 0U, ROUTE_INTERFACE) != ERR_STATE) {
        return 18;
    }
    if (route_delete(0x0A000000U, ROUTE_MASK) != ERR_STATE) return 19;
    if (route_clear() != ERR_STATE) return 20;
    if (route_reset() != ERR_STATE) return 21;
    if (route_set_default(ROUTE_GATEWAY, ROUTE_INTERFACE) != ERR_STATE) {
        return 22;
    }
    return 0;
}

static int expect_direct_contracts(void) {
    route_status_t status;
    route_entry_t entry;
    route_match_t match;
    char long_interface[IPV4_INTERFACE_ID_SIZE + 2U];

    if (route_init() != OK || route_init() != OK) return 30;
    if (route_set_base(ROUTE_LOCAL_IP, ROUTE_MASK, ROUTE_GATEWAY,
                       ROUTE_INTERFACE) != OK) return 31;
    if (route_get_status(&status) != OK || !status.initialized ||
        status.entry_count != 2U) return 32;
    if (route_validate_state() != OK) return 33;
    if (route_lookup(0x0A000203U, &match) != OK || !match.matched ||
        match.via_gateway || match.next_hop != 0x0A000203U) return 34;
    if (route_get_entry(0U, &entry) != OK || !entry.used) return 35;
    if (route_add(0x0A010000U, 0xFFFF0000U, 0U, ROUTE_INTERFACE) != OK) {
        return 36;
    }
    if (route_add(0x0A010000U, 0xFFFF0000U, 0U, ROUTE_INTERFACE) != ERR_STATE) {
        return 37;
    }
    if (route_set_default(ROUTE_GATEWAY, ROUTE_INTERFACE) != OK) return 38;
    if (route_delete(0x0A010000U, 0xFFFF0000U) != OK) return 39;
    if (route_delete(0x0A010000U, 0xFFFF0000U) != ERR_NOT_FOUND) return 40;
    if (route_add(0x0A000000U, 0xFF00FF00U, 0U, ROUTE_INTERFACE) != ERR_INVALID) {
        return 41;
    }
    if (route_add(0x0A000001U, ROUTE_MASK, 0U, ROUTE_INTERFACE) != ERR_INVALID) {
        return 42;
    }
    if (route_add(0x0A020000U, 0xFFFF0000U, 0xE0000001U,
                  ROUTE_INTERFACE) != ERR_INVALID) return 43;
    if (route_add(0x0A030000U, 0xFFFF0000U, 0U, "") != ERR_INVALID) {
        return 44;
    }
    for (uint32_t index = 0U; index < sizeof(long_interface) - 1U; index++) {
        long_interface[index] = 'x';
    }
    long_interface[sizeof(long_interface) - 1U] = '\0';
    if (route_add(0x0A040000U, 0xFFFF0000U, 0U, long_interface) != ERR_OVERFLOW) {
        return 45;
    }
    if (route_set_base(0x0A000201U, 0xFFFFFF00U, 0x0A000201U,
                       ROUTE_INTERFACE) != ERR_INVALID) return 46;
    if (route_set_base(0x0A000200U, ROUTE_MASK, 0U, ROUTE_INTERFACE) != ERR_INVALID) {
        return 47;
    }
    if (route_clear() != OK || route_reset() != ERR_STATE) return 48;
    return route_validate_state() == OK ? 0 : 49;
}

int main(void) {
    route_self_test_result_t self_test;
    int result;

    coverage_active = 1U;
    result = expect_initial_state();
    if (!result) result = expect_direct_contracts();
    if (!result && route_init() != OK) result = 50;
    if (!result && route_self_test(&self_test) != OK) result = 51;
    if (!result && (self_test.failed != 0U || self_test.passed != 10U)) result = 52;
    if (!result && route_validate_state() != OK) result = 53;
    coverage_active = 0U;
    coverage_emit(result);
    if (result) printf("route-host: FAIL code=%d\n", result);
    else printf("route-host: PASS\n");
    return result;
}
