#include "process/process.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/timer.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "core/syscall.h"
#include "drivers/tss.h"

#define PROCESS_DEFAULT_EFLAGS 0x202U

process_t processes[MAX_PROCESSES];
static process_t* current_process = 0;
static uint8_t idle_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
uint32_t process_count = 0;
static uint32_t next_pid = 1;
static int last_scheduled_idx = -1;
static int last_user_fault_valid = 0;
static uint32_t last_user_fault_pid = 0;
static uint32_t last_user_fault_vector = 0;
static uint32_t last_user_fault_error = 0;
static uint32_t last_user_fault_address = 0;
static int user_test_result_pending = 0;
static uint32_t user_test_result_pid = 0;
static uint32_t user_test_result_faulted = 0;

static void process_idle_main(void) {
    while (1) {
        asm volatile("hlt");
        process_yield();
    }
}

static uint32_t process_allocate_pid(void) {
    uint32_t candidate;

    for (uint32_t attempt = 0; attempt < MAX_PROCESSES; attempt++) {
        candidate = next_pid;
        if (candidate == 0) candidate = 1;
        next_pid = candidate + 1;
        if (next_pid == 0) next_pid = 1;

        int in_use = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state != PROCESS_STATE_UNUSED &&
                processes[i].pid == candidate) {
                in_use = 1;
                break;
            }
        }
        if (!in_use) return candidate;
    }

    LOG_ERROR("PROC", "Nao foi possivel reservar um PID unico");
    return 0;
}

void process_init(void) {
    current_process = 0;
    process_count = 0;
    next_pid = 1;
    last_scheduled_idx = -1;
    last_user_fault_valid = 0;
    last_user_fault_pid = 0;
    last_user_fault_vector = 0;
    last_user_fault_error = 0;
    last_user_fault_address = 0;
    user_test_result_pending = 0;
    user_test_result_pid = 0;
    user_test_result_faulted = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        kmemset(&processes[i], 0, sizeof(process_t));
        processes[i].state = PROCESS_STATE_UNUSED;
    }
    scheduler_init();
    LOG_INFO("PROC", "Gerenciador de processos inicializado");
}

void process_bootstrap_idle(void) {
    process_t* proc = &processes[0];
    kmemset(proc, 0, sizeof(process_t));
    proc->pid = 0;
    
    const char* name = "System Idle";
    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    
    proc->state = PROCESS_STATE_RUNNING;
    proc->total_ticks = 0;
    proc->wait_ticks = 0;
    
    /* O Idle precisa de uma stack de retorno valida quando o scheduler
       precisar voltar a ele depois de trocar para outro processo. */
    proc->kernel_stack = (uint32_t)idle_stack;
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
    proc->page_directory = paging_get_current_directory();
    proc->context.esp = proc->kernel_stack_top;
    proc->context.eip = (uint32_t)process_idle_main;
    proc->context.eflags = PROCESS_DEFAULT_EFLAGS;
    proc->context.cs = KERNEL_CODE_SELECTOR;
    proc->context.ss = KERNEL_DATA_SELECTOR;
    proc->context.ds = KERNEL_DATA_SELECTOR;
    proc->context.es = KERNEL_DATA_SELECTOR;
    proc->context.fs = KERNEL_DATA_SELECTOR;
    proc->context.gs = KERNEL_DATA_SELECTOR;
    proc->context.cr3 = (uint32_t)proc->page_directory;
    
    current_process = proc;
    process_count = 1;
    LOG_INFO("PROC", "Processo Idle inicializado");
}

