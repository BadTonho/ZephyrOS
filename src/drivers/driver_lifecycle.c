#include "driver_lifecycle_internal.h"
#include "core/errors.h"

#define Q3CHECK_ERROR_PROPAGATION_ONLY 1

#define DRIVER_LIFECYCLE_TEXT_EMPTY ""

typedef struct {
    uint8_t used;
    char driver_id[DRIVER_LIFECYCLE_ID_SIZE];
    char parent_id[DRIVER_LIFECYCLE_PARENT_SIZE];
    char bus[DRIVER_LIFECYCLE_BUS_SIZE];
    char class_name[DRIVER_LIFECYCLE_CLASS_SIZE];
    char identity[DRIVER_LIFECYCLE_IDENTITY_SIZE];
    driver_lifecycle_state_t state;
    uint32_t generation;
    uint32_t required_resources;
    uint32_t owned_resources;
    uint32_t resource_flags;
    driver_lifecycle_resource_ids_t resource_ids;
    int last_error;
    char reason[DRIVER_LIFECYCLE_REASON_SIZE];
} driver_lifecycle_entry_t;

static driver_lifecycle_entry_t entries[DRIVER_LIFECYCLE_MAX];
static uint32_t entry_count;
static uint8_t lifecycle_initialized;

static uint32_t lifecycle_next_generation(uint32_t generation) {
    generation++;
    return generation ? generation : 1U;
}

static uint32_t lifecycle_copy_text(char* destination, uint32_t capacity,
                                    const char* source) {
    uint32_t length = 0U;

    if (!destination || !capacity) return 0U;
    if (!source) source = DRIVER_LIFECYCLE_TEXT_EMPTY;
    while (length + 1U < capacity && source[length]) {
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
    return length;
}

static int lifecycle_text_equal(const char* left, const char* right) {
    uint32_t index = 0U;

    if (!left || !right) return left == right;
    while (left[index] && right[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static void lifecycle_clear_entry(driver_lifecycle_entry_t* entry) {
    uint32_t index;

    if (!entry) return;
    entry->used = 0U;
    entry->state = DRIVER_LIFECYCLE_UNREGISTERED;
    entry->required_resources = 0U;
    entry->owned_resources = 0U;
    entry->resource_flags = 0U;
    entry->resource_ids.irq = 0U;
    entry->resource_ids.io = 0U;
    entry->resource_ids.mmio = 0U;
    entry->resource_ids.dma = 0U;
    entry->resource_ids.buffer = 0U;
    entry->resource_ids.callback = 0U;
    entry->resource_ids.work = 0U;
    entry->last_error = OK;
    for (index = 0U; index < DRIVER_LIFECYCLE_ID_SIZE; index++) {
        entry->driver_id[index] = '\0';
    }
    for (index = 0U; index < DRIVER_LIFECYCLE_PARENT_SIZE; index++) {
        entry->parent_id[index] = '\0';
    }
    for (index = 0U; index < DRIVER_LIFECYCLE_BUS_SIZE; index++) {
        entry->bus[index] = '\0';
    }
    for (index = 0U; index < DRIVER_LIFECYCLE_CLASS_SIZE; index++) {
        entry->class_name[index] = '\0';
    }
    for (index = 0U; index < DRIVER_LIFECYCLE_IDENTITY_SIZE; index++) {
        entry->identity[index] = '\0';
    }
    for (index = 0U; index < DRIVER_LIFECYCLE_REASON_SIZE; index++) {
        entry->reason[index] = '\0';
    }
}

static void lifecycle_ensure_initialized(void) {
    if (!lifecycle_initialized) driver_lifecycle_init();
}

void driver_lifecycle_init(void) {
    uint32_t index;

    if (lifecycle_initialized) return;
    for (index = 0U; index < DRIVER_LIFECYCLE_MAX; index++) {
        lifecycle_clear_entry(&entries[index]);
    }
    entry_count = 0U;
    lifecycle_initialized = 1U;
}

static driver_lifecycle_entry_t* lifecycle_find(const char* driver_id) {
    uint32_t index;

    if (!driver_id) return 0;
    for (index = 0U; index < DRIVER_LIFECYCLE_MAX; index++) {
        if (entries[index].used &&
            lifecycle_text_equal(entries[index].driver_id, driver_id)) {
            return &entries[index];
        }
    }
    return 0;
}

static driver_lifecycle_entry_t* lifecycle_find_generation(
    const char* driver_id, uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find(driver_id);

    if (!entry || entry->generation != generation) return 0;
    return entry;
}

static driver_lifecycle_entry_t* lifecycle_allocate(void) {
    uint32_t index;

    for (index = 0U; index < DRIVER_LIFECYCLE_MAX; index++) {
        if (!entries[index].used) return &entries[index];
    }
    return 0;
}

static uint32_t lifecycle_resource_id(const driver_lifecycle_entry_t* entry,
                                      uint32_t resource) {
    if (!entry) return 0U;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_IRQ) return entry->resource_ids.irq;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_IO) return entry->resource_ids.io;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_MMIO) return entry->resource_ids.mmio;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_DMA) return entry->resource_ids.dma;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_BUFFER) return entry->resource_ids.buffer;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_CALLBACK) return entry->resource_ids.callback;
    if (resource == DRIVER_LIFECYCLE_RESOURCE_WORK) return entry->resource_ids.work;
    return 0U;
}

