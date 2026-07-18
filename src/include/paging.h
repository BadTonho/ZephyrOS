#ifndef PAGING_H
#define PAGING_H

#include "types.h"

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
    page_table_t* tables[1024];
    uint32_t physical_addr;
} __attribute__((packed)) page_directory_t;

void paging_init(void);
void paging_switch_directory(page_directory_t* dir);
page_entry_t* paging_get_page(uint32_t virtual_addr, int create);
void paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags);

page_directory_t* paging_create_directory(void);
void paging_free_directory(page_directory_t* dir);

#endif
