#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/net_socket.h"
#include "core/sk_buff.h"
#include "core/socket.h"
#include "core/string.h"
#include "core/wait.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_FILE_CAPACITY VFS_MAX_FDS
#define FAKE_SKB_CAPACITY 32U
#define FAKE_NET_CAPACITY NET_SOCKET_CAPACITY
#define SOCKET_TEST_IP 0x0A00020FU
#define SOCKET_TEST_PORT 8080U
#define SOCKET_TEST_HANDLE_BASE 0xA500U
#define SOCKET_TEST_PATH "socket-host"

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
    printf("ZCOV_BEGIN|case=host:network:socket-runtime|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:socket-runtime|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:socket-runtime|value=0x%08X\n",
           (uint32_t)result);
}

typedef struct {
    uint8_t used;
    vnode_t vnode;
    file_t file;
} fake_file_slot_t;

typedef struct {
    uint8_t used;
    sk_buff_t skb;
    uint8_t storage[SK_BUFF_STORAGE_SIZE];
    net_buffer_state_t state;
} fake_skb_slot_t;

typedef struct {
    uint8_t used;
    net_socket_handle_t handle;
    net_socket_info_t info;
} fake_net_slot_t;

static fake_file_slot_t fake_files[FAKE_FILE_CAPACITY];
static fake_skb_slot_t fake_skb[FAKE_SKB_CAPACITY];
static fake_net_slot_t fake_net[FAKE_NET_CAPACITY];
static const file_operations_t* fake_socket_operations;
static uint8_t fake_vfs_ready;
static int fake_skb_init_result;
static uint32_t fake_poll_notifications;
static uint32_t fake_wait_calls;
static int fake_wait_result;
static wait_reason_t fake_wait_reason;
static net_socket_status_t fake_net_status;
static int fake_net_connect_result;
static int fake_net_send_result;
static int fake_net_receive_result;
static int fake_net_close_result;
static int fake_net_abort_result;
static uint16_t fake_net_accept;
static uint8_t fake_wait_sets_send_ready;
static uint8_t fake_wait_sets_receive_data;
static net_socket_event_mask_t fake_wait_events;
static net_socket_state_t fake_wait_state;
static int fake_wait_error;
static uint8_t fake_receive_data[64];
static uint16_t fake_receive_length;
static uint16_t fake_receive_offset;
static uint8_t fake_receive_eof;
static uint32_t fake_current_pid = 42U;

static void reset_fake_file(fake_file_slot_t* slot) {
    kmemset(slot, 0, sizeof(*slot));
}

static void reset_fake_net(void) {
    kmemset(fake_net, 0, sizeof(fake_net));
    kmemset(&fake_net_status, 0, sizeof(fake_net_status));
    fake_net_status.initialized = 1U;
    fake_net_connect_result = OK;
    fake_net_send_result = OK;
    fake_net_receive_result = OK;
    fake_net_close_result = OK;
    fake_net_abort_result = OK;
    fake_net_accept = UINT16_MAX;
    fake_wait_sets_send_ready = 0U;
    fake_wait_sets_receive_data = 0U;
    fake_wait_events = NET_SOCKET_EVENT_CONNECTED;
    fake_wait_state = NET_SOCKET_STATE_CONNECTED;
    fake_wait_error = ERR_STATE;
    fake_receive_length = 0U;
    fake_receive_offset = 0U;
    fake_receive_eof = 0U;
}

static void reset_fakes(void) {
    kmemset(fake_files, 0, sizeof(fake_files));
    kmemset(fake_skb, 0, sizeof(fake_skb));
    fake_socket_operations = 0;
    fake_vfs_ready = 1U;
    fake_skb_init_result = OK;
    fake_poll_notifications = 0U;
    fake_wait_calls = 0U;
    fake_wait_result = OK;
    fake_wait_reason = WAIT_REASON_EVENT;
    reset_fake_net();
}

static int check(int condition, const char* name) {
    if (condition) return OK;
    printf("socket-runtime failure: %s\n", name);
    return ERR_STATE;
}

static int check_result(int actual, int expected, const char* name) {
    return check(actual == expected, name);
}