static int lifecycle_shared_irq_allowed(const driver_lifecycle_entry_t* owner,
                                        const driver_lifecycle_entry_t* other) {
    return owner && other &&
           (owner->resource_flags & DRIVER_LIFECYCLE_SHARED_IRQ) &&
           (other->resource_flags & DRIVER_LIFECYCLE_SHARED_IRQ);
}

static int lifecycle_resource_conflict(const driver_lifecycle_entry_t* entry,
                                       uint32_t resource) {
    uint32_t index;
    uint32_t resource_id = lifecycle_resource_id(entry, resource);

    if (!resource_id) return 0;
    for (index = 0U; index < DRIVER_LIFECYCLE_MAX; index++) {
        driver_lifecycle_entry_t* other = &entries[index];
        if (!other->used || other == entry ||
            !(other->owned_resources & resource) ||
            lifecycle_resource_id(other, resource) != resource_id) continue;
        if (resource == DRIVER_LIFECYCLE_RESOURCE_IRQ &&
            lifecycle_shared_irq_allowed(entry, other)) continue;
        return 1;
    }
    return 0;
}

static int lifecycle_transition(driver_lifecycle_entry_t* entry,
                                driver_lifecycle_state_t expected,
                                driver_lifecycle_state_t next) {
    if (!entry || entry->state != expected) return ERR_STATE;
    entry->state = next;
    return OK;
}

