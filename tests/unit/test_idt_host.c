#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/panic.h"
#include "core/test_protocol.h"
#include "core/video.h"
#include "drivers/idt.h"
#include "drivers/tss.h"
#include "process/process.h"
#include "process/signal.h"

extern idt_entry_t idt[256];
void isr_handler(registers_t* regs);
void irq_handler(registers_t* regs);

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define PIC_MASTER_DATA_PORT 0x21U
#define PIC_SLAVE_DATA_PORT 0xA1U
#define PIC_MASTER_COMMAND_PORT 0x20U
#define PIC_SLAVE_COMMAND_PORT 0xA0U
#define INTERRUPT_ENABLE_FLAG (1U << 9U)

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t pic_master_mask;
static uint8_t pic_slave_mask;
static uint32_t pic_master_eoi_count;
static uint32_t pic_slave_eoi_count;
static uint32_t host_cli_count;
static uint32_t host_sti_count;
static uint32_t host_load_count;
static idt_ptr_t host_loaded_idt;
static uint32_t host_flags = INTERRUPT_ENABLE_FLAG;
static int fake_tss_ready;
static int signal_prepare_result = OK;
static uint32_t direct_handler_calls;
static uint32_t shared_handler_calls;
static jmp_buf panic_jump;
static uint8_t panic_jump_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void video_clear(void) {}
void video_flush_updates(void) {}
void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}
void video_set_color(uint8_t foreground, uint8_t background) {
    (void)foreground;
    (void)background;
}

void test_protocol_panic(const char* reason) {
    (void)reason;
}

void panic_halt(void) {
    if (panic_jump_active) longjmp(panic_jump, 1);
}

void panic(const char* message) {
    (void)message;
    panic_halt();
}

void panic_memory(const char* message, uint32_t mmap_entries,
                  uint32_t total_memory, uint32_t free_memory,
                  uint32_t free_pages) {
    (void)message;
    (void)mmap_entries;
    (void)total_memory;
    (void)free_memory;
    (void)free_pages;
    panic_halt();
}

void idt_host_outb(uint16_t port, uint8_t value) {
    if (port == PIC_MASTER_DATA_PORT) {
        pic_master_mask = value;
    } else if (port == PIC_SLAVE_DATA_PORT) {
        pic_slave_mask = value;
    } else if (port == PIC_MASTER_COMMAND_PORT && value == 0x20U) {
        pic_master_eoi_count++;
    } else if (port == PIC_SLAVE_COMMAND_PORT && value == 0x20U) {
        pic_slave_eoi_count++;
    }
}

uint8_t idt_host_inb(uint16_t port) {
    if (port == PIC_MASTER_DATA_PORT) return pic_master_mask;
    if (port == PIC_SLAVE_DATA_PORT) return pic_slave_mask;
    return 0U;
}

uint32_t idt_host_read_flags(void) {
    return host_flags;
}

void idt_host_cli(void) {
    host_cli_count++;
    host_flags &= ~INTERRUPT_ENABLE_FLAG;
}

void idt_host_sti(void) {
    host_sti_count++;
    host_flags |= INTERRUPT_ENABLE_FLAG;
}

void idt_host_load(const idt_ptr_t* pointer) {
    if (pointer) host_loaded_idt = *pointer;
    host_load_count++;
}

int tss_is_ready(void) {
    return fake_tss_ready;
}

int process_signal_prepare_user_return(registers_t* regs) {
    (void)regs;
    return signal_prepare_result;
}

int process_handle_user_exception(registers_t* regs) {
    (void)regs;
    return ERR_STATE;
}

process_t* process_get_current(void) {
    return NULL;
}

int process_prepare_user_termination(registers_t* regs) {
    (void)regs;
    return ERR_STATE;
}

