#include "memory/paging.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/recovery.h"
#include "drivers/vesa.h"
#include "core/string.h"
#include "core/timer.h"
#include "memory/vma.h"
#include "process/process.h"

page_directory_t* current_directory = 0;
static page_directory_t* kernel_directory = 0;
static int paging_initialized = 0;

#define PAGING_MAX_USER_DIRECTORIES 64U
#define PAGING_TABLE_ENTRIES 1024U

#if KERNEL_END != USER_SPACE_START || HEAP_START != USER_SPACE_END
#error "Layout supervisor sobrepoe a janela virtual ZAPP"
#endif

#if PHYSICAL_IDENTITY_START < USER_SPACE_END
#error "Mapa fisico supervisor sobrepoe a janela virtual ZAPP"
#endif

static page_directory_t* user_directories[PAGING_MAX_USER_DIRECTORIES];
static paging_user_stats_t user_paging_stats;
static paging_boot_stats_t paging_boot_stats;

#if defined(ZEPHYROS_HOST_TEST)
#define PAGING_HOST_MAX_TABLES 128U
#define PAGING_HOST_TABLE_TOKEN_BASE 0xE0000000U
#define PAGING_HOST_MAX_USER_BUFFERS 8U

static page_table_t* paging_host_tables[PAGING_HOST_MAX_TABLES];

typedef struct {
    uint32_t address;
    uint32_t size;
    void* pointer;
} paging_host_user_buffer_t;

static paging_host_user_buffer_t paging_host_user_buffers[
    PAGING_HOST_MAX_USER_BUFFERS];

static uint32_t paging_host_table_token(page_table_t* table) {
    for (uint32_t index = 0U; index < PAGING_HOST_MAX_TABLES; index++) {
        if (paging_host_tables[index] == table) {
            return PAGING_HOST_TABLE_TOKEN_BASE + index * PAGE_SIZE;
        }
    }
    for (uint32_t index = 0U; index < PAGING_HOST_MAX_TABLES; index++) {
        if (!paging_host_tables[index]) {
            paging_host_tables[index] = table;
            return PAGING_HOST_TABLE_TOKEN_BASE + index * PAGE_SIZE;
        }
    }
    return 0U;
}
#endif

static page_table_t* paging_host_table_from_entry(uint32_t entry) {
#if defined(ZEPHYROS_HOST_TEST)
    uint32_t token = entry & 0xFFFFF000U;
    uint32_t offset;

    if (token < PAGING_HOST_TABLE_TOKEN_BASE) return 0;
    offset = token - PAGING_HOST_TABLE_TOKEN_BASE;
    if ((offset % PAGE_SIZE) != 0U || offset / PAGE_SIZE >= PAGING_HOST_MAX_TABLES) {
        return 0;
    }
    return paging_host_tables[offset / PAGE_SIZE];
#else
    return (page_table_t*)(entry & 0xFFFFF000U);
#endif
}

static void paging_host_release_table(uint32_t entry) {
#if defined(ZEPHYROS_HOST_TEST)
    uint32_t token = entry & 0xFFFFF000U;
    uint32_t offset;

    if (token < PAGING_HOST_TABLE_TOKEN_BASE) return;
    offset = token - PAGING_HOST_TABLE_TOKEN_BASE;
    if ((offset % PAGE_SIZE) != 0U || offset / PAGE_SIZE >= PAGING_HOST_MAX_TABLES) {
        return;
    }
    paging_host_tables[offset / PAGE_SIZE] = 0;
#else
    (void)entry;
#endif
}

static void* paging_physical_pointer(uint32_t physical) {
#if defined(ZEPHYROS_HOST_TEST)
    union {
        uint32_t address;
        void* pointer;
    } value;

    value.address = physical;
    return value.pointer;
#else
    return (void*)physical;
#endif
}

