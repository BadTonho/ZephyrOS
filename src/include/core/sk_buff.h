#ifndef SK_BUFF_H
#define SK_BUFF_H

#include "types.h"
#include "core/net_buffer.h"

#define SK_BUFF_STORAGE_SIZE 2048U

typedef struct net_device net_device_t;

typedef struct {
    uint8_t* head;
    uint8_t* data;
    uint8_t* tail;
    uint8_t* end;
    uint32_t len;
    uint32_t refcount;
    net_device_t* dev;
} sk_buff_t;

typedef struct {
    uint8_t initialized;
    uint32_t active_buffers;
    uint32_t peak_buffers;
    uint32_t allocations;
    uint32_t frees;
    uint32_t completions;
    uint32_t drops;
    uint32_t invalid_operations;
    int last_error;
} sk_buff_stats_t;

int skb_init(void);
sk_buff_t* alloc_skb(uint32_t size);
void free_skb(sk_buff_t* skb);
void* skb_put(sk_buff_t* skb, uint32_t len);
void* skb_push(sk_buff_t* skb, uint32_t len);
void* skb_pull(sk_buff_t* skb, uint32_t len);
int skb_transition(sk_buff_t* skb, net_buffer_state_t next_state,
                   net_buffer_owner_t next_owner);
int skb_complete(sk_buff_t* skb, int result,
                 net_buffer_owner_t delivered_owner);
int skb_retain(sk_buff_t* skb);
int skb_release(sk_buff_t* skb);
int skb_get_stats(sk_buff_stats_t* out_stats);
int skb_validate_state(void);
int skb_self_test(void);

#endif