process_t* process_create(const char* name, void (*entry_point)()) {
    process_t* proc = 0;

    if (!name || !entry_point) {
        LOG_ERROR("PROC", "Parametros invalidos ao criar processo");
        return 0;
    }
    if (!paging_get_current_directory()) {
        LOG_ERROR("PROC", "Paging indisponivel ao criar processo");
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }

    if (!proc) {
        LOG_ERROR("PROC", "Limite de processos atingido");
        return 0;
    }

    kmemset(proc, 0, sizeof(process_t));

    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';

    proc->pid = process_allocate_pid();
    if (!proc->pid) {
        LOG_ERROR("PROC", "Falha ao reservar PID do processo");
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    /* O processo permanece invisivel ao scheduler ate o contexto estar pronto. */
    proc->total_ticks = 0;
    proc->wait_ticks = 0;

    proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        LOG_ERROR("PROC", "Falha ao alocar stack do processo");
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;

    /* Os aplicativos nativos continuam compartilhando o espaco do kernel. */
    proc->page_directory = paging_get_current_directory();

    uint32_t stack = proc->kernel_stack_top;
    uint32_t* stack_ptr = (uint32_t*)stack;

    stack_ptr--;
    *stack_ptr = PROCESS_DEFAULT_EFLAGS;
    stack_ptr--;
    *stack_ptr = 0x08;
    stack_ptr--;
    *stack_ptr = (uint32_t)entry_point;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = stack;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;

    proc->context.esp = (uint32_t)stack_ptr;
    proc->context.eip = (uint32_t)entry_point;
    proc->context.eflags = PROCESS_DEFAULT_EFLAGS;
    proc->context.cs = 0x08;
    proc->context.ds = 0x10;
    proc->context.es = 0x10;
    proc->context.fs = 0x10;
    proc->context.gs = 0x10;
    proc->context.ss = 0x10;
    proc->context.cr3 = (uint32_t)proc->page_directory;
    proc->context.user_entry = 0;
    proc->context.user_mode = 0;
    proc->user_test = 0;

    proc->state = PROCESS_STATE_READY;
    process_count++;
    LOG_INFO("PROC", "Processo criado com sucesso");
    return proc;
}

static int process_pointer_valid(const process_t* proc) {
    uint32_t address;
    uint32_t start;
    uint32_t end;

    if (!proc) return 0;
    address = (uint32_t)proc;
    start = (uint32_t)&processes[0];
    end = start + sizeof(processes);
    return address >= start && address < end &&
           ((address - start) % sizeof(process_t)) == 0;
}

static const char user_test_message[] = "Zephyr ring3 OK\n";

static void process_user_patch_u32(uint8_t* code, uint32_t offset,
                                   uint32_t value) {
    code[offset + 0] = (uint8_t)(value & 0xFF);
    code[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    code[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    code[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static void process_user_emit_mov(uint8_t* code, uint32_t* offset,
                                  uint8_t reg, uint32_t value) {
    code[(*offset)++] = (uint8_t)(0xB8 + reg);
    process_user_patch_u32(code, *offset, value);
    *offset += 4;
}

static uint32_t process_user_build_code(uint8_t* code, int trigger_fault) {
    uint32_t offset = 0;

    if (trigger_fault) {
        code[offset++] = 0xA1;
        process_user_patch_u32(code, offset, 0);
        offset += 4;
        code[offset++] = 0xEB;
        code[offset++] = 0xFE;
        return offset;
    }

    process_user_emit_mov(code, &offset, 0, 1);
    process_user_emit_mov(code, &offset, 3, USER_DATA_BASE);
    process_user_emit_mov(code, &offset, 1, kstrlen(user_test_message));
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    process_user_emit_mov(code, &offset, 0, 2);
    process_user_emit_mov(code, &offset, 3, USER_DATA_BASE + 64);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    process_user_emit_mov(code, &offset, 0, 3);
    process_user_emit_mov(code, &offset, 3, USER_DATA_BASE + 128);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    process_user_emit_mov(code, &offset, 0, 0);
    code[offset++] = 0x31;
    code[offset++] = 0xDB;
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    /* Se a syscall nao encerrar o processo, HLT provoca uma excecao
       controlada em ring 3 em vez de consumir CPU indefinidamente. */
    code[offset++] = 0xF4;
    return offset;
}

static void process_switch_after_termination(void) {
    process_t* previous = current_process;
    process_t* next = scheduler_schedule();

    if (!next || next == previous) {
        next = &processes[0];
    }
    if (next == previous) {
        LOG_ERROR("PROC", "Nao foi possivel sair de processo encerrado");
        return;
    }

    current_process = next;
    next->state = PROCESS_STATE_RUNNING;
    if (next->kernel_stack_top) tss_set_kernel_stack(next->kernel_stack_top);
    if (next->page_directory &&
        next->page_directory != paging_get_current_directory()) {
        paging_switch_directory(next->page_directory);
    }
    process_context_switch(&previous->context, &next->context);
}

static int process_mark_current_user_zombie(uint32_t exit_code,
                                             int faulted) {
    if (!current_process || !process_is_user(current_process)) {
        LOG_WARN("PROC", "Encerramento recusado para processo ring 0");
        return ERR_UNAVAILABLE;
    }
    if (current_process->state != PROCESS_STATE_RUNNING &&
        current_process->state != PROCESS_STATE_READY) {
        LOG_WARN("PROC", "Estado invalido ao encerrar processo ring 3");
        return ERR_STATE;
    }

    current_process->exit_code = exit_code;
    current_process->state = PROCESS_STATE_ZOMBIE;
    user_test_result_pending = current_process->user_test;
    user_test_result_pid = current_process->pid;
    user_test_result_faulted = faulted ? 1U : 0U;
    return OK;
}

int process_reap_finished_user(void) {
    if (!current_process) {
        LOG_ERROR("PROC", "Reaper de usuario executado sem processo atual");
        return ERR_STATE;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &processes[i];

        if (!proc->context.user_mode ||
            proc->state != PROCESS_STATE_ZOMBIE) continue;
        if (proc->user_test && user_test_result_pending) {
            continue;
        }

        process_destroy(proc);
    }
    return OK;
}

static int process_user_reap_previous_test(void) {
    int result = process_reap_finished_user();

    if (result != OK) return result;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processes[i].user_test ||
            processes[i].state == PROCESS_STATE_UNUSED) continue;
        if (processes[i].state != PROCESS_STATE_ZOMBIE) {
            LOG_WARN("PROC", "Teste ring 3 ja esta em execucao");
            return ERR_STATE;
        }
        if (user_test_result_pending) {
            LOG_WARN("PROC", "Resultado do teste ring 3 ainda nao foi reportado");
            return ERR_STATE;
        }
    }
    return OK;
}

static int process_user_map_image(page_directory_t* dir) {
    uint32_t code_phys;
    uint32_t data_phys;
    uint32_t stack_phys;
    int code_mapped = 0;
    int data_mapped = 0;
    int stack_mapped = 0;

    if (!dir) {
        LOG_ERROR("PROC", "Diretorio nulo para imagem ring 3");
        return ERR_NULL;
    }
    code_phys = (uint32_t)pmm_alloc_page();
    data_phys = (uint32_t)pmm_alloc_page();
    stack_phys = (uint32_t)pmm_alloc_page();
    if (!code_phys || !data_phys || !stack_phys) {
        if (code_phys) pmm_free_page((void*)code_phys);
        if (data_phys) pmm_free_page((void*)data_phys);
        if (stack_phys) pmm_free_page((void*)stack_phys);
        LOG_ERROR("PROC", "Memoria insuficiente para paginas ring 3");
        return ERR_MEM;
    }

    code_mapped = paging_map_page_in_directory(
        dir, USER_CODE_BASE, code_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_USER) == OK;
    data_mapped = paging_map_page_in_directory(
        dir, USER_DATA_BASE, data_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE | PAGING_FLAG_USER) == OK;
    stack_mapped = paging_map_page_in_directory(
        dir, USER_STACK_BASE, stack_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE | PAGING_FLAG_USER) == OK;

    if (!code_mapped || !data_mapped || !stack_mapped) {
        if (!code_mapped) pmm_free_page((void*)code_phys);
        if (!data_mapped) pmm_free_page((void*)data_phys);
        if (!stack_mapped) pmm_free_page((void*)stack_phys);
        LOG_ERROR("PROC", "Falha ao mapear imagem do teste ring 3");
        return ERR_MEM;
    }
    return OK;
}

static int process_user_load_image(page_directory_t* dir,
                                   page_directory_t* kernel_dir,
                                   const uint8_t* code,
                                   uint32_t code_size,
                                   const uint8_t* data,
                                   uint32_t data_size) {
    if (!dir || !kernel_dir || !code || code_size == 0 ||
        code_size > PAGE_SIZE || data_size > PAGE_SIZE ||
        (data_size > 0 && !data)) {
        LOG_ERROR("PROC", "Imagem ring 3 invalida para carregamento");
        return ERR_INVALID;
    }

    asm volatile("cli");
    paging_switch_directory(dir);
    kmemset((void*)USER_CODE_BASE, 0, PAGE_SIZE);
    kmemset((void*)USER_DATA_BASE, 0, PAGE_SIZE);
    kmemset((void*)USER_STACK_BASE, 0, PAGE_SIZE);
    kmemcpy((void*)USER_CODE_BASE, code, code_size);
    if (data_size > 0) {
        kmemcpy((void*)USER_DATA_BASE, data, data_size);
    }
    paging_switch_directory(kernel_dir);
    asm volatile("sti");
    return OK;
}

static int process_user_initialize(process_t* proc, page_directory_t* dir,
                                   uint32_t kernel_stack, const char* name,
                                   uint32_t entry_offset, int diagnostic_test) {
    uint32_t stack_ptr;
    uint32_t entry_point;
    int i = 0;

    if (!proc || !dir || !kernel_stack || !name ||
        entry_offset >= PAGE_SIZE) {
        LOG_ERROR("PROC", "Parametros invalidos para processo ring 3");
        return ERR_NULL;
    }
    kmemset(proc, 0, sizeof(process_t));
    proc->pid = process_allocate_pid();
    if (!proc->pid) {
        LOG_ERROR("PROC", "Falha ao reservar PID do processo ring 3");
        return ERR_MEM;
    }
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    proc->page_directory = dir;
    proc->kernel_stack = kernel_stack;
    proc->kernel_stack_top = kernel_stack + KERNEL_STACK_SIZE;
    entry_point = USER_CODE_BASE + entry_offset;
    stack_ptr = proc->kernel_stack_top;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = USER_DATA_SELECTOR;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = USER_STACK_TOP;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = PROCESS_DEFAULT_EFLAGS;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = USER_CODE_SELECTOR;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = entry_point;
    proc->context.esp = stack_ptr;
    proc->context.eip = (uint32_t)process_user_enter;
    proc->context.eflags = PROCESS_DEFAULT_EFLAGS;
    proc->context.cs = USER_CODE_SELECTOR;
    proc->context.ss = USER_DATA_SELECTOR;
    proc->context.ds = USER_DATA_SELECTOR;
    proc->context.es = USER_DATA_SELECTOR;
    proc->context.fs = USER_DATA_SELECTOR;
    proc->context.gs = USER_DATA_SELECTOR;
    proc->context.cr3 = (uint32_t)dir;
    proc->context.user_entry = entry_point;
    proc->context.user_mode = 1;
    proc->user_test = diagnostic_test ? 1U : 0U;
    proc->state = PROCESS_STATE_READY;
    process_count++;
    return OK;
}

int process_create_user_image(const char* name, const uint8_t* code,
                              uint32_t code_size, const uint8_t* data,
                              uint32_t data_size, uint32_t entry_offset,
                              uint32_t stack_size, int diagnostic_test,
                              uint32_t* pid_out) {
    process_t* proc = 0;
    page_directory_t* dir;
    page_directory_t* kernel_dir;
    uint32_t kernel_stack;
    int result;

    if (!paging_is_ready() || !tss_is_ready() ||
        !syscall_user_mode_is_enabled()) {
        LOG_WARN("PROC", "Modo usuario indisponivel para processo");
        return ERR_UNAVAILABLE;
    }
    if (!name || !code || code_size == 0 || code_size > PAGE_SIZE ||
        data_size > PAGE_SIZE || (data_size > 0 && !data) ||
        entry_offset >= code_size || stack_size != PAGE_SIZE) {
        LOG_ERROR("PROC", "Parametros invalidos para imagem ring 3");
        return ERR_INVALID;
    }
    if (diagnostic_test) {
        result = process_user_reap_previous_test();
        if (result != OK) return result;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }
    if (!proc) {
        LOG_ERROR("PROC", "Limite de processos ring 3 atingido");
        return ERR_MEM;
    }

    kernel_dir = paging_get_current_directory();
    kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!kernel_stack) {
        LOG_ERROR("PROC", "Falha ao alocar stack do teste ring 3");
        return ERR_MEM;
    }
    dir = paging_create_user_directory();
    if (!dir) {
        kfree((void*)kernel_stack);
        LOG_ERROR("PROC", "Falha ao criar espaco do teste ring 3");
        return ERR_MEM;
    }
    result = process_user_map_image(dir);
    if (result != OK) {
        paging_free_user_directory(dir);
        kfree((void*)kernel_stack);
        return result;
    }
    result = process_user_load_image(dir, kernel_dir, code, code_size,
                                     data, data_size);
    if (result != OK) {
        paging_free_user_directory(dir);
        kfree((void*)kernel_stack);
        return result;
    }
    result = process_user_initialize(proc, dir, kernel_stack, name,
                                     entry_offset, diagnostic_test);
    if (result != OK) {
        paging_free_user_directory(dir);
        kfree((void*)kernel_stack);
        return result;
    }
    if (pid_out) *pid_out = proc->pid;
    LOG_INFO("PROC", "Processo ring 3 criado a partir de imagem");
    return OK;
}

int process_create_user_test(int trigger_fault, uint32_t* pid_out) {
    uint8_t code[128];
    uint32_t code_size = process_user_build_code(code, trigger_fault);

    return process_create_user_image("UserTest", code, code_size,
                                     user_test_message,
                                     kstrlen(user_test_message), 0,
                                     PAGE_SIZE, 1, pid_out);
}

uint32_t process_get_user_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED &&
            processes[i].context.user_mode) count++;
    }
    return count;
}