#define DEFINE_STUB(name) void name(void) {}
DEFINE_STUB(isr0)
DEFINE_STUB(isr1)
DEFINE_STUB(isr2)
DEFINE_STUB(isr3)
DEFINE_STUB(isr4)
DEFINE_STUB(isr5)
DEFINE_STUB(isr6)
DEFINE_STUB(isr7)
DEFINE_STUB(isr8)
DEFINE_STUB(isr9)
DEFINE_STUB(isr10)
DEFINE_STUB(isr11)
DEFINE_STUB(isr12)
DEFINE_STUB(isr13)
DEFINE_STUB(isr14)
DEFINE_STUB(isr15)
DEFINE_STUB(isr16)
DEFINE_STUB(isr17)
DEFINE_STUB(isr18)
DEFINE_STUB(isr19)
DEFINE_STUB(isr20)
DEFINE_STUB(isr21)
DEFINE_STUB(isr22)
DEFINE_STUB(isr23)
DEFINE_STUB(isr24)
DEFINE_STUB(isr25)
DEFINE_STUB(isr26)
DEFINE_STUB(isr27)
DEFINE_STUB(isr28)
DEFINE_STUB(isr29)
DEFINE_STUB(isr30)
DEFINE_STUB(isr31)
DEFINE_STUB(isr128)
DEFINE_STUB(irq0)
DEFINE_STUB(irq1)
DEFINE_STUB(irq2)
DEFINE_STUB(irq3)
DEFINE_STUB(irq4)
DEFINE_STUB(irq5)
DEFINE_STUB(irq6)
DEFINE_STUB(irq7)
DEFINE_STUB(irq8)
DEFINE_STUB(irq9)
DEFINE_STUB(irq10)
DEFINE_STUB(irq11)
DEFINE_STUB(irq12)
DEFINE_STUB(irq13)
DEFINE_STUB(irq14)
DEFINE_STUB(irq15)

static void direct_handler(registers_t* regs) {
    (void)regs;
    direct_handler_calls++;
}

static void direct_handler_alt(registers_t* regs) {
    (void)regs;
}

static void shared_handler_one(registers_t* regs) {
    (void)regs;
    shared_handler_calls++;
}

static void shared_handler_two(registers_t* regs) {
    (void)regs;
    shared_handler_calls++;
}

static void shared_handler_three(registers_t* regs) {
    (void)regs;
    shared_handler_calls++;
}

static void shared_handler_four(registers_t* regs) {
    (void)regs;
    shared_handler_calls++;
}

static void shared_handler_five(registers_t* regs) {
    (void)regs;
}

static int check_initial_state(void) {
    uint8_t count;
    idt_irq_status_t status;

    if (idt_is_user_syscall_enabled() != 0) return 1;
    if (idt_get_shared_irq_handler_count(0U, &count) != OK || count != 0U) {
        return 2;
    }
    if (idt_get_shared_irq_handler_count(0U, NULL) != ERR_NULL) return 3;
    if (idt_get_shared_irq_handler_count(IDT_IRQ_LINE_COUNT, &count) !=
        ERR_INVALID) return 4;
    if (idt_get_irq_status(0U, &status) != ERR_STATE) return 5;
    if (idt_get_irq_status(0U, NULL) != ERR_NULL) return 6;
    if (idt_validate_irq_state() != ERR_STATE) return 7;
    if (idt_unmask_irq(0U) != ERR_STATE) return 8;
    if (idt_unmask_irq(IDT_IRQ_LINE_COUNT) != ERR_STATE) return 9;
    if (idt_enable_user_syscall() != ERR_STATE) return 10;
    if (idt_register_handler(0U, NULL) != ERR_NULL) return 11;
    if (idt_register_shared_irq_handler(0U, NULL) != ERR_NULL) return 12;
    if (idt_register_shared_irq_handler(IDT_IRQ_LINE_COUNT, direct_handler) !=
        ERR_INVALID) return 13;
    return 0;
}

static int check_initialization(void) {
    idt_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    idt_init();
    if (host_load_count != 1U || host_loaded_idt.limit !=
        sizeof(idt_entry_t) * 256U - 1U) return 20;
    if (host_loaded_idt.base == 0U) return 21;
    if (host_sti_count != 1U || idt_validate_irq_state() != OK) return 22;
    entry = idt[0];
    if (entry.selector != 0x08U || entry.flags != 0x8EU || entry.always0 != 0U) {
        return 23;
    }
    if (idt[128].selector != 0x08U || idt[128].flags != 0x8EU) return 24;
    idt_set_gate(200U, 0x12345678U, 0x0040U, 0x9AU);
    if (idt[200].base_low != 0x5678U || idt[200].base_high != 0x1234U ||
        idt[200].selector != 0x0040U || idt[200].flags != 0x9AU ||
        idt[200].always0 != 0U) return 25;
    return 0;
}