static void copy_text(char* destination, uint32_t capacity,
                      const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (source) {
        while (index + 1U < capacity && source[index]) {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
}

static fake_file_slot_t* fake_file_from_fd(int32_t fd) {
    if (fd < VFS_FD_FIRST_FILE || (uint32_t)fd >= FAKE_FILE_CAPACITY ||
        !fake_files[fd].used) return 0;
    return &fake_files[fd];
}

static fake_skb_slot_t* fake_skb_from_pointer(const sk_buff_t* skb) {
    if (!skb) return 0;
    for (uint32_t index = 0U; index < FAKE_SKB_CAPACITY; index++) {
        if (fake_skb[index].used && &fake_skb[index].skb == skb) {
            return &fake_skb[index];
        }
    }
    return 0;
}

static fake_net_slot_t* fake_net_from_handle(net_socket_handle_t handle) {
    for (uint32_t index = 0U; index < FAKE_NET_CAPACITY; index++) {
        if (fake_net[index].used && fake_net[index].handle == handle) {
            return &fake_net[index];
        }
    }
    return 0;
}

static int invoke_poll(int32_t fd, uint32_t events, uint32_t* revents) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);

    if (!slot || !slot->file.vnode || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->poll) return ERR_INVALID;
    return slot->file.vnode->operations->poll(&slot->file, events, revents);
}

static int invoke_lseek(int32_t fd, int32_t offset, uint32_t whence,
                        uint32_t* position) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);

    if (!slot || !slot->file.vnode || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->lseek) return ERR_INVALID;
    return slot->file.vnode->operations->lseek(&slot->file, offset, whence,
                                               position);
}

static int invoke_ioctl(int32_t fd, uint32_t request, void* argument) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);

    if (!slot || !slot->file.vnode || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->ioctl) return ERR_INVALID;
    return slot->file.vnode->operations->ioctl(&slot->file, request, argument);
}

static int invoke_sync(int32_t fd) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);

    if (!slot || !slot->file.vnode || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->sync) return ERR_INVALID;
    return slot->file.vnode->operations->sync(&slot->file);
}

void log_init(void) {
}

void log_set_level(log_level_t level) {
    (void)level;
}

log_level_t log_get_level(void) {
    return LOG_LEVEL_DEBUG;
}

int log_set_buffer_level(log_level_t level) {
    (void)level;
    return OK;
}

int log_set_console_level(log_level_t level) {
    (void)level;
    return OK;
}

log_level_t log_get_buffer_level(void) {
    return LOG_LEVEL_DEBUG;
}

log_level_t log_get_console_level(void) {
    return LOG_LEVEL_DEBUG;
}

void log_print(log_level_t level, const char* module, const char* msg) {
    (void)level;
    (void)module;
    (void)msg;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* msg) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)msg;
}

void log_to_buffer(log_level_t level, const char* module, const char* msg) {
    log_print(level, module, msg);
}

int log_get_buffer(char* output, int max_size) {
    if (output && max_size > 0) output[0] = '\0';
    return 0;
}

void log_clear_buffer(void) {
}

int log_get_stats(log_stats_t* output) {
    if (!output) return ERR_NULL;
    kmemset(output, 0, sizeof(*output));
    return OK;
}