int process_is_user(const process_t* proc) {
    return process_pointer_valid(proc) && proc->context.user_mode;
}

int process_exit_current(uint32_t exit_code) {
    int result = process_mark_current_user_zombie(exit_code, 0);

    if (result != OK) return result;
    LOG_INFO("PROC", "Processo ring 3 encerrado por syscall");
    return OK;
}

int process_handle_user_exception(registers_t* regs) {
    uint32_t fault_address = 0;

    if (!regs || !current_process || !process_is_user(current_process) ||
        ((regs->cs & 0x03U) != 0x03U)) {
        return ERR_STATE;
    }
    if (regs->int_no == 14) {
        asm volatile("mov %%cr2, %0" : "=r"(fault_address));
    } else {
        fault_address = regs->eip;
    }
    current_process->fault_vector = regs->int_no;
    current_process->fault_error = regs->err_code;
    current_process->fault_address = fault_address;
    current_process->faulted = 1;
    last_user_fault_valid = 1;
    last_user_fault_pid = current_process->pid;
    last_user_fault_vector = regs->int_no;
    last_user_fault_error = regs->err_code;
    last_user_fault_address = fault_address;
    if (process_mark_current_user_zombie(ERR_STATE, 1) != OK) {
        LOG_ERROR("PROC", "Falha ao encerrar processo apos excecao de usuario");
        return ERR_STATE;
    }
    LOG_WARN("PROC", "Excecao de usuario isolada; processo encerrado");
    return OK;
}

