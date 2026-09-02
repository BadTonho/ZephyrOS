#include <stdint.h>
#include <stdio.h>

#include "core/ethernet.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/net_buffer.h"
#include "core/sk_buff.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/serial.h"
#include "kernel_tests.h"
#include "memory/paging.h"
#include "memory/slab.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define PAGE_SIZE 4096U
#define FRAME_BUFFER_SIZE ETHERNET_MAX_FRAME_SIZE
#define FAKE_DRIVER_COUNT 5U
#define ETHERNET_TYPE_IPV4 0x0800U
#define ETHERNET_TYPE_ARP 0x0806U
#define ETHERNET_TYPE_IPV6 0x86DDU
#define ETHERNET_TYPE_TEST_A 0x88B5U
#define ETHERNET_TYPE_TEST_B 0x88B6U
#define ETHERNET_TYPE_INVALID 0x0100U
#define ETHERNET_PAYLOAD_SIZE 24U
#define PAGE_POOL_PAGES 256U

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
    printf("ZCOV_BEGIN|case=host:network:ethernet|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:ethernet|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:ethernet|value=0x%08X\n",
           (uint32_t)result);
}

const kernel_tests_runtime_t* kernel_tests_active_runtime;

int kernel_tests_progress(const kernel_tests_runtime_t* runtime) {
    (void)runtime;
    return OK;
}