int log_copy_recent(log_record_t* output, uint32_t max_records,
                    uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (max_records && !output) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int log_self_test(log_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    kmemset(result, 0, sizeof(*result));
    return OK;
}

const char* log_level_str(log_level_t level) {
    if (level == LOG_LEVEL_ERROR) return "ERROR";
    if (level == LOG_LEVEL_WARN) return "WARN";
    if (level == LOG_LEVEL_INFO) return "INFO";
    if (level == LOG_LEVEL_DEBUG) return "DEBUG";
    return "UNKNOWN";
}

int vfs_is_ready(void) {
    return fake_vfs_ready;
}

int vfs_validate_state(void) {
    return fake_vfs_ready ? OK : ERR_STATE;
}

int vfs_open_socket(void* private_data, const file_operations_t* operations,
                    uint32_t mode, const char* path, int32_t* fd_out) {
    int32_t selected = VFS_FD_INVALID;
    fake_file_slot_t* slot;

    if (!private_data || !operations || !fd_out) return ERR_NULL;
    *fd_out = VFS_FD_INVALID;
    for (uint32_t index = VFS_FD_FIRST_FILE; index < FAKE_FILE_CAPACITY;
         index++) {
        if (!fake_files[index].used) {
            selected = (int32_t)index;
            break;
        }
    }
    if (selected < 0) return ERR_OVERFLOW;
    slot = &fake_files[selected];
    kmemset(slot, 0, sizeof(*slot));
    slot->used = 1U;
    slot->vnode.type = VFS_NODE_SOCKET;
    slot->vnode.operations = operations;
    slot->vnode.private_data = private_data;
    copy_text(slot->vnode.path, VFS_MAX_PATH, path);
    slot->file.vnode = &slot->vnode;
    slot->file.mode = mode;
    slot->file.slot = (uint32_t)selected;
    slot->file.used = 1U;
    fake_socket_operations = operations;
    if (operations->open(&slot->vnode, &slot->file) != OK) {
        reset_fake_file(slot);
        return ERR_STATE;
    }
    *fd_out = selected;
    return OK;
}

int vfs_close(int32_t fd) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);
    int result;

    if (!slot || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->close) return ERR_INVALID;
    result = slot->file.vnode->operations->close(&slot->file);
    if (result == OK) reset_fake_file(slot);
    return result;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);

    if (!slot || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->read) return ERR_INVALID;
    return slot->file.vnode->operations->read(&slot->file, buffer, size,
                                              bytes_read);
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    fake_file_slot_t* slot = fake_file_from_fd(fd);

    if (!slot || !slot->file.vnode->operations ||
        !slot->file.vnode->operations->write) return ERR_INVALID;
    return slot->file.vnode->operations->write(&slot->file, buffer, size,
                                               bytes_written);
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    return invoke_lseek(fd, offset, whence, position);
}

int vfs_ioctl(int32_t fd, uint32_t request, void* argument) {
    return invoke_ioctl(fd, request, argument);
}

int vfs_poll_notify(void) {
    fake_poll_notifications++;
    return OK;
}

int init_waitqueue_head(wait_queue_head_t* queue, const char* owner) {
    if (!queue) return ERR_NULL;
    kmemset(queue, 0, sizeof(*queue));
    copy_text(queue->owner, WAIT_CHANNEL_OWNER_LENGTH, owner);
    queue->initialized = 1U;
    queue->available = 1U;
    return OK;
}

int wait_channel_set_available(wait_channel_t* channel, uint8_t available) {
    if (!channel || !channel->initialized) return ERR_STATE;
    channel->available = available ? 1U : 0U;
    return OK;
}

int wait_channel_reset(wait_channel_t* channel) {
    if (!channel) return ERR_NULL;
    channel->first = 0;
    channel->last = 0;
    channel->waiters = 0U;
    channel->initialized = 0U;
    return OK;
}

int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken) {
    if (!queue || !out_woken) return ERR_NULL;
    *out_woken = queue->waiters;
    return OK;
}

int wake_up(wait_queue_head_t* queue, uint32_t* out_woken) {
    return wake_up_all(queue, out_woken);
}

int wait_event(wait_queue_head_t* queue, wait_condition_fn_t condition,
               void* context, wait_reason_t* out_reason) {
    uint8_t ready = 0U;

    if (!queue || !out_reason) return ERR_NULL;
    fake_wait_calls++;
    if (condition && condition(context, &ready) != OK) return ERR_STATE;
    (void)ready;
    *out_reason = fake_wait_reason;
    return fake_wait_result;
}

int wait_queue_entry_init(wait_queue_entry_t* entry, void* target,
                          const char* target_name,
                          wait_target_type_t target_type, uint32_t target_id,
                          wait_queue_transition_fn_t block,
                          wait_queue_transition_fn_t wake,
                          wait_queue_yield_fn_t yield) {
    if (!entry || !block || !wake) return ERR_NULL;
    kmemset(entry, 0, sizeof(*entry));
    entry->target = target;
    entry->target_name = target_name;
    entry->target_type = target_type;
    entry->target_id = target_id;
    entry->block = block;
    entry->wake = wake;
    entry->yield = yield;
    return OK;
}

int wait_queue_block(wait_queue_head_t* queue, wait_queue_entry_t* entry,
                     uint32_t observed_condition, uint32_t timeout_ticks,
                     wait_reason_t* out_reason) {
    if (!queue || !entry || !out_reason) return ERR_NULL;
    entry->queue = queue;
    entry->observed_condition = observed_condition;
    entry->deadline_tick = timeout_ticks;
    entry->linked = 1U;
    queue->first = entry;
    queue->last = entry;
    queue->waiters = 1U;
    if (entry->block) entry->block(entry->target, entry);
    entry->reason = WAIT_REASON_EVENT;
    *out_reason = entry->reason;
    return OK;
}