int process_prepare_user_termination(registers_t* regs) {
    if (!regs) {
        LOG_ERROR("PROC", "Registradores nulos ao preparar retorno de usuario");
        return ERR_NULL;
    }
    if (!current_process || !process_is_user(current_process) ||
        current_process->state != PROCESS_STATE_ZOMBIE ||
        (regs->cs & 0x03U) != 0x03U) {
        LOG_ERROR("PROC", "Retorno de usuario invalido para processo encerrado");
        return ERR_STATE;
    }

    /* O IRET sai da pilha da interrupcao antes de trocar de contexto.
       Assim, nunca reutilizamos um frame de ring 3 ja encerrado. */
    regs->eip = (uint32_t)process_user_termination_enter;
    regs->cs = KERNEL_CODE_SELECTOR;
    /* Nenhuma IRQ deve interromper a pequena janela entre o IRET e a
       troca de contexto. O contexto seguinte restaura seus proprios flags. */
    regs->eflags &= ~0x200U;
    return OK;
}

void process_finish_user_termination(void) {
    if (!current_process || !process_is_user(current_process) ||
        current_process->state != PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Trampoline de encerramento sem processo ring 3 zombie");
        while (1) asm volatile("hlt");
    }

    process_switch_after_termination();
    LOG_ERROR("PROC", "Troca de contexto retornou apos encerrar usuario");
    while (1) asm volatile("hlt");
}

