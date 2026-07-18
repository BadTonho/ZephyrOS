[BITS 32]
[EXTERN kernel_main]

section .text
global _start

_start:
    call kernel_main
    jmp $
