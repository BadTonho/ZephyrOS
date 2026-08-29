#ifndef NET_BUFFER_H
#define NET_BUFFER_H

#include "types.h"

#define NET_BUFFER_TRACKING_CAPACITY 32U
#define NET_BUFFER_MAX_ALIGNMENT 64U

typedef enum {
    NET_BUFFER_STATE_INVALID = 0,
    NET_BUFFER_STATE_ALLOCATED,
    NET_BUFFER_STATE_RX,
    NET_BUFFER_STATE_QUEUED,
    NET_BUFFER_STATE_IN_FLIGHT,
    NET_BUFFER_STATE_DELIVERED,
    NET_BUFFER_STATE_DROPPED,
    NET_BUFFER_STATE_FREED
} net_buffer_state_t;

typedef enum {
    NET_BUFFER_OWNER_NONE = 0,
    NET_BUFFER_OWNER_DRIVER,
    NET_BUFFER_OWNER_ETHERNET,
    NET_BUFFER_OWNER_PROTOCOL,
    NET_BUFFER_OWNER_SOCKET
} net_buffer_owner_t;

typedef struct {
    net_buffer_state_t state;
    net_buffer_owner_t owner;
    uint32_t refcount;
    uint32_t capacity;
    uint32_t headroom;
    uint32_t length;
    uint32_t tailroom;
    uint32_t alignment;
    int completion_error;
} net_buffer_t;

typedef struct {
    uint8_t initialized;
    uint32_t active_buffers;
    uint32_t peak_buffers;
    uint32_t allocations;
    uint32_t frees;
    uint32_t delivered;
    uint32_t dropped;
    uint32_t copies;
    uint32_t copied_bytes;
    uint32_t clones;
    uint32_t fragments;
    uint32_t invalid_transitions;
    uint32_t duplicate_completions;
    uint32_t ref_acquires;
    uint32_t ref_releases;
    int last_error;
} net_buffer_stats_t;

int net_buffer_init(void);
int net_buffer_begin(net_buffer_t* buffer, uint32_t capacity,
                     uint32_t headroom, uint32_t alignment,
                     net_buffer_owner_t owner);
int net_buffer_set_layout(net_buffer_t* buffer, uint32_t headroom,
                          uint32_t length);
int net_buffer_set_length(net_buffer_t* buffer, uint32_t length);
int net_buffer_transition(net_buffer_t* buffer,
                          net_buffer_state_t next_state,
                          net_buffer_owner_t next_owner);
int net_buffer_complete(net_buffer_t* buffer, int result,
                        net_buffer_owner_t delivered_owner);
int net_buffer_retain(net_buffer_t* buffer);
int net_buffer_release(net_buffer_t* buffer);
int net_buffer_note_copy(uint32_t bytes);
int net_buffer_note_clone(void);
int net_buffer_note_fragment(void);
int net_buffer_get_stats(net_buffer_stats_t* out_stats);
int net_buffer_restore_stats(const net_buffer_stats_t* saved_stats);
int net_buffer_validate_state(void);
int net_buffer_self_test(void);

#endif