int process_take_user_test_result(uint32_t* pid, uint32_t* faulted) {
    if (!pid || !faulted) {
        LOG_ERROR("PROC", "Destino nulo ao consultar resultado do UserTest");
        return ERR_NULL;
    }
    if (!user_test_result_pending) return ERR_NOT_FOUND;

    *pid = user_test_result_pid;
    *faulted = user_test_result_faulted;
    user_test_result_pending = 0;
    return OK;
}

void process_destroy(process_t* proc) {
    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao destruir processo");
        return;
    }
    if (proc->state == PROCESS_STATE_UNUSED) {
        LOG_WARN("PROC", "Processo ja esta inutilizado");
        return;
    }
    if (proc->state < PROCESS_STATE_UNUSED ||
        proc->state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao destruir processo");
        return;
    }
    if (proc == current_process || proc->pid == 0) {
        LOG_WARN("PROC", "Destruicao do processo atual ou Idle bloqueada");
        return;
    }
    if (proc->page_directory &&
        proc->page_directory == paging_get_current_directory() &&
        proc->context.user_mode) {
        LOG_WARN("PROC", "Diretorio ativo impede destruicao do processo");
        return;
    }

    if (process_get_focus() == proc->pid) {
        process_set_focus(0);
    }
    proc->state = PROCESS_STATE_UNUSED;
    if (proc->kernel_stack) {
        kfree((void*)proc->kernel_stack);
        proc->kernel_stack = 0;
    }
    if (proc->page_directory && proc->context.user_mode) {
        paging_free_user_directory(proc->page_directory);
    }
    if (process_count > 0) process_count--;
    kmemset(proc, 0, sizeof(process_t));
    proc->state = PROCESS_STATE_UNUSED;
    LOG_INFO("PROC", "Processo destruido");
}

