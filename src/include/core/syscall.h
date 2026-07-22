#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "drivers/idt.h"

#define SYSCALL_VECTOR 0x80

#define APP_SYSCALL_PROCESS_EXIT  0
#define APP_SYSCALL_CONSOLE_WRITE 1
#define APP_SYSCALL_UPTIME        2
#define APP_SYSCALL_MEMORY_INFO   3
#define APP_SYSCALL_FILE_OPEN     4
#define APP_SYSCALL_FILE_READ     5
#define APP_SYSCALL_FILE_WRITE    6
#define APP_SYSCALL_FILE_CLOSE    7
#define APP_SYSCALL_MESSAGE_SEND  8
#define APP_SYSCALL_MESSAGE_RECEIVE 9
#define APP_SYSCALL_INVALID       0xFFFFFFFFU

int syscall_init(void);
int syscall_is_ready(void);
void syscall_handler(registers_t* regs);
int syscall_invoke_kernel(uint32_t number,
                          uint32_t arg1,
                          uint32_t arg2,
                          uint32_t arg3,
                          uint32_t arg4,
                          uint32_t arg5);

#endif