static uint32_t paging_pointer_address(const void* pointer) {
#if defined(ZEPHYROS_HOST_TEST)
    for (uint32_t index = 0U; index < PAGING_HOST_MAX_USER_BUFFERS; index++) {
        if (paging_host_user_buffers[index].pointer == pointer) {
            return paging_host_user_buffers[index].address;
        }
    }
    union {
        const void* pointer;
        uint32_t address;
    } value;

    value.pointer = pointer;
    return value.address;
#else
    union {
        const void* pointer;
        uint32_t address;
    } value;

    value.pointer = pointer;
    return value.address;
#endif
}

#if defined(ZEPHYROS_HOST_TEST)
static void* paging_host_resolve_user_pointer(uint32_t address) {
    for (uint32_t index = 0U; index < PAGING_HOST_MAX_USER_BUFFERS; index++) {
        paging_host_user_buffer_t* buffer = &paging_host_user_buffers[index];
        if (buffer->pointer && address >= buffer->address &&
            address - buffer->address < buffer->size) {
            return (uint8_t*)buffer->pointer + (address - buffer->address);
        }
    }
    return 0;
}

int paging_host_register_user_buffer(uint32_t address, void* pointer,
                                     uint32_t size) {
    if (!pointer || !address || !size ||
        address < USER_SPACE_START || size > USER_SPACE_END - address) {
        LOG_WARN("MEM", "Fixture host de usuario invalida");
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < PAGING_HOST_MAX_USER_BUFFERS; index++) {
        if (!paging_host_user_buffers[index].pointer) {
            paging_host_user_buffers[index].address = address;
            paging_host_user_buffers[index].size = size;
            paging_host_user_buffers[index].pointer = pointer;
            return OK;
        }
    }
    LOG_WARN("MEM", "Limite de fixtures host de usuario atingido");
    return ERR_OVERFLOW;
}
#endif

static void paging_free_table_entry(uint32_t entry) {
#if defined(ZEPHYROS_HOST_TEST)
    page_table_t* table = paging_host_table_from_entry(entry);

    paging_host_release_table(entry);
    if (table) pmm_free_page(table);
#else
    pmm_free_page(paging_physical_pointer(entry & 0xFFFFF000U));
#endif
}

#if defined(ZEPHYROS_HOST_TEST)
#define PAGING_TABLE_ENTRY(table) \
    (paging_host_table_token(table) | PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE)
#else
#define PAGING_TABLE_ENTRY(table) \
    ((uint32_t)(table) | PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE)
#endif

static int paging_map_identity_range_fast(uint32_t start, uint32_t end);
static void paging_invalidate(uint32_t virtual_addr);

static int paging_user_directory_index(page_directory_t* dir) {
    if (!dir) return -1;

    for (uint32_t i = 0; i < PAGING_MAX_USER_DIRECTORIES; i++) {
        if (user_directories[i] == dir) return (int)i;
    }
    return -1;
}

static int paging_is_registered_user_directory(page_directory_t* dir) {
    return paging_user_directory_index(dir) >= 0;
}

static int paging_register_user_directory(page_directory_t* dir) {
    if (!dir) {
        LOG_ERROR("MEM", "Diretorio nulo ao registrar espaco de usuario");
        return 0;
    }
    for (uint32_t i = 0; i < PAGING_MAX_USER_DIRECTORIES; i++) {
        if (!user_directories[i]) {
            user_directories[i] = dir;
            user_paging_stats.active_directories++;
            user_paging_stats.directories_created++;
            return 1;
        }
    }
    LOG_ERROR("MEM", "Limite de diretorios de usuario atingido");
    return 0;
}