process_t* process_get_current(void) {
    return current_process;
}

uint32_t process_get_count(void) {
    return process_count;
}

int process_get_last_user_fault(uint32_t* pid, uint32_t* vector,
                                uint32_t* error, uint32_t* address) {
    if (!pid || !vector || !error || !address) {
        LOG_ERROR("PROC", "Destino nulo ao consultar ultima falha de usuario");
        return ERR_NULL;
    }
    if (!last_user_fault_valid) return ERR_NOT_FOUND;

    *pid = last_user_fault_pid;
    *vector = last_user_fault_vector;
    *error = last_user_fault_error;
    *address = last_user_fault_address;
    return OK;
}

process_t* process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED &&
            processes[i].pid == pid) {
            return &processes[i];
        }
    }

    LOG_DEBUG("PROC", "PID nao encontrado");
    return 0;
}

uint32_t process_get_current_pid(void) {
    if (!current_process) {
        LOG_WARN("PROC", "Processo atual indisponivel");
        return 0;
    }
    return current_process->pid;
}

uint32_t process_get_state_count(process_state_t state) {
    uint32_t count = 0;

    if (state < PROCESS_STATE_UNUSED || state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao contar processos");
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == state) count++;
    }
    return count;
}

process_t* scheduler_schedule(void) {
    process_t* best = 0;

    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (last_scheduled_idx + i) % MAX_PROCESSES;
        if (processes[idx].state == PROCESS_STATE_READY) {
            best = &processes[idx];
            last_scheduled_idx = idx;
            break;
        }
    }

    if (!best && processes[0].state != PROCESS_STATE_UNUSED &&
        processes[0].state != PROCESS_STATE_ZOMBIE) {
        if (processes[0].state != PROCESS_STATE_RUNNING) {
            processes[0].state = PROCESS_STATE_READY;
        }
        return &processes[0];
    }

    if (!best) LOG_WARN("PROC", "Nenhum processo pronto para escalonar");
    return best;
}