static uint8_t page_pool[PAGE_POOL_PAGES * PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_pool_cursor;
static uint32_t fake_ticks;

int paging_is_ready(void) {
    return 1;
}

void* pmm_alloc_pages_in_zone(uint32_t count, memory_zone_t zone) {
    uint8_t* result;

    (void)zone;
    if (!count || count > PAGE_POOL_PAGES - page_pool_cursor) return NULL;
    result = page_pool + page_pool_cursor * PAGE_SIZE;
    page_pool_cursor += count;
    return result;
}

void pmm_free_pages(void* address, uint32_t count) {
    (void)address;
    (void)count;
}

void memory_get_pmm_stats(memory_pmm_stats_t* stats) {
    if (!stats) return;
    stats->owned_pages = 0U;
    stats->allocation_failures = 0U;
    stats->invalid_frees = 0U;
    stats->initialized = 1U;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
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

typedef struct {
    ethernet_driver_status_t status;
    uint8_t pending;
    uint8_t received;
    uint8_t frame[FRAME_BUFFER_SIZE];
    uint16_t frame_length;
    uint8_t last_sent[FRAME_BUFFER_SIZE];
    uint16_t last_sent_length;
    int service_result;
    int pending_result;
    int receive_result;
    int send_result;
    int status_result;
    int quiesce_result;
    uint32_t service_calls;
    uint32_t receive_calls;
    uint32_t send_calls;
    uint32_t quiesce_calls;
} fake_driver_t;

static fake_driver_t drivers[FAKE_DRIVER_COUNT];
static uint32_t handler_calls;
static uint16_t handler_payload_length;
static ethernet_destination_t handler_destination_type;
static int handler_result;

static int fake_get_status(void* context,
                           ethernet_driver_status_t* out_status) {
    fake_driver_t* driver = (fake_driver_t*)context;

    if (!driver || !out_status) return ERR_NULL;
    if (driver->status_result != OK) return driver->status_result;
    *out_status = driver->status;
    return OK;
}

static int fake_service_pending(void* context) {
    fake_driver_t* driver = (fake_driver_t*)context;

    if (!driver) return ERR_NULL;
    driver->service_calls++;
    return driver->service_result;
}

static int fake_rx_pending(void* context, uint8_t* out_pending) {
    fake_driver_t* driver = (fake_driver_t*)context;

    if (!driver || !out_pending) return ERR_NULL;
    if (driver->pending_result != OK) return driver->pending_result;
    *out_pending = driver->pending;
    return OK;
}

static int fake_receive_frame(void* context, uint8_t* data,
                              uint16_t capacity, uint16_t* out_length,
                              uint8_t* out_received) {
    fake_driver_t* driver = (fake_driver_t*)context;

    if (!driver || !data || !out_length || !out_received) return ERR_NULL;
    driver->receive_calls++;
    if (driver->receive_result != OK) return driver->receive_result;
    *out_received = driver->received;
    *out_length = driver->frame_length;
    if (driver->received && driver->frame_length <= capacity) {
        kmemcpy(data, driver->frame, driver->frame_length);
    }
    driver->pending = 0U;
    return driver->frame_length <= capacity ? OK : ERR_OVERFLOW;
}

static int fake_send_frame(void* context, const uint8_t* data,
                           uint16_t length) {
    fake_driver_t* driver = (fake_driver_t*)context;

    if (!driver || !data) return ERR_NULL;
    driver->send_calls++;
    if (driver->send_result != OK) return driver->send_result;
    if (length > sizeof(driver->last_sent)) return ERR_OVERFLOW;
    kmemcpy(driver->last_sent, data, length);
    driver->last_sent_length = length;
    return OK;
}

static int fake_quiesce(void* context) {
    fake_driver_t* driver = (fake_driver_t*)context;

    if (!driver) return ERR_NULL;
    driver->quiesce_calls++;
    return driver->quiesce_result;
}

static int handler_accept(const ethernet_frame_view_t* frame) {
    if (!frame) return ERR_NULL;
    handler_calls++;
    handler_payload_length = frame->payload_length;
    handler_destination_type = frame->destination_type;
    return handler_result;
}

static int handler_alternate(const ethernet_frame_view_t* frame) {
    return frame ? OK : ERR_NULL;
}

static void make_frame(fake_driver_t* driver, const uint8_t* destination,
                       const uint8_t* source, uint16_t ethertype,
                       uint16_t payload_length) {
    uint16_t length = ETHERNET_HEADER_SIZE + payload_length;

    if (length < ETHERNET_MIN_FRAME_SIZE) length = ETHERNET_MIN_FRAME_SIZE;
    kmemset(driver->frame, 0, sizeof(driver->frame));
    kmemcpy(driver->frame, destination, ETHERNET_MAC_ADDRESS_SIZE);
    kmemcpy(driver->frame + ETHERNET_MAC_ADDRESS_SIZE, source,
            ETHERNET_MAC_ADDRESS_SIZE);
    driver->frame[12] = (uint8_t)(ethertype >> 8U);
    driver->frame[13] = (uint8_t)ethertype;
    for (uint16_t index = ETHERNET_HEADER_SIZE;
         index < ETHERNET_HEADER_SIZE + payload_length; index++) {
        driver->frame[index] = (uint8_t)index;
    }
    driver->frame_length = length;
    driver->pending = 1U;
    driver->received = 1U;
}

static ethernet_interface_t make_interface(uint32_t index) {
    ethernet_interface_t interface;
    static const char* interface_ids[FAKE_DRIVER_COUNT] = {
        "eth0", "eth1", "eth2", "eth3", "eth4"
    };

    kmemset(&interface, 0, sizeof(interface));
    interface.initialized = 1U;
    kmemcpy(interface.interface_id, interface_ids[index],
            kstrlen(interface_ids[index]) + 1U);
    interface.mac_address[0] = 0x02U;
    interface.mac_address[5] = (uint8_t)(index + 1U);
    interface.driver_context = &drivers[index];
    interface.get_driver_status = fake_get_status;
    interface.service_pending = fake_service_pending;
    interface.rx_pending = fake_rx_pending;
    interface.receive_frame = fake_receive_frame;
    interface.send_frame = fake_send_frame;
    interface.quiesce = fake_quiesce;
    drivers[index].status.initialized = 1U;
    drivers[index].status.link_up = 1U;
    drivers[index].status.last_error = OK;
    drivers[index].service_result = OK;
    drivers[index].pending_result = OK;
    drivers[index].receive_result = OK;
    drivers[index].send_result = OK;
    drivers[index].status_result = OK;
    drivers[index].quiesce_result = OK;
    return interface;
}

static int check_ethernet(void) {
    const uint8_t local_mac[ETHERNET_MAC_ADDRESS_SIZE] =
        {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};
    const uint8_t peer_mac[ETHERNET_MAC_ADDRESS_SIZE] =
        {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U};
    const uint8_t broadcast_mac[ETHERNET_MAC_ADDRESS_SIZE] =
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
    uint8_t payload[ETHERNET_PAYLOAD_SIZE];
    uint8_t zero_mac[ETHERNET_MAC_ADDRESS_SIZE] = {0};
    uint8_t invalid_source[ETHERNET_MAC_ADDRESS_SIZE] =
        {0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x04U};
    ethernet_interface_t interface;
    ethernet_interface_status_t interface_status;
    ethernet_status_t status;
    sk_buff_stats_t skb_stats;
    uint32_t processed;

    for (uint32_t index = 0U; index < sizeof(payload); index++) {
        payload[index] = (uint8_t)(index + 1U);
    }
    page_pool_cursor = 0U;
    kmemset(drivers, 0, sizeof(drivers));
    if (ethernet_validate_state() != OK ||
        ethernet_attach_interface(NULL) != ERR_STATE ||
        ethernet_register_handler(ETHERNET_TYPE_IPV4, handler_accept) !=
            ERR_STATE || ethernet_poll(1U, &processed) != ERR_STATE ||
        ethernet_send("eth0", peer_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_STATE ||
        ethernet_quiesce() != ERR_UNAVAILABLE ||
        ethernet_set_quiescing(1U) != ERR_STATE) return 1;
    if (kmem_cache_init() != OK || kmem_cache_init() != OK ||
        ethernet_init() != OK || ethernet_init() != OK ||
        skb_get_stats(&skb_stats) != OK || !skb_stats.initialized ||
        net_buffer_validate_state() != OK) return 2;
    interface = make_interface(0U);
    if (ethernet_attach_interface(NULL) != ERR_NULL ||
        ethernet_attach_interface(&interface) != OK ||
        ethernet_attach_interface(&interface) != OK ||
        ethernet_get_interface_status(NULL, &interface_status) != ERR_NULL ||
        ethernet_get_interface_status("missing", &interface_status) !=
            ERR_NOT_FOUND) return 3;
    interface.mac_address[0] = 0U;
    if (ethernet_attach_interface(&interface) != OK) return 4;
    interface = make_interface(1U);
    interface.get_driver_status = NULL;
    if (ethernet_attach_interface(&interface) != ERR_INVALID) return 5;
    interface = make_interface(1U);
    interface.mac_address[0] = 1U;
    if (ethernet_attach_interface(&interface) != ERR_INVALID) return 6;
    interface = make_interface(1U);
    kmemset(interface.interface_id, 'x', sizeof(interface.interface_id));
    if (ethernet_attach_interface(&interface) != ERR_OVERFLOW) return 7;
    for (uint32_t index = 1U; index < 4U; index++) {
        interface = make_interface(index);
        if (ethernet_attach_interface(&interface) != OK) return 8;
    }
    interface = make_interface(4U);
    if (ethernet_attach_interface(&interface) != ERR_OVERFLOW) return 9;
    if (ethernet_register_handler(ETHERNET_TYPE_INVALID, handler_accept) !=
            ERR_INVALID || ethernet_register_handler(ETHERNET_TYPE_IPV4,
                                                       NULL) != ERR_NULL ||
        ethernet_register_handler(ETHERNET_TYPE_IPV4, handler_accept) != OK ||
        ethernet_register_handler(ETHERNET_TYPE_IPV4, handler_alternate) !=
            ERR_STATE || ethernet_register_handler(ETHERNET_TYPE_ARP,
                                                    handler_accept) != OK ||
        ethernet_register_handler(ETHERNET_TYPE_IPV6, handler_accept) != OK ||
        ethernet_register_handler(ETHERNET_TYPE_TEST_A, handler_accept) != OK ||
        ethernet_register_handler(ETHERNET_TYPE_TEST_B, handler_accept) !=
            ERR_OVERFLOW) return 10;
    if (ethernet_get_status(NULL) != ERR_NULL ||
        ethernet_get_interface_status("eth0", NULL) != ERR_NULL ||
        ethernet_get_interface_status("eth0", &interface_status) != OK ||
        interface_status.driver.link_up != 1U || ethernet_validate_state() !=
            OK) return 11;
    if (ethernet_poll(0U, &processed) != ERR_INVALID ||
        ethernet_poll(ETHERNET_RX_POLL_BUDGET + 1U, &processed) != ERR_INVALID ||
        ethernet_poll(1U, NULL) != ERR_NULL ||
        ethernet_poll(ETHERNET_RX_POLL_BUDGET, &processed) != OK ||
        processed != 0U) return 12;
    make_frame(&drivers[0], local_mac, peer_mac, ETHERNET_TYPE_IPV4,
               sizeof(payload));
    handler_result = OK;
    if (ethernet_poll(1U, &processed) != OK || processed != 1U ||
        handler_calls != 1U || handler_payload_length !=
            ETHERNET_MIN_FRAME_SIZE - ETHERNET_HEADER_SIZE ||
        handler_destination_type != ETHERNET_DESTINATION_LOCAL_UNICAST) {
        return 13;
    }
    make_frame(&drivers[0], broadcast_mac, peer_mac, ETHERNET_TYPE_ARP, 0U);
    if (ethernet_poll(1U, &processed) != OK || processed != 1U ||
        handler_calls != 2U ||
        handler_destination_type != ETHERNET_DESTINATION_BROADCAST) return 14;
    make_frame(&drivers[0], peer_mac, peer_mac, ETHERNET_TYPE_IPV4, 0U);
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 15;
    make_frame(&drivers[0], local_mac, invalid_source, ETHERNET_TYPE_IPV4, 0U);
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 16;
    make_frame(&drivers[0], local_mac, peer_mac, ETHERNET_TYPE_INVALID, 0U);
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 17;
    make_frame(&drivers[0], local_mac, peer_mac, ETHERNET_TYPE_IPV4, 0U);
    drivers[0].frame_length = ETHERNET_HEADER_SIZE - 1U;
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 18;
    drivers[0].pending = 1U;
    drivers[0].received = 0U;
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 19;
    drivers[0].received = 1U;
    drivers[0].service_result = ERR_TIMEOUT;
    drivers[0].pending = 1U;
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 20;
    drivers[0].service_result = OK;
    drivers[0].pending_result = ERR_AGAIN;
    drivers[0].pending = 1U;
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 21;
    drivers[0].pending_result = OK;
    drivers[0].receive_result = ERR_DISK;
    drivers[0].pending = 1U;
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 22;
    drivers[0].receive_result = OK;
    make_frame(&drivers[0], local_mac, peer_mac, ETHERNET_TYPE_IPV4, 0U);
    handler_result = ERR_INVALID;
    if (ethernet_poll(1U, &processed) != OK || processed != 0U) return 23;
    handler_result = OK;
    if (ethernet_send(NULL, peer_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_NULL ||
        ethernet_send("eth0", NULL, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_NULL ||
        ethernet_send("eth0", zero_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_INVALID ||
        ethernet_send("eth0", peer_mac, ETHERNET_TYPE_INVALID,
                      payload, sizeof(payload)) != ERR_INVALID ||
        ethernet_send("eth0", peer_mac, ETHERNET_TYPE_IPV4,
                      payload, ETHERNET_MAX_PAYLOAD_SIZE + 1U) != ERR_INVALID ||
        ethernet_send("missing", peer_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_NOT_FOUND ||
        ethernet_send("eth0", peer_mac, ETHERNET_TYPE_IPV4, NULL, 1U) !=
            ERR_NULL) return 24;
    if (ethernet_send("eth0", peer_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != OK ||
        drivers[0].send_calls == 0U || drivers[0].last_sent_length <
            ETHERNET_MIN_FRAME_SIZE) return 25;
    drivers[0].send_result = ERR_DISK;
    if (ethernet_send("eth0", peer_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_DISK) return 26;
    drivers[0].send_result = OK;
    drivers[0].status_result = ERR_TIMEOUT;
    if (ethernet_get_interface_status("eth0", &interface_status) !=
            ERR_TIMEOUT) return 27;
    drivers[0].status_result = OK;
    drivers[0].quiesce_result = ERR_UNAVAILABLE;
    if (ethernet_quiesce() != ERR_UNAVAILABLE ||
        ethernet_poll(1U, &processed) != ERR_UNAVAILABLE ||
        ethernet_send("eth0", peer_mac, ETHERNET_TYPE_IPV4,
                      payload, sizeof(payload)) != ERR_UNAVAILABLE ||
        ethernet_set_quiescing(0U) != OK) return 28;
    if (ethernet_quiesce() != ERR_UNAVAILABLE ||
        ethernet_set_quiescing(0U) != OK) return 29;
    drivers[0].quiesce_result = OK;
    if (ethernet_quiesce() != OK || ethernet_set_quiescing(0U) != OK ||
        ethernet_get_status(&status) != OK || status.interface_count != 4U ||
        status.handler_count != ETHERNET_PROTOCOL_HANDLER_CAPACITY ||
        ethernet_validate_state() != OK || skb_get_stats(&skb_stats) != OK ||
        skb_stats.active_buffers != 0U || skb_self_test() != OK ||
        skb_validate_state() != OK || net_buffer_validate_state() != OK) {
        return 30;
    }
    if (ethernet_get_interface_status("eth0", &interface_status) != OK ||
        interface_status.rx_unicast == 0U ||
        interface_status.rx_broadcast == 0U ||
        interface_status.rx_filtered == 0U ||
        interface_status.rx_invalid == 0U ||
        interface_status.rx_protocol_errors == 0U ||
        interface_status.poll_errors == 0U ||
        interface_status.tx_frames != 1U || status.rx_delivered < 2U ||
        status.tx_frames != 1U) return 31;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_ethernet();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
