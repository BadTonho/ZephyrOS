#include "memory/paging.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/recovery.h"
#include "drivers/vesa.h"
#include "core/string.h"

page_directory_t* current_directory = 0;
static page_directory_t* kernel_directory = 0;
static int paging_initialized = 0;

page_directory_t* paging_create_directory(void) {
    page_directory_t* dir = (page_directory_t*)pmm_alloc_page();
    if (!dir) {
        LOG_ERROR("MEM", "Falha ao alocar diretorio de paginas");
        return 0;
    }
    kmemset(dir, 0, sizeof(page_directory_t));
    return dir;
}

page_directory_t* paging_get_current_directory(void) {
    return current_directory;
}

page_entry_t* paging_get_page_in_directory(page_directory_t* dir,
                                           uint32_t virtual_addr,
                                           int create) {
    if (!dir) {
        LOG_ERROR("MEM", "Diretorio nulo ao consultar pagina");
        return 0;
    }
    if (create != 0 && create != 1) {
        LOG_ERROR("MEM", "Parametro create invalido no paging");
        return 0;
    }

    uint32_t table_idx = virtual_addr / (PAGE_SIZE * 1024);
    uint32_t page_idx = (virtual_addr / PAGE_SIZE) % 1024;
    uint32_t directory_entry = dir->entries[table_idx];

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

    dir->entries[table_idx] = (uint32_t)table | 0x03;

    return &table->entries[page_idx];
}

page_entry_t* paging_get_page(uint32_t virtual_addr, int create) {
    if (!current_directory) {
        LOG_ERROR("MEM", "Diretorio de paginas atual inexistente");
        return 0;
    }
    return paging_get_page_in_directory(current_directory, virtual_addr, create);
}

int paging_map_page_in_directory(page_directory_t* dir,
                                 uint32_t virtual,
                                 uint32_t physical,
                                 uint32_t flags) {
    if (!dir) {
        LOG_ERROR("MEM", "Mapeamento com diretorio nulo");
        return ERR_NULL;
    }
    if ((virtual % PAGE_SIZE) != 0 || (physical % PAGE_SIZE) != 0) {
        LOG_ERROR("MEM", "Endereco desalinhado no mapeamento de pagina");
        return ERR_INVALID;
    }
    if ((flags & ~0x07U) != 0) {
        LOG_ERROR("MEM", "Flags invalidas no mapeamento de pagina");
        return ERR_INVALID;
    }
    if ((flags & PAGING_FLAG_USER) &&
        (virtual < USER_SPACE_START || virtual >= USER_SPACE_END)) {
        LOG_ERROR("MEM", "Pagina de usuario fora da faixa permitida");
        return ERR_INVALID;
    }

    uint32_t table_idx = virtual / (PAGE_SIZE * 1024);
    uint32_t directory_entry = dir->entries[table_idx];
    if ((flags & PAGING_FLAG_USER) && directory_entry &&
        !(directory_entry & PAGING_FLAG_USER)) {
        LOG_ERROR("MEM", "Tabela supervisor recusou pagina de usuario");
        return ERR_STATE;
    }

    page_entry_t* page = paging_get_page_in_directory(dir, virtual, 1);
    if (!page) {
        LOG_ERROR("MEM", "Falha ao obter pagina para mapeamento");
        return ERR_MEM;
    }

    if (!(directory_entry & PAGING_FLAG_PRESENT) &&
        (flags & PAGING_FLAG_USER)) {
        dir->entries[table_idx] |= PAGING_FLAG_USER;
    }
    page->frame = physical / PAGE_SIZE;
    page->present = 1;
    page->rw = (flags & PAGING_FLAG_WRITE) ? 1 : 0;
    page->user = (flags & PAGING_FLAG_USER) ? 1 : 0;
    return OK;
}

int paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags) {
    if (!current_directory) {
        LOG_ERROR("MEM", "Mapeamento sem diretorio de paginas ativo");
        return ERR_STATE;
    }
    return paging_map_page_in_directory(current_directory, virtual,
                                        physical, flags);
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
    kernel_directory = 0;
    paging_initialized = 0;
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
    paging_initialized = 1;
}

int paging_init(void) {
    if (paging_initialized && current_directory) {
        LOG_WARN("MEM", "Paging ja estava inicializado");
        return OK;
    }

    LOG_INFO("MEM", "Inicializando paging");
    page_directory_t* dir = paging_create_directory();
    if (!dir) {
        LOG_ERROR("MEM", "Falha ao criar diretorio de paginas");
        return ERR_MEM;
    }

    current_directory = dir;
    kernel_directory = dir;

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
    LOG_INFO("MEM", "Paging inicializado com sucesso");
    return OK;
}

