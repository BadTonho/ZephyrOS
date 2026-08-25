[BITS 32]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]

section .text
global _start

_start:
    ; Breadcrumbs pre-kernel ficam na quinta linha VGA e sobrevivem a um
    ; reset por excecao antes de video_init limpar a tela.
    mov word [0x000B8280], 0x0F45
    ; kernel_main(mmap_addr, vesa_info_addr)
    ; Salvar os registradores antes de usar EDI para limpar a .bss.
    ; O cdecl exige que o segundo argumento seja empilhado primeiro.
    push edi
    push esi
    mov word [0x000B8282], 0x0F43
    cld
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb
    mov word [0x000B8284], 0x0F42
    mov word [0x000B8286], 0x0F4D
    call kernel_main
    add esp, 8
    jmp $