int wait_queue_remove(wait_queue_entry_t* entry, wait_reason_t reason) {
    if (!entry || !entry->linked) return ERR_INVALID;
    entry->linked = 0U;
    entry->reason = reason;
    if (entry->queue) {
        entry->queue->first = 0;
        entry->queue->last = 0;
        entry->queue->waiters = 0U;
    }
    if (entry->wake) entry->wake(entry->target, entry);
    return OK;
}

sk_buff_t* alloc_skb(uint32_t size) {
    if (!size || size > SK_BUFF_STORAGE_SIZE) return 0;
    for (uint32_t index = 0U; index < FAKE_SKB_CAPACITY; index++) {
        fake_skb_slot_t* slot = &fake_skb[index];

        if (!slot->used) {
            kmemset(slot, 0, sizeof(*slot));
            slot->used = 1U;
            slot->skb.head = slot->storage;
            slot->skb.data = slot->storage;
            slot->skb.tail = slot->storage;
            slot->skb.end = slot->storage + size;
            slot->state = NET_BUFFER_STATE_ALLOCATED;
            return &slot->skb;
        }
    }
    return 0;
}

void free_skb(sk_buff_t* skb) {
    (void)skb_release(skb);
}

void* skb_put(sk_buff_t* skb, uint32_t length) {
    fake_skb_slot_t* slot = fake_skb_from_pointer(skb);

    if (!slot || length > (uint32_t)(skb->end - skb->tail)) return 0;
    skb->tail += length;
    skb->len += length;
    return skb->tail - length;
}

void* skb_push(sk_buff_t* skb, uint32_t length) {
    fake_skb_slot_t* slot = fake_skb_from_pointer(skb);

    if (!slot || length > (uint32_t)(skb->data - skb->head)) return 0;
    skb->data -= length;
    skb->len += length;
    return skb->data;
}

void* skb_pull(sk_buff_t* skb, uint32_t length) {
    fake_skb_slot_t* slot = fake_skb_from_pointer(skb);

    if (!slot || length > skb->len) return 0;
    skb->data += length;
    skb->len -= length;
    return skb->data;
}

int skb_init(void) {
    return fake_skb_init_result;
}

int skb_transition(sk_buff_t* skb, net_buffer_state_t state,
                   net_buffer_owner_t owner) {
    fake_skb_slot_t* slot = fake_skb_from_pointer(skb);

    (void)owner;
    if (!slot) return ERR_INVALID;
    slot->state = state;
    return OK;
}

int skb_complete(sk_buff_t* skb, int result,
                 net_buffer_owner_t delivered_owner) {
    fake_skb_slot_t* slot = fake_skb_from_pointer(skb);

    (void)delivered_owner;
    if (!slot) return ERR_INVALID;
    slot->state = result == OK ? NET_BUFFER_STATE_DELIVERED :
                 NET_BUFFER_STATE_DROPPED;
    return OK;
}

int skb_retain(sk_buff_t* skb) {
    return fake_skb_from_pointer(skb) ? OK : ERR_INVALID;
}

int skb_release(sk_buff_t* skb) {
    fake_skb_slot_t* slot = fake_skb_from_pointer(skb);

    if (!slot) return ERR_INVALID;
    slot->used = 0U;
    return OK;
}

int skb_get_stats(sk_buff_stats_t* out_stats) {
    uint32_t active = 0U;

    if (!out_stats) return ERR_NULL;
    for (uint32_t index = 0U; index < FAKE_SKB_CAPACITY; index++) {
        if (fake_skb[index].used) active++;
    }
    kmemset(out_stats, 0, sizeof(*out_stats));
    out_stats->initialized = 1U;
    out_stats->active_buffers = active;
    return OK;
}

int skb_validate_state(void) {
    return OK;
}

int skb_self_test(void) {
    return OK;
}

int net_buffer_note_copy(uint32_t bytes) {
    (void)bytes;
    return OK;
}

uint32_t process_get_current_pid(void) {
    return fake_current_pid;
}

