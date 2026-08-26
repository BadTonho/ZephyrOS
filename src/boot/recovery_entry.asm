[BITS 32]

[EXTERN recovery_loader_main]
[EXTERN __recovery_bss_start]
[EXTERN __recovery_bss_end]

section .text
global _start
global recovery_bios_read_sector
global recovery_bios_write_sector
global recovery_bios_wait_key
global recovery_boot_kernel_entry
global recovery_boot_system_entry

BIOS_GATEWAY_OFFSET equ 0x00005F00
BIOS_GATEWAY_DAP equ 0x00002A00
BIOS_GATEWAY_RETURN equ 0x00002A10
BIOS_GATEWAY_SAVED_ESP equ 0x00002A18
BIOS_GATEWAY_BUFFER equ 0x00002A1C
BIOS_GATEWAY_STATUS equ 0x00002A20
BIOS_GATEWAY_OPERATION equ 0x00002A21
BIOS_GATEWAY_KEY_RESULT equ 0x00002A24
BIOS_GATEWAY_TIMEOUT equ 0x00002A28
BIOS_BOUNCE_BUFFER equ 0x00010000
BIOS_GATEWAY_OPERATION_READ equ 0
BIOS_GATEWAY_OPERATION_WRITE equ 1
BIOS_GATEWAY_OPERATION_KEY equ 2
GDT_CODE32_SEL equ 0x08
GDT_DATA32_SEL equ 0x10
GDT_CODE16_SEL equ 0x18
KERNEL_OFFSET equ 0x00100000
SYSTEM_BOOT_OFFSET equ 0x00007C00
KERNEL_STACK_TOP equ 0x0009F000

_start:
    mov ebx, [esp + 4]
    mov edx, [esp + 8]
    cld
    xor eax, eax
    mov edi, __recovery_bss_start
    mov ecx, __recovery_bss_end
    sub ecx, edi
    rep stosb
    push edx
    push ebx
    call recovery_loader_main
    add esp, 8
.halt:
    cli
    hlt
    jmp .halt

recovery_bios_read_sector:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp + 8]
    call recovery_bios_prepare
    mov eax, [ebp + 12]
    mov [BIOS_GATEWAY_BUFFER], eax
    mov byte [BIOS_GATEWAY_OPERATION], BIOS_GATEWAY_OPERATION_READ
    mov dword [BIOS_GATEWAY_RETURN], .resume
    mov word [BIOS_GATEWAY_RETURN + 4], GDT_CODE32_SEL
    mov [BIOS_GATEWAY_SAVED_ESP], esp
    cli
    jmp GDT_CODE16_SEL:BIOS_GATEWAY_OFFSET
.resume:
    mov ax, GDT_DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [BIOS_GATEWAY_SAVED_ESP]
    cld
    cmp byte [BIOS_GATEWAY_STATUS], 0
    jne .fail
    mov esi, BIOS_BOUNCE_BUFFER
    mov edi, [BIOS_GATEWAY_BUFFER]
    mov ecx, 128
    cld
    rep movsd
    mov eax, 1
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

recovery_bios_write_sector:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp + 8]
    call recovery_bios_prepare
    mov esi, [ebp + 12]
    mov edi, BIOS_BOUNCE_BUFFER
    mov ecx, 128
    cld
    rep movsd
    mov byte [BIOS_GATEWAY_OPERATION], BIOS_GATEWAY_OPERATION_WRITE
    mov dword [BIOS_GATEWAY_RETURN], .resume
    mov word [BIOS_GATEWAY_RETURN + 4], GDT_CODE32_SEL
    mov [BIOS_GATEWAY_SAVED_ESP], esp
    cli
    jmp GDT_CODE16_SEL:BIOS_GATEWAY_OFFSET
.resume:
    mov ax, GDT_DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [BIOS_GATEWAY_SAVED_ESP]
    cld
    cmp byte [BIOS_GATEWAY_STATUS], 0
    jne .fail
    mov eax, 1
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

recovery_bios_wait_key:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp + 8]
    mov [BIOS_GATEWAY_TIMEOUT], eax
    mov byte [BIOS_GATEWAY_OPERATION], BIOS_GATEWAY_OPERATION_KEY
    mov dword [BIOS_GATEWAY_RETURN], .resume
    mov word [BIOS_GATEWAY_RETURN + 4], GDT_CODE32_SEL
    mov [BIOS_GATEWAY_SAVED_ESP], esp
    cli
    jmp GDT_CODE16_SEL:BIOS_GATEWAY_OFFSET
.resume:
    mov ax, GDT_DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [BIOS_GATEWAY_SAVED_ESP]
    cld
    cmp byte [BIOS_GATEWAY_STATUS], 0
    jne .fail
    movzx eax, word [BIOS_GATEWAY_KEY_RESULT]
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

recovery_bios_prepare:
    mov byte [BIOS_GATEWAY_DAP], 0x10
    mov byte [BIOS_GATEWAY_DAP + 1], 0
    mov word [BIOS_GATEWAY_DAP + 2], 1
    mov word [BIOS_GATEWAY_DAP + 4], 0
    mov word [BIOS_GATEWAY_DAP + 6], (BIOS_BOUNCE_BUFFER >> 4)
    mov [BIOS_GATEWAY_DAP + 8], eax
    mov dword [BIOS_GATEWAY_DAP + 12], 0
    ret

recovery_boot_kernel_entry:
    mov esi, [esp + 4]
    mov edi, [esp + 8]
    mov esp, KERNEL_STACK_TOP
    cli
    call KERNEL_OFFSET
.halt:
    cli
    hlt
    jmp .halt

recovery_boot_system_entry:
    push ebp
    push ebx
    push esi
    push edi
    cli
    call SYSTEM_BOOT_OFFSET
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