static void paging_unregister_user_directory(uint32_t index) {
    if (index >= PAGING_MAX_USER_DIRECTORIES || !user_directories[index]) {
        LOG_ERROR("MEM", "Registro de diretorio de usuario invalido");
        return;
    }
    user_directories[index] = 0;
    if (user_paging_stats.active_directories > 0) {
        user_paging_stats.active_directories--;
    }
    user_paging_stats.directories_released++;
}

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
        page_table_t* table = paging_host_table_from_entry(directory_entry);
        if (!table) {
            LOG_ERROR("MEM", "Tabela de paginas invalida");
            return 0;
        }
        return &table->entries[page_idx];
    }

    if (!create) return 0;

    memory_zone_t zone = paging_is_registered_user_directory(dir) ?
                         MEMORY_ZONE_PROCESS : MEMORY_ZONE_KERNEL;
    page_table_t* table = (page_table_t*)pmm_alloc_page_in_zone(zone);
    if (!table) {
        LOG_ERROR("MEM", "Falha ao alocar tabela de paginas");
        return 0;
    }
    kmemset(table, 0, sizeof(page_table_t));

    dir->entries[table_idx] = PAGING_TABLE_ENTRY(table);
    if (!dir->entries[table_idx]) {
        pmm_free_page(table);
        LOG_ERROR("MEM", "Limite de tabelas de paginas atingido");
        return 0;
    }

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
    int user_directory;
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

    user_directory = paging_is_registered_user_directory(dir);
    if ((flags & PAGING_FLAG_USER) && user_directory < 0) {
        LOG_ERROR("MEM", "Pagina de usuario sem diretorio registrado");
        return ERR_STATE;
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

    if ((flags & PAGING_FLAG_USER) && page->present) {
        LOG_ERROR("MEM", "Remapeamento de pagina de usuario recusado");
        return ERR_STATE;
    }

    if (!(directory_entry & PAGING_FLAG_PRESENT) &&
        (flags & PAGING_FLAG_USER)) {
        dir->entries[table_idx] |= PAGING_FLAG_USER;
    }
    page->frame = physical / PAGE_SIZE;
    page->present = 1;
    page->rw = (flags & PAGING_FLAG_WRITE) ? 1 : 0;
    page->user = (flags & PAGING_FLAG_USER) ? 1 : 0;
    if (flags & PAGING_FLAG_USER) user_paging_stats.active_pages++;
    if (dir == current_directory) paging_invalidate(virtual);
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
#if defined(ZEPHYROS_HOST_TEST)
    (void)virtual_addr;
#else
    asm volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
#endif
}

static int paging_table_is_empty(const page_table_t* table) {
    if (!table) return 1;
    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        if (table->entries[i].present) return 0;
    }
    return 1;
}

int paging_unmap_user_page_in_directory(page_directory_t* dir,
                                        uint32_t virtual_addr) {
    uint32_t table_idx;
    uint32_t directory_entry;
    uint32_t shared_entry;
    page_table_t* table;
    page_entry_t* page;
    uint32_t physical;

    if (!dir) {
        LOG_ERROR("MEM", "Desmapeamento com diretorio nulo");
        return ERR_NULL;
    }
    if (!paging_is_registered_user_directory(dir)) {
        LOG_ERROR("MEM", "Desmapeamento fora de diretorio de usuario");
        return ERR_STATE;
    }
    if ((virtual_addr % PAGE_SIZE) != 0 ||
        virtual_addr < USER_SPACE_START || virtual_addr >= USER_SPACE_END) {
        LOG_ERROR("MEM", "Endereco invalido no desmapeamento de usuario");
        return ERR_INVALID;
    }

    table_idx = virtual_addr / (PAGE_SIZE * PAGING_TABLE_ENTRIES);
    directory_entry = dir->entries[table_idx];
    if (!(directory_entry & PAGING_FLAG_PRESENT) ||
        !(directory_entry & PAGING_FLAG_USER)) {
        LOG_ERROR("MEM", "Tabela de usuario ausente no desmapeamento");
        return ERR_NOT_FOUND;
    }
    table = paging_host_table_from_entry(directory_entry);
    if (!table) {
        LOG_ERROR("MEM", "Tabela de paginas invalida no desmapeamento");
        return ERR_STATE;
    }
    page = &table->entries[(virtual_addr / PAGE_SIZE) % PAGING_TABLE_ENTRIES];
    if (!page->present || !page->user) {
        LOG_ERROR("MEM", "Pagina de usuario ausente no desmapeamento");
        return ERR_NOT_FOUND;
    }

    physical = page->frame * PAGE_SIZE;
    kmemset(page, 0, sizeof(*page));
    if (dir == current_directory) paging_invalidate(virtual_addr);
    pmm_free_page(paging_physical_pointer(physical));
    if (user_paging_stats.active_pages == 0) {
        LOG_ERROR("MEM", "Contagem de paginas ativa inconsistente");
    } else {
        user_paging_stats.active_pages--;
    }

    shared_entry = kernel_directory ? kernel_directory->entries[table_idx] : 0;
    if ((directory_entry & 0xFFFFF000U) !=
        (shared_entry & 0xFFFFF000U) && paging_table_is_empty(table)) {
        dir->entries[table_idx] = 0;
        paging_host_release_table(directory_entry);
        pmm_free_page(table);
    }
    return OK;
}

