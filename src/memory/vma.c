#include "memory/vma.h"
#include "process/process.h"
#include "core/memory.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define VMA_IMAGE_FLAGS_CODE (VM_READ | VM_EXEC | VM_ANONYMOUS)
#define VMA_IMAGE_FLAGS_DATA (VM_READ | VM_WRITE | VM_ANONYMOUS)
#define VMA_IMAGE_FLAGS_LAUNCH (VM_READ | VM_WRITE | VM_EXEC | VM_ANONYMOUS)
#define VMA_IMAGE_FLAGS_STACK (VM_READ | VM_WRITE | VM_ANONYMOUS)
#define VMA_PROTECTION_MASK (VM_READ | VM_WRITE | VM_EXEC)
#define VMA_FLAGS_MASK (VM_SHARED | VM_ANONYMOUS)
#define VMA_PAGE_FAULT_PRESENT 0x01U
#define VMA_PAGE_FAULT_WRITE 0x02U
#define VMA_PAGE_FAULT_USER 0x04U
#define VMA_PAGE_FAULT_RESERVED 0x08U
#define VMA_PAGE_FAULT_INSTRUCTION 0x10U

static page_fault_stats_t page_fault_stats;

static int process_vma_is_current_user(const process_t* proc) {
    if (!proc) {
        LOG_ERROR("MEM", "Processo nulo ao operar VMA");
        return ERR_NULL;
    }
    if (proc != process_get_current() || !process_is_user(proc)) {
        LOG_ERROR("MEM", "Operacao VMA fora do processo ring 3 atual");
        return ERR_STATE;
    }
    if (!proc->page_directory) {
        LOG_ERROR("MEM", "Processo ring 3 sem diretorio para VMA");
        return ERR_STATE;
    }
    if (proc->page_directory != paging_get_current_directory()) {
        LOG_ERROR("MEM", "Diretorio da VMA nao esta ativo");
        return ERR_STATE;
    }
    return OK;
}