uint8_t ipv4_address_is_unicast(uint32_t address) {
    uint8_t first = (uint8_t)(address >> 24U);

    return (uint8_t)(address && address != 0xFFFFFFFFU &&
                     first < 224U && first != 127U);
}

int net_socket_init(void) {
    fake_net_status.initialized = 1U;
    return OK;
}

int net_socket_open(net_socket_type_t type,
                    net_socket_handle_t* out_handle) {
    if (!out_handle) return ERR_NULL;
    if (type != NET_SOCKET_TYPE_STREAM) return ERR_UNAVAILABLE;
    for (uint32_t index = 0U; index < FAKE_NET_CAPACITY; index++) {
        fake_net_slot_t* slot = &fake_net[index];

        if (!slot->used) {
            kmemset(slot, 0, sizeof(*slot));
            slot->used = 1U;
            slot->handle = SOCKET_TEST_HANDLE_BASE + index + 1U;
            slot->info.used = 1U;
            slot->info.handle = slot->handle;
            slot->info.type = type;
            slot->info.state = NET_SOCKET_STATE_OPEN;
            fake_net_status.active_count++;
            fake_net_status.opens++;
            *out_handle = slot->handle;
            return OK;
        }
    }
    return ERR_OVERFLOW;
}

int net_socket_connect(net_socket_handle_t handle, uint32_t remote_ip,
                       uint16_t remote_port) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);

    if (!slot || !remote_ip || !remote_port) return ERR_INVALID;
    if (fake_net_connect_result != OK) return fake_net_connect_result;
    slot->info.remote_ip = remote_ip;
    slot->info.remote_port = remote_port;
    slot->info.local_port = 49152U;
    slot->info.state = NET_SOCKET_STATE_CONNECTING;
    fake_net_status.connects++;
    return OK;
}

int net_socket_wait(net_socket_handle_t handle,
                    net_socket_event_mask_t events, uint32_t timeout_ticks,
                    net_socket_event_mask_t* out_events,
                    wait_reason_t* out_reason) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);

    (void)timeout_ticks;
    if (!slot || !out_events || !out_reason) return ERR_NULL;
    *out_events = fake_wait_events & events;
    *out_reason = fake_wait_reason;
    if (fake_wait_sets_send_ready) {
        fake_net_accept = UINT16_MAX;
        fake_wait_sets_send_ready = 0U;
    }
    if (fake_wait_sets_receive_data) {
        fake_receive_data[0] = 'o';
        fake_receive_data[1] = 'k';
        fake_receive_length = 2U;
        fake_receive_offset = 0U;
        fake_wait_sets_receive_data = 0U;
    }
    if (fake_wait_reason == WAIT_REASON_CANCELLED ||
        fake_wait_reason == WAIT_REASON_SIGNAL) return OK;
    if (fake_wait_error != OK &&
        (fake_wait_events & NET_SOCKET_EVENT_ERROR) != 0U) {
        slot->info.state = NET_SOCKET_STATE_ERROR;
        slot->info.last_error = fake_wait_error;
    } else {
        slot->info.state = fake_wait_state;
        slot->info.last_error = OK;
    }
    fake_net_status.wait_calls++;
    return OK;
}

int net_socket_send(net_socket_handle_t handle, const uint8_t* data,
                    uint16_t length, uint16_t* out_written) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);
    uint16_t accepted;

    if (!slot || !out_written || (length && !data)) return ERR_NULL;
    *out_written = 0U;
    if (fake_net_send_result != OK) return fake_net_send_result;
    accepted = fake_net_accept < length ? fake_net_accept : length;
    *out_written = accepted;
    slot->info.tx_queued = 0U;
    return OK;
}

int net_socket_receive(net_socket_handle_t handle, uint8_t* buffer,
                       uint16_t capacity, uint16_t* out_read,
                       uint8_t* out_eof) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);
    uint16_t available;
    uint16_t read;

    if (!slot || !out_read || !out_eof || (capacity && !buffer)) {
        return ERR_NULL;
    }
    *out_read = 0U;
    *out_eof = 0U;
    if (fake_net_receive_result != OK) return fake_net_receive_result;
    available = fake_receive_length - fake_receive_offset;
    read = available < capacity ? available : capacity;
    if (read) {
        kmemcpy(buffer, fake_receive_data + fake_receive_offset, read);
        fake_receive_offset += read;
        *out_read = read;
    } else if (fake_receive_eof) {
        *out_eof = 1U;
    }
    slot->info.rx_queued = available - read;
    slot->info.eof = *out_eof;
    return OK;
}

