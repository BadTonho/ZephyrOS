#include "memory/paging.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/log.h"

static page_directory_t* current_directory = 0;

static void* memset(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
    return dst;
}

page_directory_t* paging_create_directory(void) {
    page_directory_t* dir = (page_directory_t*)pmm_alloc_page();
    if (!dir) {
        LOG_ERROR("MEM", "Falha ao alocar diretorio de paginas");
        return 0;
    }
    memset(dir, 0, sizeof(page_directory_t));
    return dir;
}

page_entry_t* paging_get_page(uint32_t virtual_addr, int create) {
    if (!current_directory) {
        LOG_ERROR("MEM", "Diretorio de paginas atual inexistente");
        return 0;
    }

    uint32_t table_idx = virtual_addr / (PAGE_SIZE * 1024);
    uint32_t page_idx = (virtual_addr / PAGE_SIZE) % 1024;
    uint32_t directory_entry = current_directory->entries[table_idx];

    if (directory_entry & 0x01) {
        page_table_t* table = (page_table_t*)(directory_entry & 0xFFFFF000);
        return &table->entries[page_idx];
    }

    if (!create) return 0;

    page_table_t* table = (page_table_t*)pmm_alloc_page();
    if (!table) {
        LOG_ERROR("MEM", "Falha ao alocar tabela de paginas");
        return 0;
    }
    memset(table, 0, sizeof(page_table_t));

    current_directory->entries[table_idx] = (uint32_t)table | 0x03;

    return &table->entries[page_idx];
}

void paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags) {
    page_entry_t* page = paging_get_page(virtual, 1);
    if (!page) {
        panic("Falha ao mapear pagina!");
        return;
    }
    page->frame = physical / PAGE_SIZE;
    page->present = 1;
    page->rw = (flags & 0x2) ? 1 : 0;
    page->user = (flags & 0x4) ? 1 : 0;
}

static void paging_invalidate(uint32_t virtual_addr) {
    asm volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

void paging_switch_directory(page_directory_t* dir) {
    if (!dir) {
        LOG_ERROR("MEM", "Tentativa de trocar para diretorio nulo");
        return;
    }

    current_directory = dir;
    asm volatile("mov %0, %%cr3" : : "r"(dir) : "memory");

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

void paging_init(void) {
    page_directory_t* dir = paging_create_directory();
    if (!dir) {
        panic("Falha ao criar diretorio de paginas!");
        return;
    }

    current_directory = dir;

    for (uint32_t i = KERNEL_START; i < HEAP_START + HEAP_SIZE; i += PAGE_SIZE) {
        paging_map_page(i, i, 0x03);
    }

    for (uint32_t i = 0xB8000; i < 0xC0000; i += PAGE_SIZE) {
        paging_map_page(i, i, 0x03);
    }

    paging_switch_directory(dir);
}

void paging_free_directory(page_directory_t* dir) {
    if (!dir) return;

    for (int i = 0; i < 1024; i++) {
        if (dir->entries[i] & 0x01) {
            pmm_free_page((void*)(dir->entries[i] & 0xFFFFF000));
        }
    }
    pmm_free_page(dir);
}
