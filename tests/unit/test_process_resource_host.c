#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "memory/paging.h"
#include "memory/vma.h"
#include "process/process.h"
#include "process/resource.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static page_directory_t fake_directory;
static uint32_t fake_resident_pages;

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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:process:resources|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:process:resources|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:process:resources|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

void panic(const char* message) {
    (void)message;
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    memset(destination, value, size);
}

int paging_get_user_page_count(page_directory_t* directory,
                               uint32_t* out_count) {
    if (!directory || !out_count) return ERR_NULL;
    if (directory != &fake_directory) return ERR_STATE;
    *out_count = fake_resident_pages;
    return OK;
}

int vfs_get_process_resource_usage(uint32_t pid, uint32_t* descriptors,
                                   uint32_t* pipes) {
    (void)pid;
    if (!descriptors || !pipes) return ERR_NULL;
    *descriptors = 3U;
    *pipes = 0U;
    return OK;
}

uint32_t process_get_child_count(uint32_t parent_pid) {
    (void)parent_pid;
    return 0U;
}

int ipc_get_pending_count_for_pid(uint32_t pid, uint32_t* pending) {
    (void)pid;
    if (!pending) return ERR_NULL;
    *pending = 0U;
    return OK;
}

static int expect_true(int condition, const char* expression) {
    if (condition) return OK;
    fprintf(stderr, "process-resource-host: falhou: %s\n", expression);
    return ERR_STATE;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression) != OK) failures++; \
    } while (0)

