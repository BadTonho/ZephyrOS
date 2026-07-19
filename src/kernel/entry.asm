[BITS 32]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]

section .text
global _start

_start:
    cld
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb
    push esi
    call kernel_main
    add esp, 4
    jmp $