int net_socket_close(net_socket_handle_t handle) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);

    if (!slot) return ERR_INVALID;
    if (fake_net_close_result != OK) return fake_net_close_result;
    slot->info.state = NET_SOCKET_STATE_CLOSING;
    fake_net_status.closes++;
    return OK;
}

int net_socket_abort(net_socket_handle_t handle) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);

    if (!slot) return ERR_INVALID;
    if (fake_net_abort_result != OK) return fake_net_abort_result;
    slot->used = 0U;
    if (fake_net_status.active_count) fake_net_status.active_count--;
    fake_net_status.aborts++;
    return OK;
}

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_net_status;
    return OK;
}

int net_socket_get_handle_info(net_socket_handle_t handle,
                               net_socket_info_t* out_info) {
    fake_net_slot_t* slot = fake_net_from_handle(handle);

    if (!slot || !out_info) return ERR_INVALID;
    *out_info = slot->info;
    return OK;
}

int net_socket_validate_state(void) {
    return OK;
}

int test_tcp_and_vfs(void) {
    socket_address_t address;
    socket_status_t before;
    socket_status_t after;
    uint8_t buffer[8];
    uint32_t count = 0U;
    uint32_t revents = 0U;
    uint32_t position = 99U;
    int32_t fd = VFS_FD_INVALID;
    int result;

    kmemset(&address, 0, sizeof(address));
    address.family = SOCKET_FAMILY_INET;
    address.value.ipv4.address = SOCKET_TEST_IP;
    address.value.ipv4.port = SOCKET_TEST_PORT;
    if (socket_create(SOCKET_FAMILY_INET, SOCKET_TYPE_STREAM, 0U, &fd) != OK) {
        return ERR_STATE;
    }
    if (socket_bind(fd, &address) != ERR_UNAVAILABLE ||
        socket_listen(fd, 1U) != ERR_UNAVAILABLE ||
        socket_accept(fd, &fd) != ERR_UNAVAILABLE) return ERR_STATE;
    if (socket_connect(fd, &address) != OK) return ERR_STATE;
    if (invoke_poll(fd, POLLIN | POLLOUT, &revents) != OK ||
        !(revents & POLLOUT)) return ERR_STATE;
    if (socket_send(fd, 0, 1U, &count) != ERR_NULL || count != 0U) {
        return ERR_STATE;
    }
    fake_net_accept = 3U;
    if (socket_send(fd, (const uint8_t*)"abcd", 4U, &count) != OK ||
        count != 4U) return ERR_STATE;
    fake_net_accept = 0U;
    if (socket_set_nonblocking(fd, 1U) != OK ||
        socket_send(fd, (const uint8_t*)"x", 1U, &count) != ERR_AGAIN ||
        count != 0U) return ERR_STATE;
    if (socket_set_nonblocking(fd, 0U) != OK) return ERR_STATE;
    fake_wait_events = NET_SOCKET_EVENT_WRITABLE;
    fake_wait_state = NET_SOCKET_STATE_CONNECTED;
    fake_wait_sets_send_ready = 1U;
    if (socket_send(fd, (const uint8_t*)"x", 1U, &count) != OK ||
        count != 1U) return ERR_STATE;
    fake_receive_data[0] = 'a';
    fake_receive_data[1] = 'b';
    fake_receive_length = 2U;
    fake_receive_offset = 0U;
    if (socket_recv(fd, buffer, sizeof(buffer), &count, &buffer[7]) != OK ||
        count != 2U || buffer[0] != 'a' || buffer[1] != 'b') return ERR_STATE;
    fake_receive_length = 0U;
    fake_receive_offset = 0U;
    fake_wait_events = NET_SOCKET_EVENT_READABLE;
    fake_wait_state = NET_SOCKET_STATE_CONNECTED;
    fake_wait_sets_receive_data = 1U;
    if (socket_recv(fd, buffer, sizeof(buffer), &count, &buffer[7]) != OK ||
        count != 2U || buffer[0] != 'o' || buffer[1] != 'k') return ERR_STATE;
    fake_receive_length = 0U;
    fake_receive_offset = 0U;
    fake_receive_eof = 1U;
    fake_wait_events = NET_SOCKET_EVENT_EOF;
    fake_wait_state = NET_SOCKET_STATE_EOF;
    result = socket_recv(fd, buffer, 1U, &count, &buffer[7]);
    if (result != OK || count != 0U || !buffer[7]) return ERR_STATE;
    fake_receive_eof = 0U;
    fake_net_send_result = ERR_DISK;
    if (socket_send(fd, (const uint8_t*)"x", 1U, &count) != ERR_DISK) {
        return ERR_STATE;
    }
    fake_net_send_result = OK;
    fake_net_receive_result = ERR_DISK;
    if (socket_recv(fd, buffer, 1U, &count, &buffer[7]) != ERR_DISK) {
        return ERR_STATE;
    }
    fake_net_receive_result = OK;
    if (vfs_write(fd, "xy", 2U, &count) != OK || count != 2U) {
        return ERR_STATE;
    }
    fake_receive_data[0] = 'z';
    fake_receive_length = 1U;
    fake_receive_offset = 0U;
    if (vfs_read(fd, buffer, 1U, &count) != OK || count != 1U ||
        buffer[0] != 'z') return ERR_STATE;
    if (invoke_lseek(fd, 0, VFS_SEEK_SET, &position) != ERR_UNAVAILABLE ||
        position != 0U || invoke_ioctl(fd, 0U, 0) != ERR_UNAVAILABLE ||
        invoke_sync(fd) != ERR_UNAVAILABLE) return ERR_STATE;
    if (!fake_socket_operations ||
        fake_socket_operations->open(0, 0) != ERR_NULL ||
        fake_socket_operations->read(0, buffer, 1U, &count) != ERR_NULL ||
        fake_socket_operations->write(0, buffer, 1U, &count) != ERR_NULL ||
        fake_socket_operations->close(0) != ERR_NULL) return ERR_STATE;
    if (socket_get_status(&before) != OK) return ERR_STATE;
    result = socket_close(fd);
    if (result != OK) return ERR_STATE;
    if (socket_get_status(&after) != OK || after.active_count != 0U ||
        after.closes <= before.closes) return ERR_STATE;
    return socket_validate_state();
}