int driver_lifecycle_begin_probe(const char* driver_id,
                                 const char* parent_id,
                                 const char* bus,
                                 const char* class_name,
                                 const char* identity,
                                 uint32_t required_resources,
                                 const driver_lifecycle_resource_ids_t* resource_ids,
                                 uint32_t resource_flags,
                                 uint32_t* generation_out) {
    driver_lifecycle_entry_t* entry;
    uint32_t generation;

    lifecycle_ensure_initialized();
    if (!driver_id || !driver_id[0] || !identity || !identity[0] ||
        !generation_out) return ERR_NULL;
    entry = lifecycle_find(driver_id);
    if (entry && entry->used &&
        entry->state != DRIVER_LIFECYCLE_FAILED &&
        entry->state != DRIVER_LIFECYCLE_STOPPED) {
        if (!lifecycle_text_equal(entry->identity, identity)) return ERR_STATE;
        if (entry->state == DRIVER_LIFECYCLE_READY ||
            entry->state == DRIVER_LIFECYCLE_DEGRADED) {
            *generation_out = entry->generation;
            return OK;
        }
        return ERR_STATE;
    }
    if (!entry) {
        entry = lifecycle_allocate();
        if (!entry) return ERR_OVERFLOW;
        generation = 1U;
        entry_count++;
    } else {
        generation = lifecycle_next_generation(entry->generation);
        lifecycle_clear_entry(entry);
    }
    entry->used = 1U;
    entry->generation = generation;
    entry->state = DRIVER_LIFECYCLE_PROBING;
    entry->required_resources = required_resources;
    entry->resource_flags = resource_flags;
    lifecycle_copy_text(entry->driver_id, DRIVER_LIFECYCLE_ID_SIZE, driver_id);
    lifecycle_copy_text(entry->parent_id, DRIVER_LIFECYCLE_PARENT_SIZE, parent_id);
    lifecycle_copy_text(entry->bus, DRIVER_LIFECYCLE_BUS_SIZE, bus);
    lifecycle_copy_text(entry->class_name, DRIVER_LIFECYCLE_CLASS_SIZE, class_name);
    lifecycle_copy_text(entry->identity, DRIVER_LIFECYCLE_IDENTITY_SIZE, identity);
    if (resource_ids) entry->resource_ids = *resource_ids;
    entry->last_error = OK;
    lifecycle_copy_text(entry->reason, DRIVER_LIFECYCLE_REASON_SIZE, "probing");
    *generation_out = generation;
    return OK;
}

int driver_lifecycle_begin_reset(const char* driver_id, uint32_t generation) {
    return lifecycle_transition(lifecycle_find_generation(driver_id, generation),
                                DRIVER_LIFECYCLE_PROBING,
                                DRIVER_LIFECYCLE_RESETTING);
}

int driver_lifecycle_begin_configure(const char* driver_id,
                                     uint32_t generation) {
    return lifecycle_transition(lifecycle_find_generation(driver_id, generation),
                                DRIVER_LIFECYCLE_RESETTING,
                                DRIVER_LIFECYCLE_CONFIGURING);
}

int driver_lifecycle_acquire(const char* driver_id, uint32_t generation,
                             uint32_t resources, uint32_t resource_flags) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);
    uint32_t known_resources = DRIVER_LIFECYCLE_RESOURCE_IRQ |
                               DRIVER_LIFECYCLE_RESOURCE_IO |
                               DRIVER_LIFECYCLE_RESOURCE_MMIO |
                               DRIVER_LIFECYCLE_RESOURCE_DMA |
                               DRIVER_LIFECYCLE_RESOURCE_BUFFER |
                               DRIVER_LIFECYCLE_RESOURCE_CALLBACK |
                               DRIVER_LIFECYCLE_RESOURCE_WORK;
    uint32_t bit;

    if (!entry) return ERR_STATE;
    if (entry->state != DRIVER_LIFECYCLE_CONFIGURING &&
        entry->state != DRIVER_LIFECYCLE_REGISTERED) return ERR_STATE;
    if (!resources || (resources & ~known_resources) ||
        (resources & ~entry->required_resources)) return ERR_INVALID;
    entry->resource_flags |= resource_flags;
    for (bit = DRIVER_LIFECYCLE_RESOURCE_IRQ; bit <=
         DRIVER_LIFECYCLE_RESOURCE_WORK; bit <<= 1U) {
        if (!(resources & bit) || (entry->owned_resources & bit)) continue;
        if (lifecycle_resource_conflict(entry, bit)) return ERR_UNAVAILABLE;
    }
    entry->owned_resources |= resources;
    return OK;
}

int driver_lifecycle_release(const char* driver_id, uint32_t generation,
                             uint32_t resources) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry) return ERR_STATE;
    if (resources & ~entry->owned_resources) return ERR_INVALID;
    entry->owned_resources &= ~resources;
    return OK;
}

int driver_lifecycle_mark_registered(const char* driver_id,
                                     uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry) return ERR_STATE;
    if ((entry->owned_resources & entry->required_resources) !=
        entry->required_resources) return ERR_STATE;
    return lifecycle_transition(entry, DRIVER_LIFECYCLE_CONFIGURING,
                                DRIVER_LIFECYCLE_REGISTERED);
}