static int paging_map_framebuffer(vesa_mode_t* mode) {
    uint32_t fb_phys;
    uint32_t fb_size;
    uint32_t fb_end;
    uint32_t map_start;
    uint32_t map_end;
    union {
        uint32_t address;
        uint32_t* pointer;
    } framebuffer_address;

    if (!mode || !mode->initialized) return ERR_NOT_FOUND;
    if (mode->height != 0 &&
        mode->pitch > 0xFFFFFFFFU / mode->height) {
        LOG_ERROR("MEM", "Tamanho do framebuffer excede o limite");
        return ERR_OVERFLOW;
    }

    framebuffer_address.pointer = mode->framebuffer;
    fb_phys = framebuffer_address.address;
    fb_size = mode->pitch * mode->height;
    if (fb_size == 0 || fb_phys > 0xFFFFFFFFU - fb_size) {
        LOG_ERROR("MEM", "Intervalo do framebuffer invalido");
        return ERR_OVERFLOW;
    }

    fb_end = fb_phys + fb_size;
    if ((uint64_t)fb_end + PAGE_SIZE - 1U > MAX_PHYSICAL_ADDRESS) {
        LOG_ERROR("MEM", "Alinhamento do framebuffer excede o limite");
        return ERR_OVERFLOW;
    }
    map_start = fb_phys & ~(PAGE_SIZE - 1U);
    map_end = (fb_end + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
    return paging_map_identity_range_fast(map_start, map_end);
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
#if !defined(ZEPHYROS_HOST_TEST)
    asm volatile("mov %0, %%cr3" : : "r"(dir) : "memory");

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
#endif
    paging_initialized = 1;
}

static page_table_t* paging_bootstrap_get_table(uint32_t table_index) {
    uint32_t entry;
    page_table_t* table;

    if (!kernel_directory || table_index >= PAGING_TABLE_ENTRIES) {
        LOG_ERROR("MEM", "Tabela de bootstrap fora do diretorio");
        return 0;
    }
    entry = kernel_directory->entries[table_index];
    if (entry & PAGING_FLAG_PRESENT) {
        if (entry & PAGING_FLAG_USER) {
            LOG_ERROR("MEM", "Tabela de usuario conflita com bootstrap");
            return 0;
        }
        return paging_host_table_from_entry(entry);
    }

    table = (page_table_t*)pmm_alloc_page();
    if (!table) {
        LOG_ERROR("MEM", "Falha ao alocar tabela do bootstrap");
        return 0;
    }
    kmemset(table, 0, sizeof(*table));
    kernel_directory->entries[table_index] = PAGING_TABLE_ENTRY(table);
    if (!kernel_directory->entries[table_index]) {
        pmm_free_page(table);
        LOG_ERROR("MEM", "Limite de tabelas do bootstrap atingido");
        return 0;
    }
    paging_boot_stats.page_tables_created++;
    return table;
}

static int paging_map_identity_range_fast(uint32_t start, uint32_t end) {
    uint32_t address = start;

    if (start >= end || (start % PAGE_SIZE) != 0 ||
        (end % PAGE_SIZE) != 0) {
        LOG_ERROR("MEM", "Intervalo de identidade invalido");
        return ERR_INVALID;
    }

    while (address < end) {
        uint32_t table_index = address / (PAGE_SIZE * PAGING_TABLE_ENTRIES);
        uint32_t entry_index = (address / PAGE_SIZE) % PAGING_TABLE_ENTRIES;
        uint32_t range_pages = (end - address) / PAGE_SIZE;
        uint32_t table_pages = PAGING_TABLE_ENTRIES - entry_index;
        page_table_t* table = paging_bootstrap_get_table(table_index);

        if (!table) {
            LOG_ERROR("MEM", "Falha ao obter tabela do mapa em blocos");
            return ERR_MEM;
        }
        if (table_pages > range_pages) table_pages = range_pages;
        for (uint32_t page_index = 0; page_index < table_pages; page_index++) {
            page_entry_t* page = &table->entries[entry_index + page_index];
            uint32_t frame = address / PAGE_SIZE;

            if (page->present && (page->frame != frame || page->user)) {
                LOG_ERROR("MEM", "Remapeamento conflitante no bootstrap");
                return ERR_STATE;
            }
            if (!page->present) paging_boot_stats.identity_pages++;
            page->frame = frame;
            page->present = 1;
            page->rw = 1;
            page->user = 0;
            address += PAGE_SIZE;
        }
    }
    return OK;
}

int paging_init(void) {
    uint32_t physical_end;
    uint32_t start_ticks;

    if (paging_initialized && current_directory) {
        LOG_WARN("MEM", "Paging ja estava inicializado");
        return OK;
    }

    LOG_INFO("MEM", "Inicializando paging");
    start_ticks = timer_get_ticks();
    kmemset(user_directories, 0, sizeof(user_directories));
    kmemset(&user_paging_stats, 0, sizeof(user_paging_stats));
    kmemset(&paging_boot_stats, 0, sizeof(paging_boot_stats));
#if defined(ZEPHYROS_HOST_TEST)
    kmemset(paging_host_tables, 0, sizeof(paging_host_tables));
    kmemset(paging_host_user_buffers, 0, sizeof(paging_host_user_buffers));
#endif
    user_paging_stats.initialized = 1;
    page_directory_t* dir = paging_create_directory();
    if (!dir) {
        LOG_ERROR("MEM", "Falha ao criar diretorio de paginas");
        return ERR_MEM;
    }

    current_directory = dir;
    kernel_directory = dir;

    if (paging_map_identity_range_fast(BOOT_CONTEXT_PAGE_START,
                                       BOOT_CONTEXT_PAGE_END) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear contexto reservado do boot");
        return paging_abort_init(dir, ERR_MEM);
    }
    if (paging_map_identity_range_fast(BOOT_TRANSITION_START,
                                       BOOT_TRANSITION_END) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear transicao do boot");
        return paging_abort_init(dir, ERR_MEM);
    }
    if (paging_map_identity_range_fast(PMM_BITMAP_STORAGE_START,
                                       KERNEL_STACK_TOP) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear bitmaps e stack");
        return paging_abort_init(dir, ERR_MEM);
    }
    if (paging_map_identity_range_fast(KERNEL_START, KERNEL_END) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear kernel");
        return paging_abort_init(dir, ERR_MEM);
    }
    if (paging_map_identity_range_fast(HEAP_START,
                                       HEAP_START + HEAP_SIZE) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear heap");
        return paging_abort_init(dir, ERR_MEM);
    }

    physical_end = memory_get_total();
    if (physical_end <= PHYSICAL_IDENTITY_START ||
        paging_map_identity_range_fast(PHYSICAL_IDENTITY_START,
                                       physical_end) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear RAM fisica");
        return paging_abort_init(dir, ERR_MEM);
    }
    if (paging_map_identity_range_fast(0xB8000U, 0xC0000U) != OK) {
        LOG_ERROR("MEM", "Falha ao mapear memoria VGA");
        return paging_abort_init(dir, ERR_MEM);
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
    paging_boot_stats.init_ticks = timer_get_ticks() - start_ticks;
    paging_boot_stats.initialized = 1;
    LOG_INFO("MEM", "Paging inicializado com sucesso");
    return OK;
}

int paging_is_ready(void) {
    return paging_initialized && current_directory != 0;
}

int paging_get_user_page_count(page_directory_t* dir, uint32_t* out_count) {
    uint32_t count = 0U;

    if (!dir || !out_count) {
        LOG_ERROR("MEM", "Destino invalido ao contar paginas de usuario");
        return ERR_NULL;
    }
    if (!paging_is_ready()) {
        LOG_ERROR("MEM", "Paging indisponivel ao contar paginas de usuario");
        return ERR_STATE;
    }
    if (dir != kernel_directory && !paging_is_registered_user_directory(dir)) {
        LOG_ERROR("MEM", "Diretorio desconhecido ao contar paginas de usuario");
        return ERR_STATE;
    }
    for (uint32_t table_index = 0U; table_index < PAGING_TABLE_ENTRIES;
         table_index++) {
        uint32_t directory_entry = dir->entries[table_index];
        uint32_t shared_entry = kernel_directory ?
                                kernel_directory->entries[table_index] : 0U;

        if (!(directory_entry & PAGING_FLAG_PRESENT) ||
            (directory_entry & 0xFFFFF000U) ==
            (shared_entry & 0xFFFFF000U)) {
            continue;
        }
        if (!(directory_entry & PAGING_FLAG_USER)) continue;
        page_table_t* table = paging_host_table_from_entry(directory_entry);
        if (!table) {
            LOG_ERROR("MEM", "Tabela invalida ao contar paginas de usuario");
            return ERR_STATE;
        }

        for (uint32_t page_index = 0U; page_index < PAGING_TABLE_ENTRIES;
             page_index++) {
            page_entry_t* page = &table->entries[page_index];

            if (page->present && page->user) {
                if (count == 0xFFFFFFFFU) {
                    LOG_ERROR("MEM", "Contagem de paginas excedeu limite");
                    return ERR_OVERFLOW;
                }
                count++;
            }
        }
    }
    *out_count = count;
    return OK;
}

page_directory_t* paging_create_user_directory(void) {
    page_directory_t* dir;

    if (!paging_is_ready() || !kernel_directory) {
        LOG_ERROR("MEM", "Paging indisponivel para diretorio de usuario");
        return 0;
    }

    dir = (page_directory_t*)pmm_alloc_page_in_zone(MEMORY_ZONE_PROCESS);
    if (!dir) return 0;

    /* As tabelas do kernel sao compartilhadas, mas continuam supervisor. */
    for (uint32_t i = 0; i < 1024; i++) {
        dir->entries[i] = kernel_directory->entries[i] & ~PAGING_FLAG_USER;
    }
    if (!paging_register_user_directory(dir)) {
        pmm_free_page(dir);
        return 0;
    }
    return dir;
}

void paging_free_user_directory(page_directory_t* dir) {
    int directory_index;
    uint32_t released_pages = 0;

    if (!dir) {
        LOG_ERROR("MEM", "Diretorio de usuario nulo recusado");
        user_paging_stats.rejected_releases++;
        return;
    }
    if (dir == current_directory) {
        LOG_ERROR("MEM", "Tentativa de liberar diretorio de usuario ativo");
        user_paging_stats.rejected_releases++;
        return;
    }
    if (dir == kernel_directory) {
        LOG_ERROR("MEM", "Tentativa de liberar diretorio do kernel");
        user_paging_stats.rejected_releases++;
        return;
    }
    directory_index = paging_user_directory_index(dir);
    if (directory_index < 0) {
        LOG_ERROR("MEM", "Diretorio de usuario desconhecido recusado");
        user_paging_stats.rejected_releases++;
        return;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t entry = dir->entries[i];
        uint32_t shared = kernel_directory ? kernel_directory->entries[i] : 0;
        if (!(entry & PAGING_FLAG_PRESENT) ||
            (entry & 0xFFFFF000U) == (shared & 0xFFFFF000U)) {
            continue;
        }

        page_table_t* table = paging_host_table_from_entry(entry);
        if (!table) {
            LOG_ERROR("MEM", "Tabela invalida ao liberar diretorio de usuario");
            continue;
        }
        for (uint32_t j = 0; j < 1024; j++) {
            if (table->entries[j].present && table->entries[j].user) {
                pmm_free_page(paging_physical_pointer(
                    table->entries[j].frame * PAGE_SIZE));
                released_pages++;
            }
        }
        paging_free_table_entry(entry);
    }
    pmm_free_page(dir);
    if (released_pages > user_paging_stats.active_pages) {
        LOG_ERROR("MEM", "Contagem de paginas de usuario inconsistente");
        user_paging_stats.active_pages = 0;
    } else {
        user_paging_stats.active_pages -= released_pages;
    }
    paging_unregister_user_directory((uint32_t)directory_index);
    LOG_DEBUG("MEM", "Diretorio de usuario liberado");
}

void paging_get_user_stats(paging_user_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("MEM", "Destino nulo ao consultar diretorios de usuario");
        return;
    }
    *stats = user_paging_stats;
}

int paging_get_boot_stats(paging_boot_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("MEM", "Destino nulo ao consultar bootstrap do paging");
        return ERR_NULL;
    }
    if (!paging_boot_stats.initialized) {
        LOG_ERROR("MEM", "Metricas consultadas antes do paging");
        return ERR_STATE;
    }
    *stats = paging_boot_stats;
    return OK;
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
        if (!page || !page->present) {
            process_t* current = process_get_current();
            if (!current || !process_is_user(current) ||
                process_vma_ensure_page(current, page_addr, write) != OK) {
                LOG_WARN("MEM", "Pagina de usuario ausente");
                return ERR_UNAVAILABLE;
            }
            page = paging_get_page_in_directory(current_directory,
                                                page_addr, 0);
        }
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
    uint32_t source_address;
    const void* resolved_source;

    if (!destination || !source) {
        LOG_ERROR("MEM", "Copia de usuario com ponteiro nulo");
        return ERR_NULL;
    }
    source_address = paging_pointer_address(source);
    result = paging_validate_user_range(source_address, size, 0);
    if (result != OK) return result;
    resolved_source = source;
#if defined(ZEPHYROS_HOST_TEST)
    resolved_source = paging_host_resolve_user_pointer(source_address);
    if (!resolved_source) return ERR_UNAVAILABLE;
#endif
    kmemcpy(destination, resolved_source, size);
    return OK;
}

int paging_copy_to_user(void* destination, const void* source,
                        uint32_t size) {
    int result;
    uint32_t destination_address;
    void* resolved_destination;

    if (!destination || !source) {
        LOG_ERROR("MEM", "Copia para usuario com ponteiro nulo");
        return ERR_NULL;
    }
    destination_address = paging_pointer_address(destination);
    result = paging_validate_user_range(destination_address, size, 1);
    if (result != OK) return result;
    resolved_destination = destination;
#if defined(ZEPHYROS_HOST_TEST)
    resolved_destination = paging_host_resolve_user_pointer(destination_address);
    if (!resolved_destination) return ERR_UNAVAILABLE;
#endif
    kmemcpy(resolved_destination, source, size);
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
    if (paging_is_registered_user_directory(dir)) {
        LOG_ERROR("MEM", "Liberador generico recusou diretorio de usuario");
        user_paging_stats.rejected_releases++;
        return;
    }

    for (int i = 0; i < 1024; i++) {
        if (dir->entries[i] & 0x01) {
            paging_free_table_entry(dir->entries[i]);
        }
    }
    pmm_free_page(dir);
    LOG_DEBUG("MEM", "Diretorio de paginas liberado");
}