int test_unix_and_wait(void) {
    socket_address_t address;
    int32_t listener = VFS_FD_INVALID;
    int32_t client = VFS_FD_INVALID;
    int32_t abandoned_client = VFS_FD_INVALID;
    int32_t server = VFS_FD_INVALID;
    uint8_t buffer[8];
    uint32_t count = 0U;
    uint8_t eof = 0U;
    uint32_t revents = 0U;

    kmemset(&address, 0, sizeof(address));
    address.family = SOCKET_FAMILY_UNIX;
    copy_text(address.value.local.path, SOCKET_UNIX_PATH_MAX, SOCKET_TEST_PATH);
    if (socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                      SOCKET_FLAG_NONBLOCK, &listener) != OK ||
        socket_bind(listener, &address) != OK ||
        socket_listen(listener, SOCKET_UNIX_BACKLOG_MAX) != OK) {
        return ERR_STATE;
    }
    if (socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                      SOCKET_FLAG_NONBLOCK, &abandoned_client) != OK ||
        socket_connect(abandoned_client, &address) != OK ||
        socket_close(abandoned_client) != OK) {
        return ERR_STATE;
    }
    abandoned_client = VFS_FD_INVALID;
    if (socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                      SOCKET_FLAG_NONBLOCK, &client) != OK ||
        socket_connect(client, &address) != OK ||
        socket_accept(listener, &server) != OK) return ERR_STATE;
    if (invoke_poll(server, POLLIN | POLLOUT, &revents) != OK ||
        !(revents & POLLOUT)) return ERR_STATE;
    if (socket_send(client, (const uint8_t*)"hello", 5U, &count) != OK ||
        count != 5U || socket_recv(server, buffer, sizeof(buffer), &count,
                                   &eof) != OK || count != 5U || eof) {
        return ERR_STATE;
    }
    if (socket_set_nonblocking(server, 0U) != OK) return ERR_STATE;
    fake_wait_reason = WAIT_REASON_CANCELLED;
    if (socket_recv(server, buffer, 1U, &count, &eof) != ERR_CANCELLED) {
        return ERR_STATE;
    }
    fake_wait_reason = WAIT_REASON_EVENT;
    if (socket_close(client) != OK) return ERR_STATE;
    client = VFS_FD_INVALID;
    if (socket_close(server) != OK || socket_close(listener) != OK) {
        return ERR_STATE;
    }
    return socket_validate_state();
}