int driver_lifecycle_mark_ready(const char* driver_id, uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry) return ERR_STATE;
    if ((entry->owned_resources & entry->required_resources) !=
        entry->required_resources) return ERR_STATE;
    return lifecycle_transition(entry, DRIVER_LIFECYCLE_REGISTERED,
                                DRIVER_LIFECYCLE_READY);
}

static void lifecycle_set_failure(driver_lifecycle_entry_t* entry, int error,
                                  const char* reason) {
    if (!entry) return;
    entry->owned_resources = 0U;
    entry->last_error = error;
    lifecycle_copy_text(entry->reason, DRIVER_LIFECYCLE_REASON_SIZE, reason);
}

int driver_lifecycle_mark_degraded(const char* driver_id, uint32_t generation,
                                   int error, const char* reason) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry || entry->state == DRIVER_LIFECYCLE_UNREGISTERED ||
        entry->state == DRIVER_LIFECYCLE_STOPPED) return ERR_STATE;
    lifecycle_set_failure(entry, error, reason);
    entry->state = DRIVER_LIFECYCLE_DEGRADED;
    return OK;
}

int driver_lifecycle_begin_quiesce(const char* driver_id,
                                   uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry) return ERR_STATE;
    if (entry->state != DRIVER_LIFECYCLE_READY &&
        entry->state != DRIVER_LIFECYCLE_DEGRADED) return ERR_STATE;
    return lifecycle_transition(entry, entry->state,
                                DRIVER_LIFECYCLE_QUIESCING);
}

int driver_lifecycle_mark_quiesced(const char* driver_id,
                                   uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry) return ERR_STATE;
    if (entry->state != DRIVER_LIFECYCLE_QUIESCING) return ERR_STATE;
    entry->owned_resources &= ~(DRIVER_LIFECYCLE_RESOURCE_CALLBACK |
                               DRIVER_LIFECYCLE_RESOURCE_WORK);
    entry->state = DRIVER_LIFECYCLE_QUIESCED;
    entry->last_error = OK;
    lifecycle_copy_text(entry->reason, DRIVER_LIFECYCLE_REASON_SIZE, "quiesced");
    return OK;
}

int driver_lifecycle_mark_failed(const char* driver_id, uint32_t generation,
                                 int error, const char* reason) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry || entry->state == DRIVER_LIFECYCLE_UNREGISTERED ||
        entry->state == DRIVER_LIFECYCLE_STOPPED) return ERR_STATE;
    lifecycle_set_failure(entry, error, reason);
    entry->state = DRIVER_LIFECYCLE_FAILED;
    entry->generation = lifecycle_next_generation(entry->generation);
    return OK;
}

int driver_lifecycle_mark_stopped(const char* driver_id, uint32_t generation,
                                  int error, const char* reason) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry || entry->state == DRIVER_LIFECYCLE_UNREGISTERED ||
        entry->state == DRIVER_LIFECYCLE_STOPPED) return ERR_STATE;
    lifecycle_set_failure(entry, error, reason);
    entry->state = DRIVER_LIFECYCLE_STOPPED;
    entry->generation = lifecycle_next_generation(entry->generation);
    return OK;
}

int driver_lifecycle_get_state(const char* driver_id, uint32_t generation,
                               driver_lifecycle_state_t* state_out) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry || !state_out) return ERR_STATE;
    *state_out = entry->state;
    return OK;
}

int driver_lifecycle_validate_ready(const char* driver_id,
                                     uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry) return ERR_STATE;
    if (entry->state != DRIVER_LIFECYCLE_READY &&
        entry->state != DRIVER_LIFECYCLE_DEGRADED) return ERR_UNAVAILABLE;
    if ((entry->owned_resources & entry->required_resources) !=
        entry->required_resources) return ERR_STATE;
    return OK;
}

