[BITS 32]
[EXTERN kernel_main]

section .text
global _start

_start:
    push esi
    call kernel_main
    add esp, 4
    jmp $