static int process_vma_validate_length(uint32_t length,
                                       uint32_t* rounded_length) {
    if (!rounded_length) {
        LOG_ERROR("MEM", "Destino nulo ao arredondar VMA");
        return ERR_NULL;
    }
    if (length == 0) {
        LOG_ERROR("MEM", "Tamanho zero rejeitado na VMA");
        return ERR_INVALID;
    }
    if (length > 0xFFFFFFFFU - (PAGE_SIZE - 1U)) {
        LOG_ERROR("MEM", "Tamanho da VMA excede o limite");
        return ERR_OVERFLOW;
    }
    *rounded_length = (length + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
    if (*rounded_length == 0) {
        LOG_ERROR("MEM", "Tamanho arredondado da VMA invalido");
        return ERR_OVERFLOW;
    }
    return OK;
}

static vm_area_t* process_vma_create(uint32_t start_addr, uint32_t end_addr,
                                     uint32_t flags) {
    vm_area_t* area;

    area = (vm_area_t*)kmalloc(sizeof(vm_area_t));
    if (!area) {
        LOG_ERROR("MEM", "Falha ao alocar metadado de VMA");
        return 0;
    }
    area->start_addr = start_addr;
    area->end_addr = end_addr;
    area->flags = flags;
    area->file = 0;
    area->offset = 0;
    area->next = 0;
    return area;
}

static int process_vma_insert_sorted(process_t* proc, vm_area_t* area) {
    vm_area_t* current;

    if (!proc || !area) {
        LOG_ERROR("MEM", "VMA nula na insercao ordenada");
        return ERR_NULL;
    }
    if (!proc->vma_list || area->start_addr < proc->vma_list->start_addr) {
        area->next = proc->vma_list;
        proc->vma_list = area;
        proc->vma_count++;
        return OK;
    }

    current = proc->vma_list;
    while (current->next &&
           current->next->start_addr < area->start_addr) {
        current = current->next;
    }
    area->next = current->next;
    current->next = area;
    proc->vma_count++;
    return OK;
}

static int process_vma_find_gap(const process_t* proc, uint32_t length,
                                uint32_t* address_out) {
    const vm_area_t* current;
    uint32_t candidate = VMA_USER_MMAP_START;

    if (!proc || !address_out || length == 0) {
        LOG_ERROR("MEM", "Parametros invalidos ao procurar VMA livre");
        return ERR_NULL;
    }
    current = proc->vma_list;
    while (current) {
        if (current->end_addr <= VMA_USER_MMAP_START) {
            current = current->next;
            continue;
        }
        if (current->start_addr >= VMA_USER_MMAP_END) break;
        if (current->start_addr > candidate &&
            current->start_addr - candidate >= length) {
            *address_out = candidate;
            return OK;
        }
        if (current->end_addr > candidate) candidate = current->end_addr;
        current = current->next;
    }
    if (candidate <= VMA_USER_MMAP_END &&
        length <= VMA_USER_MMAP_END - candidate) {
        *address_out = candidate;
        return OK;
    }
    LOG_WARN("MEM", "Faixa dinamica de VMA esgotada");
    return ERR_MEM;
}

static int process_vma_unmap_pages(process_t* proc, uint32_t start_addr,
                                   uint32_t end_addr) {
    for (uint32_t address = start_addr; address < end_addr;
         address += PAGE_SIZE) {
        page_entry_t* page = paging_get_page_in_directory(
            proc->page_directory, address, 0);
        if (!page || !page->present) continue;
        if (!page->user) {
            LOG_ERROR("MEM", "Pagina supervisora encontrada na VMA");
            return ERR_STATE;
        }
        int result = paging_unmap_user_page_in_directory(
            proc->page_directory, address);
        if (result != OK) {
            LOG_ERROR("MEM", "Falha ao desmapear pagina de VMA");
            return result;
        }
    }
    return OK;
}

static vm_area_t* process_vma_find_containing(process_t* proc,
                                              uint32_t start_addr,
                                              uint32_t end_addr,
                                              vm_area_t** previous_out) {
    vm_area_t* previous = 0;
    vm_area_t* current = proc ? proc->vma_list : 0;

    while (current) {
        if (start_addr >= current->start_addr &&
            end_addr <= current->end_addr) {
            if (previous_out) *previous_out = previous;
            return current;
        }
        if (current->start_addr > start_addr) break;
        previous = current;
        current = current->next;
    }
    if (previous_out) *previous_out = 0;
    return 0;
}

static vm_area_t* process_vma_find_page(process_t* proc,
                                        uint32_t page_addr) {
    vm_area_t* area = proc ? proc->vma_list : 0;

    while (area) {
        if (page_addr >= area->start_addr && page_addr < area->end_addr) {
            return area;
        }
        if (area->start_addr > page_addr) break;
        area = area->next;
    }
    return 0;
}

static int process_vma_access_allowed(const vm_area_t* area, int write,
                                      int instruction) {
    if (!area || (write != 0 && write != 1) ||
        (instruction != 0 && instruction != 1)) {
        return 0;
    }
    if (write) return (area->flags & VM_WRITE) != 0;
    if (instruction) return (area->flags & VM_EXEC) != 0;
    return (area->flags & VM_READ) != 0;
}

static int process_vma_populate_page(const process_t* proc,
                                     const vm_area_t* area,
                                     uint32_t page_addr, void* physical) {
    if (!proc || !area || !physical) {
        LOG_ERROR("MEM", "Parametros invalidos ao preencher pagina VMA");
        return ERR_NULL;
    }

    kmemset(physical, 0, PAGE_SIZE);
    if (area->start_addr == USER_CODE_BASE) {
        if (!proc->user_code_image || proc->user_code_size > PAGE_SIZE) {
            LOG_ERROR("MEM", "Backing de codigo ring 3 invalido");
            return ERR_STATE;
        }
        kmemcpy(physical, proc->user_code_image, proc->user_code_size);
    } else if (area->start_addr == USER_DATA_BASE) {
        if (proc->user_data_size > PAGE_SIZE ||
            (proc->user_data_size && !proc->user_data_image)) {
            LOG_ERROR("MEM", "Backing de dados ring 3 invalido");
            return ERR_STATE;
        }
        if (proc->user_data_size) {
            kmemcpy(physical, proc->user_data_image,
                    proc->user_data_size);
        }
    } else if (area->start_addr == USER_LAUNCH_BASE) {
        if (sizeof(app_launch_info_t) > PAGE_SIZE) {
            LOG_ERROR("MEM", "Estrutura de lancamento excede uma pagina");
            return ERR_OVERFLOW;
        }
        kmemcpy(physical, &proc->user_launch, sizeof(app_launch_info_t));
    }
    (void)page_addr;
    return OK;
}

static int process_vma_materialize_page(process_t* proc,
                                        const vm_area_t* area,
                                        uint32_t page_addr, int write,
                                        int instruction) {
    page_entry_t* page;
    void* physical;
    union {
        void* pointer;
        uint32_t address;
    } physical_address;
    uint32_t paging_flags;
    int result;

    page = paging_get_page_in_directory(proc->page_directory, page_addr, 0);
    if (page && page->present) {
        if (!page->user || (write && !page->rw)) return ERR_UNAVAILABLE;
        return OK;
    }
    if (!process_vma_access_allowed(area, write, instruction)) {
        return ERR_UNAVAILABLE;
    }

    physical = pmm_alloc_page_in_zone(MEMORY_ZONE_PROCESS);
    if (!physical) {
        LOG_ERROR("MEM", "Falha ao alocar pagina de usuario sob demanda");
        return ERR_MEM;
    }
    result = process_vma_populate_page(proc, area, page_addr, physical);
    if (result != OK) {
        pmm_free_page(physical);
        return result;
    }

    paging_flags = PAGING_FLAG_PRESENT | PAGING_FLAG_USER;
    if (area->flags & VM_WRITE) paging_flags |= PAGING_FLAG_WRITE;
    physical_address.pointer = physical;
    result = paging_map_page_in_directory(proc->page_directory, page_addr,
                                           physical_address.address,
                                           paging_flags);
    if (result != OK) {
        pmm_free_page(physical);
        LOG_ERROR("MEM", "Falha ao mapear pagina de usuario sob demanda");
        return result;
    }
    return OK;
}

static int process_vma_page_fault_failure(int result, const char* message) {
    page_fault_stats.invalid++;
    if (result == ERR_MEM) LOG_ERROR("MEM", message);
    else LOG_WARN("MEM", message);
    return result;
}

int process_vma_ensure_page(struct process* proc, uint32_t address,
                            int write) {
    uint32_t page_addr;
    vm_area_t* area;
    int result;

    result = process_vma_is_current_user((const process_t*)proc);
    if (result != OK) return result;
    if (write != 0 && write != 1) {
        LOG_ERROR("MEM", "Modo de escrita invalido ao garantir pagina VMA");
        return ERR_INVALID;
    }
    if (address < USER_SPACE_START || address >= USER_SPACE_END) {
        LOG_WARN("MEM", "Endereco fora do espaco user ao garantir pagina");
        return ERR_INVALID;
    }
    page_addr = address & ~(PAGE_SIZE - 1U);
    area = process_vma_find_page((process_t*)proc, page_addr);
    if (!area) {
        LOG_WARN("MEM", "Endereco sem VMA ao garantir pagina user");
        return ERR_UNAVAILABLE;
    }
    return process_vma_materialize_page((process_t*)proc, area, page_addr,
                                        write, 0);
}

int process_vma_handle_page_fault(struct process* proc,
                                  uint32_t fault_address,
                                  uint32_t fault_error) {
    uint32_t page_addr;
    vm_area_t* area;
    page_entry_t* page;
    int write;
    int instruction;
    int result;

    result = process_vma_is_current_user((const process_t*)proc);
    if (result != OK) {
        return process_vma_page_fault_failure(result,
                                              "Page fault sem processo user valido");
    }
    if (!(fault_error & VMA_PAGE_FAULT_USER) ||
        (fault_error & VMA_PAGE_FAULT_RESERVED) ||
        fault_address < USER_SPACE_START ||
        fault_address >= USER_SPACE_END) {
        return process_vma_page_fault_failure(ERR_INVALID,
                                              "Page fault fora do contrato user");
    }

    page_addr = fault_address & ~(PAGE_SIZE - 1U);
    area = process_vma_find_page((process_t*)proc, page_addr);
    if (!area) {
        return process_vma_page_fault_failure(ERR_NOT_FOUND,
                                              "Page fault sem VMA correspondente");
    }
    page = paging_get_page_in_directory(proc->page_directory, page_addr, 0);
    if ((fault_error & VMA_PAGE_FAULT_PRESENT) ||
        (page && page->present)) {
        return process_vma_page_fault_failure(ERR_INVALID,
                                              "Page fault em pagina ja presente");
    }

    write = (fault_error & VMA_PAGE_FAULT_WRITE) != 0;
    instruction = (fault_error & VMA_PAGE_FAULT_INSTRUCTION) != 0;
    if (write && instruction) {
        return process_vma_page_fault_failure(ERR_INVALID,
                                              "Page fault com acesso ambiguo");
    }
    if (!process_vma_access_allowed(area, write, instruction)) {
        return process_vma_page_fault_failure(ERR_INVALID,
                                              "Permissao insuficiente na VMA");
    }

    result = process_vma_materialize_page((process_t*)proc, area, page_addr,
                                          write, instruction);
    if (result != OK) {
        return process_vma_page_fault_failure(result,
                                              "Falha ao resolver page fault user");
    }
    page_fault_stats.handled++;
    return OK;
}

int process_vma_get_page_fault_stats(page_fault_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("MEM", "Destino nulo ao consultar page faults");
        return ERR_NULL;
    }
    *stats = page_fault_stats;
    return OK;
}

