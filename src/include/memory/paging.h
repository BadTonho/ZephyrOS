#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGING_FLAG_PRESENT 0x01U
#define PAGING_FLAG_WRITE   0x02U
#define PAGING_FLAG_USER    0x04U

#define USER_SPACE_START 0x00800000U
#define USER_SPACE_END   0x01000000U
#define USER_CODE_BASE   0x00800000U
#define USER_DATA_BASE   0x00801000U
#define USER_LAUNCH_BASE 0x00802000U
#define USER_STACK_BASE  0x00C00000U
#define USER_STACK_TOP   0x00C01000U

typedef struct {
    uint32_t present : 1;
    uint32_t rw : 1;
    uint32_t user : 1;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t unused : 7;
    uint32_t frame : 20;
} __attribute__((packed)) page_entry_t;

typedef struct {
    page_entry_t entries[1024];
} __attribute__((aligned(4096))) page_table_t;

typedef struct {
    uint32_t entries[1024];
} __attribute__((aligned(4096))) page_directory_t;

int paging_init(void);
int paging_is_ready(void);
page_directory_t* paging_get_current_directory(void);
void paging_switch_directory(page_directory_t* dir);
page_entry_t* paging_get_page(uint32_t virtual_addr, int create);
page_entry_t* paging_get_page_in_directory(page_directory_t* dir,
                                           uint32_t virtual_addr,
                                           int create);
int paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags);
int paging_map_page_in_directory(page_directory_t* dir,
                                 uint32_t virtual,
                                 uint32_t physical,
                                 uint32_t flags);

page_directory_t* paging_create_directory(void);
page_directory_t* paging_create_user_directory(void);
void paging_free_directory(page_directory_t* dir);
void paging_free_user_directory(page_directory_t* dir);

int paging_validate_user_range(uint32_t address, uint32_t size, int write);
int paging_copy_from_user(void* destination, const void* source,
                          uint32_t size);
int paging_copy_to_user(void* destination, const void* source,
                        uint32_t size);

#endif
