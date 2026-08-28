#ifndef SLAB_H
#define SLAB_H

#include "types.h"

#define KMEM_CACHE_NAME_LENGTH 32U
#define KMEM_CACHE_MAX 16U
#define KMEM_SLAB_MAX 128U
#define KMEM_SLAB_MAX_OBJECTS 128U
#define KMEM_SLAB_MIN_OBJECTS 8U

typedef struct kmem_cache kmem_cache_t;

typedef struct {
    char name[KMEM_CACHE_NAME_LENGTH];
    uint32_t object_size;
    uint32_t alignment;
    uint32_t object_stride;
    uint32_t objects_per_slab;
    uint32_t active_objects;
    uint32_t capacity;
    uint32_t slabs;
    uint32_t pages;
    uint32_t allocation_failures;
    uint32_t invalid_frees;
    uint32_t double_frees;
    uint8_t initialized;
} kmem_cache_info_t;

typedef struct {
    uint32_t caches;
    uint32_t slabs;
    uint32_t pages;
    uint32_t active_objects;
    uint32_t capacity;
    uint32_t allocation_failures;
    uint32_t invalid_frees;
    uint32_t double_frees;
    uint8_t initialized;
    uint8_t valid;
} kmem_slab_stats_t;

int kmem_cache_init(void);
kmem_cache_t* kmem_cache_create(const char* name, uint32_t object_size,
                                uint32_t alignment);
void* kmem_cache_alloc(kmem_cache_t* cache);
void kmem_cache_free(kmem_cache_t* cache, void* object);
int kmem_cache_destroy(kmem_cache_t* cache);
int kmem_cache_get_info(const kmem_cache_t* cache,
                        kmem_cache_info_t* info);
int kmem_cache_get_info_at(uint32_t index, kmem_cache_info_t* info);
uint32_t kmem_cache_get_count(void);
int kmem_cache_owns(const kmem_cache_t* cache, const void* object);
int kmem_cache_validate(void);
void kmem_cache_get_stats(kmem_slab_stats_t* stats);
int kmem_cache_self_test(void);

#endif