int process_vma_register_image(process_t* proc, uint32_t code_size) {
    vm_area_t* areas[4];
    uint32_t starts[4] = {
        USER_CODE_BASE, USER_DATA_BASE, USER_LAUNCH_BASE, USER_STACK_BASE
    };
    uint32_t flags[4] = {
        VMA_IMAGE_FLAGS_CODE, VMA_IMAGE_FLAGS_DATA,
        VMA_IMAGE_FLAGS_LAUNCH, VMA_IMAGE_FLAGS_STACK
    };

    if (!proc || code_size == 0 || code_size > PAGE_SIZE) {
        LOG_ERROR("MEM", "Imagem invalida ao registrar VMAs fixas");
        return ERR_INVALID;
    }
    if (proc->vma_list || proc->vma_count != 0) {
        LOG_ERROR("MEM", "Processo ja possui VMAs registradas");
        return ERR_STATE;
    }
    for (uint32_t i = 0; i < 4U; i++) {
        areas[i] = process_vma_create(starts[i], starts[i] + PAGE_SIZE,
                                      flags[i]);
        if (!areas[i]) {
            process_vma_release(proc);
            return ERR_MEM;
        }
        if (process_vma_insert_sorted(proc, areas[i]) != OK) {
            kfree(areas[i]);
            process_vma_release(proc);
            return ERR_STATE;
        }
    }
    return OK;
}

