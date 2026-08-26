[BITS 32]
[ORG 0x7C00]

HANDOFF equ 0x2B00
HANDOFF_SIZE equ 64
HANDOFF_VERSION equ 1
BOOT_ABI_CHAIN equ 2
BOOT_ADDRESS equ 0x7C00
BOOT_SIZE equ 512
STAGE2_ADDRESS equ 0x5000
STAGE2_LIMIT equ 0x0F00
KERNEL_ADDRESS equ 0x100000
KERNEL_LIMIT equ 0x700000
MEMORY_MAP_ADDRESS equ 0x3000
VESA_INFO_ADDRESS equ 0x2000
BOOT_HANDOFF_ADDRESS equ 0x2800
HANDOFF_CHECKSUM_WORDS equ 13

start:
    cli
%ifdef SYSTEM_BOOT_CORRUPT_HANDOFF
    mov dword [HANDOFF], 0
%endif
%ifdef SYSTEM_BOOT_FORCE_RETURN
    ret
%endif
    cmp dword [HANDOFF], 0x4342535A
    jne fail
    cmp word [HANDOFF + 4], HANDOFF_VERSION
    jne fail
    cmp word [HANDOFF + 6], HANDOFF_SIZE
    jne fail
    cmp dword [HANDOFF + 8], BOOT_ABI_CHAIN
    jne fail
    cmp dword [HANDOFF + 12], BOOT_ADDRESS
    jne fail
    cmp dword [HANDOFF + 16], BOOT_SIZE
    jne fail
    cmp dword [HANDOFF + 20], STAGE2_ADDRESS
    jne fail
    cmp dword [HANDOFF + 24], 0
    je fail
    cmp dword [HANDOFF + 24], STAGE2_LIMIT
    ja fail
    cmp dword [HANDOFF + 28], KERNEL_ADDRESS
    jne fail
    cmp dword [HANDOFF + 32], 0
    je fail
    cmp dword [HANDOFF + 32], KERNEL_LIMIT
    ja fail
    cmp dword [HANDOFF + 36], MEMORY_MAP_ADDRESS
    jne fail
    cmp dword [HANDOFF + 40], VESA_INFO_ADDRESS
    jne fail
    cmp dword [HANDOFF + 44], BOOT_HANDOFF_ADDRESS
    jne fail
    cmp dword [HANDOFF + 52], 0
    jne fail
    cmp dword [HANDOFF + 56], 0
    jne fail
    cmp dword [HANDOFF + 60], 0
    jne fail
    xor eax, eax
    mov esi, HANDOFF
    mov ecx, HANDOFF_CHECKSUM_WORDS
.checksum:
    add eax, [esi]
    add esi, 4
    loop .checksum
    cmp eax, 0xFFFFFFFF
    jne fail
    jmp STAGE2_ADDRESS

fail:
    ret

times 512-($-$$) db 0