int paging_is_ready(void) {
    return paging_initialized && current_directory != 0;
}

page_directory_t* paging_create_user_directory(void) {
    page_directory_t* dir;

    if (!paging_is_ready() || !kernel_directory) {
        LOG_ERROR("MEM", "Paging indisponivel para diretorio de usuario");
        return 0;
    }

    dir = paging_create_directory();
    if (!dir) return 0;

    /* As tabelas do kernel sao compartilhadas, mas continuam supervisor. */
    for (uint32_t i = 0; i < 1024; i++) {
        dir->entries[i] = kernel_directory->entries[i] & ~PAGING_FLAG_USER;
    }
    return dir;
}

void paging_free_user_directory(page_directory_t* dir) {
    if (!dir) {
        LOG_WARN("MEM", "Diretorio de usuario nulo ignorado");
        return;
    }
    if (dir == current_directory) {
        LOG_ERROR("MEM", "Tentativa de liberar diretorio de usuario ativo");
        return;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t entry = dir->entries[i];
        uint32_t shared = kernel_directory ? kernel_directory->entries[i] : 0;
        if (!(entry & PAGING_FLAG_PRESENT) ||
            (entry & 0xFFFFF000U) == (shared & 0xFFFFF000U)) {
            continue;
        }

        page_table_t* table = (page_table_t*)(entry & 0xFFFFF000U);
        for (uint32_t j = 0; j < 1024; j++) {
            if (table->entries[j].present && table->entries[j].user) {
                pmm_free_page((void*)(table->entries[j].frame * PAGE_SIZE));
            }
        }
        pmm_free_page((void*)(entry & 0xFFFFF000U));
    }
    pmm_free_page(dir);
    LOG_DEBUG("MEM", "Diretorio de usuario liberado");
}

int paging_validate_user_range(uint32_t address, uint32_t size, int write) {
    uint32_t end;

    if (!current_directory) {
        LOG_ERROR("MEM", "Validacao de usuario sem paging ativo");
        return ERR_STATE;
    }
    if (!address || size == 0) {
        LOG_ERROR("MEM", "Intervalo de usuario nulo ou vazio");
        return ERR_NULL;
    }
    if (address < USER_SPACE_START || address >= USER_SPACE_END ||
        size > USER_SPACE_END - address) {
        LOG_ERROR("MEM", "Intervalo de usuario fora dos limites");
        return ERR_INVALID;
    }
    end = address + size;
    for (uint32_t page_addr = address & ~(PAGE_SIZE - 1U);
         page_addr < end; page_addr += PAGE_SIZE) {
        page_entry_t* page = paging_get_page_in_directory(current_directory,
                                                           page_addr, 0);
        if (!page || !page->present || !page->user ||
            (write && !page->rw)) {
            LOG_WARN("MEM", "Pagina de usuario sem permissao");
            return ERR_UNAVAILABLE;
        }
        if (page_addr > 0xFFFFFFFFU - PAGE_SIZE) break;
    }
    return OK;
}

int paging_copy_from_user(void* destination, const void* source,
                          uint32_t size) {
    int result;

    if (!destination || !source) {
        LOG_ERROR("MEM", "Copia de usuario com ponteiro nulo");
        return ERR_NULL;
    }
    result = paging_validate_user_range((uint32_t)source, size, 0);
    if (result != OK) return result;
    kmemcpy(destination, source, size);
    return OK;
}

int paging_copy_to_user(void* destination, const void* source,
                        uint32_t size) {
    int result;

    if (!destination || !source) {
        LOG_ERROR("MEM", "Copia para usuario com ponteiro nulo");
        return ERR_NULL;
    }
    result = paging_validate_user_range((uint32_t)destination, size, 1);
    if (result != OK) return result;
    kmemcpy(destination, source, size);
    return OK;
}

void paging_free_directory(page_directory_t* dir) {
    if (!dir) {
        LOG_WARN("MEM", "Diretorio nulo ignorado ao liberar paging");
        return;
    }
    if (dir == current_directory) {
        LOG_ERROR("MEM", "Tentativa de liberar diretorio de paginas ativo");
        return;
    }

    for (int i = 0; i < 1024; i++) {
        if (dir->entries[i] & 0x01) {
            pmm_free_page((void*)(dir->entries[i] & 0xFFFFF000));
        }
    }
    pmm_free_page(dir);
    LOG_DEBUG("MEM", "Diretorio de paginas liberado");
}