int process_vma_mmap(process_t* proc, uint32_t length,
                     uint32_t protection, uint32_t flags,
                     uint32_t* address_out) {
    uint32_t rounded_length;
    uint32_t address;
    uint32_t end_addr;
    vm_area_t* area;
    int result;

    result = process_vma_is_current_user(proc);
    if (result != OK) return result;
    if (!address_out) {
        LOG_ERROR("MEM", "Destino nulo no mmap de VMA");
        return ERR_NULL;
    }
    if ((protection & ~VMA_PROTECTION_MASK) != 0) {
        LOG_ERROR("MEM", "Protecao invalida no mmap de VMA");
        return ERR_INVALID;
    }
    if ((flags & ~VMA_FLAGS_MASK) != 0 || flags != VM_ANONYMOUS) {
        LOG_ERROR("MEM", "Flags de VMA nao suportadas");
        return ERR_UNAVAILABLE;
    }
    result = process_vma_validate_length(length, &rounded_length);
    if (result != OK) return result;
    if (rounded_length > VMA_USER_MMAP_END - VMA_USER_MMAP_START) {
        LOG_ERROR("MEM", "Tamanho da VMA excede a faixa dinamica");
        return ERR_OVERFLOW;
    }
    result = process_vma_find_gap(proc, rounded_length, &address);
    if (result != OK) return result;
    end_addr = address + rounded_length;
    area = process_vma_create(address, end_addr, protection | flags);
    if (!area) return ERR_MEM;
    result = process_vma_insert_sorted(proc, area);
    if (result != OK) {
        kfree(area);
        return result;
    }
    *address_out = address;
    LOG_DEBUG("MEM", "VMA anonima criada");
    return OK;
}