int main(void) {
    process_t process;
    vm_area_t areas[PROCESS_RESOURCE_MAX_DYNAMIC_VMAS];
    process_resource_snapshot_t snapshot;
    process_resource_validation_t validation;
    uint32_t failures = 0U;
    int result;

    kmemset(&process, 0, sizeof(process));
    kmemset(&fake_directory, 0, sizeof(fake_directory));
    kmemset(areas, 0, sizeof(areas));
    process.pid = 7U;
    process.event_generation = 11U;
    process.context.user_mode = 1U;
    process.page_directory = &fake_directory;
    process.kernel_stack_size = PROCESS_USER_KERNEL_STACK_SIZE;
    process.user_code_size = PAGE_SIZE;
    process.user_data_size = PAGE_SIZE;
    process.user_launch.argc = 2U;
    process.user_launch.raw_length = 5U;
    fake_resident_pages = 0U;
    coverage_active = 1U;

    result = process_resource_init();
    EXPECT(result == OK);
    result = process_resource_attach(&process);
    EXPECT(result == OK);
    result = process_resource_snapshot_copy(process.pid,
                                            process.event_generation,
                                            &snapshot);
    EXPECT(result == OK);
    EXPECT(snapshot.limits_active == 1U);
    EXPECT(snapshot.resident_pages == 0U);
    EXPECT(snapshot.resident_limit_pages ==
           PROCESS_RESOURCE_MAX_RESIDENT_PAGES);
    EXPECT(snapshot.argument_count == 2U);
    EXPECT(snapshot.argument_bytes == 5U);
    EXPECT(snapshot.image_bytes == 2U * PAGE_SIZE);
    EXPECT(snapshot.descriptors == 3U);
    EXPECT(snapshot.descriptor_limit == PROCESS_RESOURCE_MAX_DESCRIPTORS);
    EXPECT(snapshot.children == 0U);
    EXPECT(snapshot.child_limit == PROCESS_RESOURCE_MAX_CHILDREN);
    EXPECT(snapshot.ipc_pending == 0U);
    EXPECT(snapshot.ipc_pending_limit == PROCESS_RESOURCE_MAX_IPC_PENDING);
    EXPECT(snapshot.pipes == 0U);
    EXPECT(snapshot.pipe_limit == PROCESS_RESOURCE_MAX_PIPES);
    EXPECT(process_resource_check_descriptors(
               &process, PROCESS_RESOURCE_MAX_DESCRIPTORS - 3U) == OK);
    EXPECT(process_resource_check_descriptors(
               &process, PROCESS_RESOURCE_MAX_DESCRIPTORS) == ERR_OVERFLOW);
    EXPECT(snapshot.last_failure == PROCESS_RESOURCE_FAILURE_NONE);
    EXPECT(process_resource_check_children(
               &process, PROCESS_RESOURCE_MAX_CHILDREN) == OK);
    EXPECT(process_resource_check_children(
               &process, PROCESS_RESOURCE_MAX_CHILDREN + 1U) == ERR_OVERFLOW);
    EXPECT(process_resource_check_pipes(&process,
                                       PROCESS_RESOURCE_MAX_PIPES) == OK);
    EXPECT(process_resource_check_pipes(
               &process, PROCESS_RESOURCE_MAX_PIPES + 1U) == ERR_OVERFLOW);
    EXPECT(process_resource_check_ipc_pending(
               &process, PROCESS_RESOURCE_MAX_IPC_PENDING - 1U, 1U) == OK);
    EXPECT(process_resource_check_ipc_pending(
               &process, PROCESS_RESOURCE_MAX_IPC_PENDING, 1U) == ERR_OVERFLOW);
    EXPECT(process_resource_snapshot_copy(process.pid,
                                          process.event_generation,
                                          &snapshot) == OK);
    EXPECT(snapshot.last_failure == PROCESS_RESOURCE_FAILURE_IPC_PENDING);
    result = process_resource_check_vma_split(&process);
    EXPECT(result == OK);

    result = process_resource_check_vma(
        &process, PROCESS_RESOURCE_MAX_ANONYMOUS_BYTES + PAGE_SIZE);
    EXPECT(result == ERR_OVERFLOW);
    result = process_resource_snapshot_copy(process.pid,
                                            process.event_generation,
                                            &snapshot);
    EXPECT(result == OK);
    EXPECT(snapshot.anonymous_bytes == 0U);
    EXPECT(snapshot.dynamic_vmas == 0U);
    EXPECT(snapshot.last_failure ==
           PROCESS_RESOURCE_FAILURE_ANONYMOUS_BYTES);
    EXPECT(snapshot.last_error == ERR_OVERFLOW);

    for (uint32_t index = 0U; index < PROCESS_RESOURCE_MAX_DYNAMIC_VMAS;
         index++) {
        result = process_resource_check_vma(&process, PAGE_SIZE);
        EXPECT(result == OK);
        areas[index].start_addr = VMA_USER_MMAP_START + index * PAGE_SIZE;
        areas[index].end_addr = areas[index].start_addr + PAGE_SIZE;
        areas[index].flags = VM_READ | VM_WRITE | VM_ANONYMOUS;
        if (index) {
            areas[index - 1U].next = &areas[index];
        } else {
            process.vma_list = &areas[index];
        }
        process.vma_count++;
        process_resource_note_vma_success(&process);
    }
    result = process_resource_check_vma(&process, PAGE_SIZE);
    EXPECT(result == ERR_OVERFLOW);
    result = process_resource_check_vma_split(&process);
    EXPECT(result == ERR_OVERFLOW);
    result = process_resource_snapshot_copy(process.pid,
                                            process.event_generation,
                                            &snapshot);
    EXPECT(result == OK);
    EXPECT(snapshot.anonymous_bytes ==
           PROCESS_RESOURCE_MAX_DYNAMIC_VMAS * PAGE_SIZE);
    EXPECT(snapshot.dynamic_vmas == PROCESS_RESOURCE_MAX_DYNAMIC_VMAS);
    EXPECT(snapshot.anonymous_peak_bytes == snapshot.anonymous_bytes);
    EXPECT(snapshot.last_failure ==
           PROCESS_RESOURCE_FAILURE_DYNAMIC_VMAS);

    fake_resident_pages = PROCESS_RESOURCE_MAX_RESIDENT_PAGES - 1U;
    result = process_resource_check_page(&process);
    EXPECT(result == OK);
    process_resource_note_page_success(&process);
    fake_resident_pages = PROCESS_RESOURCE_MAX_RESIDENT_PAGES;
    result = process_resource_check_page(&process);
    EXPECT(result == ERR_OVERFLOW);
    result = process_resource_snapshot_copy(process.pid,
                                            process.event_generation,
                                            &snapshot);
    EXPECT(result == OK);
    EXPECT(snapshot.resident_pages == PROCESS_RESOURCE_MAX_RESIDENT_PAGES);
    EXPECT(snapshot.resident_peak_pages ==
           PROCESS_RESOURCE_MAX_RESIDENT_PAGES - 1U);
    EXPECT(snapshot.last_failure == PROCESS_RESOURCE_FAILURE_RESIDENT_PAGES);
    EXPECT(snapshot.allocation_failures == 8U);

    result = process_resource_snapshot_copy(process.pid,
                                            process.event_generation + 1U,
                                            &snapshot);
    EXPECT(result == ERR_AGAIN);
    result = process_resource_validate_all(&validation);
    EXPECT(result == OK);
    EXPECT(validation.checked == 1U);
    EXPECT(validation.valid == 1U);
    EXPECT(validation.invalid == 0U);

    process_resource_detach(&process);
    result = process_resource_snapshot_copy(process.pid,
                                            process.event_generation,
                                            &snapshot);
    EXPECT(result == ERR_NOT_FOUND);

    coverage_active = 0U;
    coverage_emit(failures ? ERR_STATE : OK);
    if (failures) {
        printf("process-resource-host: FAIL (%u)\n", failures);
        return 1;
    }
    printf("process-resource-host: PASS\n");
    return 0;
}
