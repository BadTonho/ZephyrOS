#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "recovery_menu.h"

int recovery_loader_host_test_contracts(void);

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY)
        coverage_addresses[coverage_count++] = address;
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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:boot:recovery-loader|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:boot:recovery-loader|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:boot:recovery-loader|value=0x%08X\n",
           (uint32_t)result);
}

int recovery_bios_read_sector(uint32_t lba, void* output) {
    uint8_t* bytes = (uint8_t*)output;
    (void)lba;
    if (!bytes) return 0;
    for (uint32_t index = 0U; index < 512U; index++) bytes[index] = 0U;
    return 1;
}

int recovery_bios_write_sector(uint32_t lba, const void* input) {
    (void)lba;
    return input != 0;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void recovery_boot_kernel_entry(uint32_t mmap, uint32_t vesa) {
    (void)mmap;
    (void)vesa;
}

void recovery_boot_system_entry(void) {}

void recovery_console_init(uint8_t* vesa) {
    (void)vesa;
}

void recovery_console_clear(void) {}

void recovery_console_print(const char* message) {
    (void)message;
}

void recovery_console_print_u32(uint32_t value) {
    (void)value;
}

int recovery_menu_wait_f8(void) {
    return 0;
}

recovery_menu_action_t recovery_menu_run(const recovery_menu_view_t* view) {
    (void)view;
    return RECOVERY_MENU_ACTION_LEGACY;
}

int recovery_menu_confirm_retry(const recovery_menu_view_t* view) {
    (void)view;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = recovery_loader_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) {
        fprintf(stderr, "recovery-loader-host: FAIL %d\n", result);
        return result;
    }
    printf("recovery-loader-host: PASS\n");
    return 0;
}