void process_yield(void) {
    /* O timer pode disparar antes do bootstrap do Idle; isso nao e falha. */
    if (!current_process) {
        return;
    }
    if (current_process->state < PROCESS_STATE_READY ||
        current_process->state > PROCESS_STATE_BLOCKED) {
        return;
    }
    
    process_t* next = scheduler_schedule();
    if (!next) return;
    if (next != current_process) {
        process_t* prev = current_process;
        current_process = next;
        current_process->state = PROCESS_STATE_RUNNING;

        if (prev && prev->state == PROCESS_STATE_RUNNING) {
            prev->state = PROCESS_STATE_READY;
        }

        if (next->kernel_stack_top) {
            tss_set_kernel_stack(next->kernel_stack_top);
        }
        if (next->page_directory &&
            next->page_directory != paging_get_current_directory()) {
            paging_switch_directory(next->page_directory);
        }
        process_context_switch(&prev->context, &next->context);
    }
}

void process_block(uint32_t ticks) {
    if (!current_process) {
        LOG_ERROR("PROC", "Tentativa de bloquear sem processo atual");
        return;
    }
    if (current_process->pid == 0 ||
        current_process->state != PROCESS_STATE_RUNNING) {
        LOG_WARN("PROC", "Estado invalido para bloquear processo");
        return;
    }
    if (ticks == 0) {
        LOG_WARN("PROC", "Bloqueio solicitado com duracao zero");
        return;
    }
    current_process->state = PROCESS_STATE_BLOCKED;
    current_process->wait_ticks = ticks;
    process_yield();
}

void process_unblock(process_t* proc) {
    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao desbloquear processo");
        return;
    }
    if (proc->state < PROCESS_STATE_UNUSED ||
        proc->state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao desbloquear processo");
        return;
    }
    if (proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
        LOG_DEBUG("PROC", "Processo desbloqueado");
    } else {
        LOG_DEBUG("PROC", "Processo nao estava bloqueado");
    }
}

void scheduler_init(void) {
    process_count = 0;
    last_scheduled_idx = -1;
    LOG_INFO("PROC", "Scheduler round-robin inicializado");
}

void scheduler_tick(void) {
    if (!current_process) return;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_BLOCKED) {
            if (processes[i].wait_ticks > 0) {
                processes[i].wait_ticks--;
                if (processes[i].wait_ticks == 0) {
                    processes[i].state = PROCESS_STATE_READY;
                }
            }
        }
    }

    current_process->total_ticks++;
}
