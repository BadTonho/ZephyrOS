[BITS 32]

[EXTERN recovery_loader_main]
[EXTERN __recovery_bss_start]
[EXTERN __recovery_bss_end]

section .text
global _start

_start:
    cld
    xor eax, eax
    mov edi, __recovery_bss_start
    mov ecx, __recovery_bss_end
    sub ecx, edi
    rep stosb
    call recovery_loader_main
.halt:
    cli
    hlt
    jmp .halt
