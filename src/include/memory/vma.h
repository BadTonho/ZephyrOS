#ifndef VMA_H
#define VMA_H

#include "types.h"
#include "core/memory.h"
#include "memory/paging.h"

struct process;
struct vfs_file;

#define VM_READ      0x01U
#define VM_WRITE     0x02U
#define VM_EXEC      0x04U
#define VM_SHARED    0x08U
#define VM_ANONYMOUS 0x10U

#define VMA_USER_MMAP_START (USER_LAUNCH_BASE + PAGE_SIZE)
#define VMA_USER_MMAP_END   USER_STACK_BASE

typedef struct vm_area {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t flags;
    struct vfs_file* file;
    uint32_t offset;
    struct vm_area* next;
} vm_area_t;

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t flags;
    uint32_t offset;
} vm_area_info_t;

int process_vma_register_image(struct process* proc, uint32_t code_size);
int process_vma_mmap(struct process* proc, uint32_t length,
                     uint32_t protection, uint32_t flags,
                     uint32_t* address_out);
int process_vma_munmap(struct process* proc, uint32_t address,
                       uint32_t length);
int process_vma_copy(const struct process* proc, vm_area_info_t* output,
                     uint32_t capacity, uint32_t* out_count);
void process_vma_release(struct process* proc);

#endif