static int test_invalid_inputs_and_names(void) {
    socket_address_t address;
    socket_info_t info;
    socket_status_t status;
    int32_t fd = VFS_FD_INVALID;

    if (socket_get_status(0) != ERR_NULL || socket_get_info(0, 0) != ERR_NULL ||
        socket_get_info(SOCKET_CAPACITY, &info) != ERR_INVALID ||
        socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM, 0U, 0) !=
            ERR_NULL ||
        socket_create((socket_family_t)99, SOCKET_TYPE_STREAM, 0U, &fd) !=
            ERR_INVALID ||
        socket_create(SOCKET_FAMILY_UNIX, (socket_type_t)99, 0U, &fd) !=
            ERR_INVALID ||
        socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM, 2U, &fd) !=
            ERR_INVALID) return ERR_STATE;
    if (socket_family_name(SOCKET_FAMILY_UNIX)[0] != 'U' ||
        socket_family_name(SOCKET_FAMILY_INET)[0] != 'I' ||
        socket_family_name((socket_family_t)99)[0] != 'U' ||
        socket_state_name(SOCKET_STATE_OPEN)[0] != 'O' ||
        socket_state_name(SOCKET_STATE_CLOSED)[0] != 'C' ||
        socket_state_name((socket_state_t)99)[0] != 'U') return ERR_STATE;
    kmemset(&address, 0, sizeof(address));
    address.family = SOCKET_FAMILY_UNIX;
    kmemset(address.value.local.path, 'x', SOCKET_UNIX_PATH_MAX);
    if (socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                      SOCKET_FLAG_NONBLOCK, &fd) != OK ||
        socket_bind(fd, &address) != ERR_OVERFLOW || socket_close(fd) != OK) {
        return ERR_STATE;
    }
    if (socket_bind(VFS_FD_INVALID, &address) != ERR_INVALID ||
        socket_set_nonblocking(VFS_FD_INVALID, 2U) != ERR_INVALID ||
        socket_get_status(&status) != OK) return ERR_STATE;
    return socket_validate_state();
}

static int test_capacity_and_self_test(void) {
    int32_t fds[SOCKET_CAPACITY];
    int32_t extra = VFS_FD_INVALID;
    socket_self_test_result_t self_test;

    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        fds[index] = VFS_FD_INVALID;
        if (socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                          SOCKET_FLAG_NONBLOCK, &fds[index]) != OK) {
            return ERR_STATE;
        }
    }
    if (socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                      SOCKET_FLAG_NONBLOCK, &extra) != ERR_OVERFLOW) {
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        if (socket_close(fds[index]) != OK) return ERR_STATE;
    }
    if (socket_self_test(&self_test) != OK || self_test.failed != 0U ||
        self_test.passed < 7U || !self_test.invariants) return ERR_STATE;
    return socket_validate_state();
}

int main(void) {
    int result = OK;

    reset_fakes();
    coverage_active = 1U;
    fake_vfs_ready = 0U;
    result = check_result(socket_init(), ERR_UNAVAILABLE,
                          "init without vfs");
    fake_vfs_ready = 1U;
    if (result == OK) {
        fake_skb_init_result = ERR_STATE;
        result = check_result(socket_init(), ERR_STATE, "init without skb");
        fake_skb_init_result = OK;
    }
    if (result == OK) result = check_result(socket_init(), OK, "socket init");
    if (result == OK) result = check_result(socket_init(), OK, "socket reinit");
    if (result == OK) {
        result = test_invalid_inputs_and_names();
    }
    if (result == OK) {
        result = test_tcp_and_vfs();
    }
    if (result == OK) {
        result = test_unix_and_wait();
    }
    if (result == OK) {
        result = test_capacity_and_self_test();
    }
    if (result == OK) result = check(fake_poll_notifications > 0U,
                                     "poll notifications");
    if (result == OK) result = check(fake_wait_calls > 0U, "wait calls");
    coverage_active = 0U;
    coverage_emit(result);
    if (result == OK) {
        printf("socket-runtime-host: PASS\n");
        return 0;
    }
    printf("socket-runtime-host: FAIL (%d)\n", result);
    return 1;
}