static int check_handler_registration(void) {
    uint8_t count;

    if (idt_register_handler(33U, NULL) != ERR_NULL) return 30;
    if (idt_register_handler(33U, direct_handler) != OK) return 31;
    if (idt_register_handler(33U, direct_handler) != OK) return 32;
    if (idt_register_handler(33U, direct_handler_alt) != ERR_STATE) return 33;
    if (idt_register_shared_irq_handler(3U, shared_handler_one) != OK) return 34;
    if (idt_register_shared_irq_handler(3U, shared_handler_one) != OK) return 35;
    if (idt_register_shared_irq_handler(3U, shared_handler_two) != OK) return 36;
    if (idt_register_shared_irq_handler(3U, shared_handler_three) != OK) return 37;
    if (idt_register_shared_irq_handler(3U, shared_handler_four) != OK) return 38;
    if (idt_register_shared_irq_handler(3U, shared_handler_five) !=
        ERR_OVERFLOW) return 39;
    if (idt_get_shared_irq_handler_count(3U, &count) != OK || count != 4U) {
        return 40;
    }
    if (idt_get_shared_irq_handler_count(3U, NULL) != ERR_NULL) return 41;
    if (idt_get_shared_irq_handler_count(IDT_IRQ_LINE_COUNT, &count) !=
        ERR_INVALID) return 42;
    return 0;
}

static int check_pic_operations(void) {
    uint8_t count;
    idt_irq_status_t status;

    pic_master_mask = 0xFFU;
    pic_slave_mask = 0xFFU;
    host_flags = INTERRUPT_ENABLE_FLAG;
    if (idt_unmask_irq(0U) != OK || pic_master_mask != 0xFEU) return 50;
    if (idt_unmask_irq(10U) != OK || pic_slave_mask != 0xFBU) return 51;
    if (idt_unmask_irq(IDT_IRQ_LINE_COUNT) != ERR_INVALID) return 52;
    if (idt_get_irq_status(1U, &status) != OK || status.irq_line != 1U ||
        status.registered_handlers != 1U || status.occurrences != 0U) return 53;
    if (idt_get_irq_status(1U, NULL) != ERR_NULL) return 54;
    if (idt_get_irq_status(IDT_IRQ_LINE_COUNT, &status) != ERR_INVALID) return 55;
    if (idt_get_shared_irq_handler_count(3U, &count) != OK || count != 4U) {
        return 56;
    }
    return 0;
}

static int check_irq_dispatch(void) {
    registers_t regs;
    idt_irq_status_t status;

    memset(&regs, 0, sizeof(regs));
    irq_handler(NULL);
    regs.int_no = 31U;
    irq_handler(&regs);
    regs.int_no = 48U;
    irq_handler(&regs);
    if (direct_handler_calls != 0U || shared_handler_calls != 0U) return 61;
    regs.int_no = 33U;
    irq_handler(&regs);
    if (direct_handler_calls != 1U || pic_master_eoi_count != 1U ||
        pic_slave_eoi_count != 0U) return 62;
    regs.int_no = 35U;
    irq_handler(&regs);
    if (shared_handler_calls != 4U || pic_master_eoi_count != 2U) return 63;
    if (idt_get_irq_status(1U, &status) != OK || status.occurrences != 1U ||
        status.registered_handlers != 1U) return 64;
    if (idt_get_irq_status(3U, &status) != OK || status.occurrences != 1U ||
        status.registered_handlers != 4U) return 65;
    regs.int_no = 42U;
    irq_handler(&regs);
    if (pic_slave_eoi_count != 1U) return 66;
    if (signal_prepare_result != OK) return 67;
    signal_prepare_result = ERR_STATE;
    regs.int_no = 33U;
    irq_handler(&regs);
    signal_prepare_result = OK;
    if (direct_handler_calls != 2U) return 68;
    if (idt_validate_irq_state() != OK) return 69;
    return 0;
}

static int check_syscall_and_panic(void) {
    registers_t regs;
    uint32_t panic_result;

    fake_tss_ready = 0;
    if (idt_enable_user_syscall() != ERR_STATE) return 70;
    fake_tss_ready = 1;
    if (idt_enable_user_syscall() != OK) return 71;
    if (idt_is_user_syscall_enabled() != 1 || idt[128].flags != 0xEEU ||
        idt[128].selector != KERNEL_CODE_SELECTOR) return 72;
    memset(&regs, 0, sizeof(regs));
    regs.int_no = 31U;
    regs.err_code = 0xABCDU;
    regs.eip = 1234U;
    panic_jump_active = 1U;
    panic_result = (uint32_t)setjmp(panic_jump);
    if (panic_result == 0U) {
        isr_handler(&regs);
        panic_jump_active = 0U;
        return 73;
    }
    panic_jump_active = 0U;
    if (panic_result != 1U) return 74;
    if (idt_get_irq_status(0U, NULL) != ERR_NULL) return 75;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:idt|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:idt|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:idt|value=0x%08X\n", (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_initial_state();
    if (!result) result = check_initialization();
    if (!result) result = check_handler_registration();
    if (!result) result = check_pic_operations();
    if (!result) result = check_irq_dispatch();
    if (!result) result = check_syscall_and_panic();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