int driver_lifecycle_validate_callback(const char* driver_id,
                                       uint32_t generation) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (driver_lifecycle_validate_ready(driver_id, generation) != OK) {
        return ERR_STATE;
    }
    if (!entry || !(entry->owned_resources & DRIVER_LIFECYCLE_RESOURCE_CALLBACK)) {
        return ERR_STATE;
    }
    return OK;
}

int driver_lifecycle_snapshot(const char* driver_id, uint32_t generation,
                              driver_lifecycle_snapshot_t* snapshot_out) {
    driver_lifecycle_entry_t* entry = lifecycle_find_generation(driver_id, generation);

    if (!entry || !snapshot_out) return ERR_STATE;
    snapshot_out->state = entry->state;
    snapshot_out->generation = entry->generation;
    snapshot_out->required_resources = entry->required_resources;
    snapshot_out->owned_resources = entry->owned_resources;
    snapshot_out->resource_ids = entry->resource_ids;
    snapshot_out->last_error = entry->last_error;
    lifecycle_copy_text(snapshot_out->reason, DRIVER_LIFECYCLE_REASON_SIZE,
                        entry->reason);
    return OK;
}

int driver_lifecycle_validate_state(void) {
    uint32_t index;
    uint32_t other_index;

    lifecycle_ensure_initialized();
    if (entry_count > DRIVER_LIFECYCLE_MAX) return ERR_STATE;
    for (index = 0U; index < DRIVER_LIFECYCLE_MAX; index++) {
        driver_lifecycle_entry_t* entry = &entries[index];
        if (!entry->used) continue;
        if (!entry->driver_id[0] || !entry->identity[0] || !entry->generation ||
            (entry->owned_resources & ~entry->required_resources)) return ERR_STATE;
        for (other_index = index + 1U; other_index < DRIVER_LIFECYCLE_MAX;
             other_index++) {
            driver_lifecycle_entry_t* other = &entries[other_index];
            if (other->used && lifecycle_text_equal(entry->driver_id,
                                                    other->driver_id)) {
                return ERR_STATE;
            }
        }
    }
    return OK;
}

uint32_t driver_lifecycle_count(void) {
    lifecycle_ensure_initialized();
    return entry_count;
}

int driver_lifecycle_publish(const char* driver_id,
                             const char* parent_id,
                             const char* bus,
                             const char* class_name,
                             const char* identity,
                             uint32_t required_resources,
                             const driver_lifecycle_resource_ids_t* resource_ids,
                             uint32_t resource_flags,
                             int init_result,
                             uint8_t optional,
                             const char* reason) {
    uint32_t generation = 0U;
    driver_lifecycle_state_t state;
    int result;

    result = driver_lifecycle_begin_probe(driver_id, parent_id, bus, class_name,
                                          identity, required_resources,
                                          resource_ids, resource_flags,
                                          &generation);
    if (result != OK) return result;
    if (driver_lifecycle_get_state(driver_id, generation, &state) != OK) {
        return ERR_STATE;
    }
    if (state == DRIVER_LIFECYCLE_READY ||
        state == DRIVER_LIFECYCLE_DEGRADED) return init_result;
    if (init_result != OK) {
        result = optional ? driver_lifecycle_mark_degraded(
            driver_id, generation, init_result, reason) :
            driver_lifecycle_mark_failed(driver_id, generation,
                                         init_result, reason);
        return result == OK ? init_result : result;
    }
    result = driver_lifecycle_begin_reset(driver_id, generation);
    if (result == OK) result = driver_lifecycle_begin_configure(driver_id, generation);
    if (result == OK && required_resources) {
        result = driver_lifecycle_acquire(driver_id, generation,
                                          required_resources, resource_flags);
    }
    if (result == OK) result = driver_lifecycle_mark_registered(driver_id, generation);
    if (result == OK) result = driver_lifecycle_mark_ready(driver_id, generation);
    if (result != OK) {
        (void)driver_lifecycle_mark_failed(driver_id, generation, result,
                                           reason);
    }
    return result;
}