int process_vma_munmap(process_t* proc, uint32_t address, uint32_t length) {
    uint32_t rounded_length;
    uint32_t end_addr;
    vm_area_t* previous;
    vm_area_t* area;
    vm_area_t* right_area;
    int result;

    result = process_vma_is_current_user(proc);
    if (result != OK) return result;
    if ((address % PAGE_SIZE) != 0) {
        LOG_ERROR("MEM", "Endereco desalinhado no munmap de VMA");
        return ERR_INVALID;
    }
    result = process_vma_validate_length(length, &rounded_length);
    if (result != OK) return result;
    if (address < VMA_USER_MMAP_START ||
        address >= VMA_USER_MMAP_END ||
        rounded_length > VMA_USER_MMAP_END - address) {
        LOG_ERROR("MEM", "Intervalo invalido no munmap de VMA");
        return ERR_INVALID;
    }
    end_addr = address + rounded_length;
    area = process_vma_find_containing(proc, address, end_addr, &previous);
    if (!area || (area->flags & VM_ANONYMOUS) == 0 || area->file) {
        LOG_ERROR("MEM", "Munmap fora de uma VMA anonima dinamica");
        return ERR_INVALID;
    }
    right_area = 0;
    if (address > area->start_addr && end_addr < area->end_addr) {
        right_area = process_vma_create(end_addr, area->end_addr,
                                        area->flags);
        if (!right_area) return ERR_MEM;
        right_area->offset = area->offset + (end_addr - area->start_addr);
    }
    result = process_vma_unmap_pages(proc, address, end_addr);
    if (result != OK) {
        if (right_area) kfree(right_area);
        return result;
    }

    if (address == area->start_addr && end_addr == area->end_addr) {
        if (previous) previous->next = area->next;
        else proc->vma_list = area->next;
        if (proc->vma_count > 0) proc->vma_count--;
        kfree(area);
    } else if (address == area->start_addr) {
        area->start_addr = end_addr;
        area->offset += rounded_length;
    } else if (end_addr == area->end_addr) {
        area->end_addr = address;
    } else {
        right_area->next = area->next;
        area->next = right_area;
        area->end_addr = address;
        proc->vma_count++;
    }
    LOG_DEBUG("MEM", "VMA anonima desmapeada");
    return OK;
}

int process_vma_copy(const process_t* proc, vm_area_info_t* output,
                     uint32_t capacity, uint32_t* out_count) {
    const vm_area_t* area;
    uint32_t count = 0;

    if (!proc || !output || !out_count) {
        LOG_ERROR("MEM", "Destino invalido ao consultar VMAs");
        return ERR_NULL;
    }
    area = proc->vma_list;
    while (area) {
        if (count >= capacity) {
            LOG_ERROR("MEM", "Capacidade insuficiente para listar VMAs");
            *out_count = count;
            return ERR_OVERFLOW;
        }
        output[count].start_addr = area->start_addr;
        output[count].end_addr = area->end_addr;
        output[count].flags = area->flags;
        output[count].offset = area->offset;
        count++;
        area = area->next;
    }
    *out_count = count;
    return OK;
}

void process_vma_release(process_t* proc) {
    vm_area_t* area;

    if (!proc) {
        LOG_ERROR("MEM", "Processo nulo ao liberar metadados de VMA");
        return;
    }
    area = proc->vma_list;
    while (area) {
        vm_area_t* next = area->next;
        kfree(area);
        area = next;
    }
    proc->vma_list = 0;
    proc->vma_count = 0;
}
