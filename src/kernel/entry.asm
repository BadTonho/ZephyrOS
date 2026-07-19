[BITS 32]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]

section .text
global _start

_start:
    ; kernel_main(mmap_addr, vesa_info_addr)
    ; Salvar os registradores antes de usar EDI para limpar a .bss.
    ; O cdecl exige que o segundo argumento seja empilhado primeiro.
    push edi
    push esi
    cld
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb
    call kernel_main
    add esp, 8
    jmp $
