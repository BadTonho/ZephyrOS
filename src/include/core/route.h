#ifndef ROUTE_H
#define ROUTE_H

#include "types.h"
#include "core/ipv4.h"

#define ROUTE_TABLE_CAPACITY 16U

typedef struct {
    uint8_t used;
    uint8_t prefix_length;
    uint32_t network;
    uint32_t subnet_mask;
    uint32_t gateway;
    char interface_id[IPV4_INTERFACE_ID_SIZE];
} route_entry_t;

typedef struct {
    uint8_t initialized;
    uint8_t entry_count;
    uint32_t lookups;
    uint32_t matches;
    uint32_t misses;
    uint32_t adds;
    uint32_t deletes;
    uint32_t replacements;
    int last_error;
} route_status_t;

typedef struct {
    uint8_t matched;
    uint8_t via_gateway;
    route_entry_t entry;
    uint32_t next_hop;
} route_match_t;

typedef struct {
    uint8_t lifecycle;
    uint8_t direct_route;
    uint8_t default_route;
    uint8_t longest_prefix;
    uint8_t delete_route;
    uint8_t duplicate_route;
    uint8_t overflow;
    uint8_t invalid_input;
    uint8_t reset;
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} route_self_test_result_t;

int route_init(void);
int route_set_base(uint32_t local_ip, uint32_t subnet_mask,
                   uint32_t gateway, const char* interface_id);
int route_clear(void);
int route_reset(void);
int route_add(uint32_t network, uint32_t subnet_mask, uint32_t gateway,
              const char* interface_id);
int route_delete(uint32_t network, uint32_t subnet_mask);
int route_set_default(uint32_t gateway, const char* interface_id);
int route_lookup(uint32_t destination_ip, route_match_t* out_match);
int route_get_status(route_status_t* out_status);
int route_get_entry(uint32_t index, route_entry_t* out_entry);
int route_validate_state(void);
int route_self_test(route_self_test_result_t* out_result);

#endif
