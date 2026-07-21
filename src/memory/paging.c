#include "memory/paging.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/recovery.h"
#include "drivers/vesa.h"
#include "core/string.h"

page_directory_t* current_directory = 0;

page_directory_t* paging_create_directory(void) {
    page_directory_t* dir = (page_directory_t*)pmm_alloc_page();
    if (!dir) {
        LOG_ERROR("MEM", "Falha ao alocar diretorio de paginas");
        return 0;
    }
    kmemset(dir, 0, sizeof(page_directory_t));
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
    kmemset(table, 0, sizeof(page_table_t));

    current_directory->entries[table_idx] = (uint32_t)table | 0x03;

    return &table->entries[page_idx];
}

int paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags) {
    page_entry_t* page = paging_get_page(virtual, 1);
    if (!page) {
        LOG_ERROR("MEM", "Falha ao obter pagina para mapeamento");
        return ERR_MEM;
    }
    page->frame = physical / PAGE_SIZE;
    page->present = 1;
    page->rw = (flags & 0x2) ? 1 : 0;
    page->user = (flags & 0x4) ? 1 : 0;
    return OK;
}

static void paging_invalidate(uint32_t virtual_addr) {
    asm volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

static int paging_map_framebuffer(vesa_mode_t* mode) {
    uint32_t fb_phys;
    uint32_t fb_size;
    uint32_t fb_end;

    if (!mode || !mode->initialized) return ERR_NOT_FOUND;
    if (mode->height != 0 &&
        mode->pitch > 0xFFFFFFFFU / mode->height) {
        LOG_ERROR("MEM", "Tamanho do framebuffer excede o limite");
        return ERR_OVERFLOW;
    }

    fb_phys = (uint32_t)mode->framebuffer;
    fb_size = mode->pitch * mode->height;
    if (fb_size == 0 || fb_phys > 0xFFFFFFFFU - fb_size) {
        LOG_ERROR("MEM", "Intervalo do framebuffer invalido");
        return ERR_OVERFLOW;
    }

    fb_end = fb_phys + fb_size;
    for (uint32_t i = fb_phys; i < fb_end;) {
        if (paging_map_page(i, i, 0x03) != OK) {
            LOG_ERROR("MEM", "Falha ao mapear pagina do framebuffer");
            return ERR_MEM;
        }

        if (fb_end - i <= PAGE_SIZE) break;
        i += PAGE_SIZE;
    }

    return OK;
}

static int paging_abort_init(page_directory_t* dir, int error_code) {
    current_directory = 0;
    paging_free_directory(dir);
    return error_code;
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

int paging_init(void) {
    page_directory_t* dir = paging_create_directory();
    if (!dir) {
        LOG_ERROR("MEM", "Falha ao criar diretorio de paginas");
        return ERR_MEM;
    }

    current_directory = dir;

    /* A GDT do stage2 continua ativa ate o tss_init instalar a GDT do kernel. */
    for (uint32_t i = BOOT_TRANSITION_START; i < BOOT_TRANSITION_END; i += PAGE_SIZE) {
        if (paging_map_page(i, i, 0x03) != OK) {
            LOG_ERROR("MEM", "Falha ao mapear transicao do boot");
            return paging_abort_init(dir, ERR_MEM);
        }
    }

    for (uint32_t i = KERNEL_START; i < HEAP_START + HEAP_SIZE; i += PAGE_SIZE) {
        if (paging_map_page(i, i, 0x03) != OK) {
            LOG_ERROR("MEM", "Falha ao mapear kernel e heap");
            return paging_abort_init(dir, ERR_MEM);
        }
    }

    for (uint32_t i = 0xB8000; i < 0xC0000; i += PAGE_SIZE) {
        if (paging_map_page(i, i, 0x03) != OK) {
            LOG_ERROR("MEM", "Falha ao mapear memoria VGA");
            return paging_abort_init(dir, ERR_MEM);
        }
    }

    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized) {
        int framebuffer_result = paging_map_framebuffer(mode);
        if (framebuffer_result != OK) {
            LOG_WARN("MEM", "Framebuffer nao mapeado; ativando fallback VGA");
            vesa_disable();
            recovery_mark_disabled(RECOVERY_COMPONENT_VESA, framebuffer_result,
                                   "Framebuffer indisponivel; fallback VGA ativo");
            video_disable_framebuffer();
        } else {
            LOG_INFO("MEM", "Framebuffer mapeado na pagina");
        }
    }

    paging_switch_directory(dir);
    return OK;
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
