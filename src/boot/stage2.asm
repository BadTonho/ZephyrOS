[BITS 16]
[ORG 0x5000]

KERNEL_OFFSET    equ 0x00100000
KERNEL_LIMIT     equ 0x00800000
RECOVERY_LOADER_OFFSET equ 0x00900000
RECOVERY_LOADER_LIMIT equ 0x00A00000
LEGACY_KERNEL_LBA equ 64
RECOVERY_LOADER_LBA equ 3000
FAT32_START_LBA equ 4096
KERNEL_STACK_TOP equ 0x0009F000
STAGE2_LOAD      equ 0x5000
STAGE2_INFO      equ 0x4FFE
STAGE2_STACK     equ 0x1F00
MEMORY_MAP       equ 0x3000
VESA_INFO        equ 0x2000
VESA_MODE_INFO   equ 0x2400
VESA_MODE_SIZE   equ 256
SYSTEM_BOOT_HANDOFF equ 0x2800
SYSTEM_BOOT_HANDOFF_SIZE equ 64
BIOS_GATEWAY_OFFSET equ 0x5F00
BIOS_GATEWAY_DAP equ 0x2A00
BIOS_GATEWAY_RETURN equ 0x2A10
BIOS_GATEWAY_SAVED_ESP equ 0x2A18
BIOS_GATEWAY_STATUS equ 0x2A20
BIOS_GATEWAY_OPERATION equ 0x2A21
BIOS_GATEWAY_ATTEMPTS equ 0x2A22
BIOS_GATEWAY_CHS_SECTOR equ 0x2A23
SECTOR_SIZE      equ 512
KERNEL_BUFFER    equ 0x00010000
KERNEL_BUFFER_SEG equ (KERNEL_BUFFER >> 4)
KERNEL_BUFFER_SECTORS equ 63
KERNEL_BUFFER_END equ (KERNEL_BUFFER + KERNEL_BUFFER_SECTORS * SECTOR_SIZE)
LOW_BUFFER_LIMIT equ 0x00080000
DISK_MODE_CHS    equ 0
DISK_MODE_LBA    equ 1
DISK_READ_ATTEMPTS equ 3
EDD_PACKET_SIZE  equ 0x10
EDD_SIGNATURE_IN equ 0x55AA
EDD_SIGNATURE_OUT equ 0xAA55
EDD_FIXED_DISK_ACCESS equ 0x0001
KBC_TIMEOUT      equ 0xFFFF
KBC_DATA_PORT    equ 0x60
KBC_STATUS_PORT  equ 0x64
FAST_A20_PORT    equ 0x92
KBC_DISABLE      equ 0xAD
KBC_ENABLE       equ 0xAE
KBC_READ_OUTPUT  equ 0xD0
KBC_WRITE_OUTPUT equ 0xD1
A20_ENABLE_BIT   equ 0x02
E820_ENTRY_SIZE  equ 24
MAX_E820_ENTRIES equ ((STAGE2_LOAD - MEMORY_MAP) / E820_ENTRY_SIZE)
GDT_CODE32_SEL   equ 0x08
GDT_DATA32_SEL   equ 0x10
GDT_CODE16_SEL   equ 0x18
GDT_DATA16_SEL   equ 0x20

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS ((KERNEL_LIMIT - KERNEL_OFFSET) / SECTOR_SIZE)
%endif

%ifndef RECOVERY_LOADER_SECTORS
%define RECOVERY_LOADER_SECTORS 1
%endif

%ifndef KERNEL_BYTES
%define KERNEL_BYTES (KERNEL_SECTORS * SECTOR_SIZE)
%endif

%if KERNEL_SECTORS <= 0 || RECOVERY_LOADER_SECTORS <= 0
    %error "kernel nao possui setores para carregar"
%endif

%if KERNEL_SECTORS > ((KERNEL_LIMIT - KERNEL_OFFSET) / SECTOR_SIZE)
    %error "kernel legado excede a janela reservada"
%endif

%if KERNEL_BYTES <= 0 || KERNEL_BYTES > (KERNEL_SECTORS * SECTOR_SIZE)
    %error "tamanho do kernel legado invalido"
%endif

%if RECOVERY_LOADER_SECTORS > ((RECOVERY_LOADER_LIMIT - RECOVERY_LOADER_OFFSET) / SECTOR_SIZE) || RECOVERY_LOADER_SECTORS > (FAT32_START_LBA - RECOVERY_LOADER_LBA)
    %error "recovery loader excede a janela reservada"
%endif

%if (KERNEL_BUFFER & 0x0F) != 0
    %error "bounce buffer do kernel deve estar alinhado a segmento"
%endif

%if (KERNEL_BUFFER_END > LOW_BUFFER_LIMIT) || ((KERNEL_BUFFER & 0xFFFF) + (KERNEL_BUFFER_SECTORS * SECTOR_SIZE)) > 0x10000
    %error "bounce buffer do kernel excede a janela BIOS segura"
%endif

stage2_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STAGE2_STACK
    mov [BOOT_DRIVE], dl
    mov byte [VESA_INFO + 11], 0
    mov di, SYSTEM_BOOT_HANDOFF
    mov cx, SYSTEM_BOOT_HANDOFF_SIZE
    xor al, al
    rep stosb

    call detect_memory
    call detect_disk_access

    mov dword [LOAD_DEST], KERNEL_OFFSET
    mov dword [LOAD_LIMIT], KERNEL_LIMIT
    call validate_load_memory
    jc memory_error

    mov dword [LOAD_DEST], RECOVERY_LOADER_OFFSET
    mov dword [LOAD_LIMIT], RECOVERY_LOADER_LIMIT
    call validate_load_memory
    jc memory_error

    call enable_a20
    jc a20_error

    mov word [LBA], RECOVERY_LOADER_LBA
    mov word [remaining], RECOVERY_LOADER_SECTORS
    call load_kernel

    call set_vesa_mode

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp GDT_CODE32_SEL:protected_mode

detect_memory:
    mov di, MEMORY_MAP
    xor ebx, ebx
    mov dword [mmap_count], 0

.loop:
    cmp dword [mmap_count], MAX_E820_ENTRIES
    jae .done

    mov eax, 0xE820
    mov ecx, E820_ENTRY_SIZE
    mov edx, 0x534D4150
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .done

    inc dword [mmap_count]
    cmp ecx, E820_ENTRY_SIZE
    jae .entry_size_ok
    mov dword [es:di + 20], 0
.entry_size_ok:
    add di, E820_ENTRY_SIZE
    test ebx, ebx
    jnz .loop

.done:
    mov eax, [mmap_count]
    mov [MEMORY_MAP - 4], eax
    ret

validate_load_memory:
    pusha
    mov si, MEMORY_MAP
    mov bp, [mmap_count]

.next:
    test bp, bp
    jz .fail
    cmp dword [si + 16], 1
    jne .advance

    mov eax, [si]
    mov edx, [si + 4]
    test edx, edx
    jnz .advance
    cmp eax, [LOAD_DEST]
    ja .advance

    mov ebx, [si + 8]
    mov ecx, [si + 12]
    add ebx, eax
    adc ecx, edx
    test ecx, ecx
    jnz .success
    cmp ebx, [LOAD_LIMIT]
    jae .success

.advance:
    add si, E820_ENTRY_SIZE
    dec bp
    jmp .next

.success:
    clc
    popa
    ret

.fail:
    stc
    popa
    ret

detect_disk_access:
    mov byte [DISK_MODE], DISK_MODE_CHS
    mov bx, EDD_SIGNATURE_IN
    mov ah, 0x41
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .chs
    cmp bx, EDD_SIGNATURE_OUT
    jne .chs
    test cx, EDD_FIXED_DISK_ACCESS
    jz .chs

    mov byte [DISK_MODE], DISK_MODE_LBA
    ret

.chs:
    call detect_geometry
    ret

detect_geometry:
    mov ah, 0x08
    xor di, di
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .fallback

    and cl, 0x3F
    mov [SPT], cl
    xor ax, ax
    mov al, dh
    inc ax
    mov [NUM_HEADS], ax
    ret

.fallback:
    mov word [SPT], 18
    mov word [NUM_HEADS], 2
    ret

set_vesa_mode:
    push es
    xor ax, ax
    mov es, ax
    mov di, VESA_MODE_INFO
    mov ax, 0x4F01
    mov cx, 0x118
    int 0x10
    pop es

    cmp ax, 0x004F
    jne .fail
    test word [VESA_MODE_INFO], 0x0001
    jz .fail
    test word [VESA_MODE_INFO], 0x0080
    jz .fail

    mov eax, [VESA_MODE_INFO + 40]
    mov [VESA_INFO], eax
    mov ax, [VESA_MODE_INFO + 16]
    mov [VESA_INFO + 4], ax
    mov ax, [VESA_MODE_INFO + 18]
    mov [VESA_INFO + 6], ax
    mov ax, [VESA_MODE_INFO + 20]
    mov [VESA_INFO + 8], ax
    mov al, [VESA_MODE_INFO + 25]
    mov [VESA_INFO + 10], al

    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov byte [VESA_INFO + 11], 1
    ret

.fail:
    mov byte [VESA_INFO + 11], 0
    ret

check_a20:
    pushf
    cli
    push ds
    push es
    push si
    push di

    xor ax, ax
    mov ds, ax
    mov si, 0x0500
    mov ax, 0xFFFF
    mov es, ax
    mov di, 0x0510

    mov al, [ds:si]
    mov ah, [es:di]
    push ax
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    cmp byte [ds:si], 0xFF
    pop ax
    mov [es:di], ah
    mov [ds:si], al

    mov ax, 0
    je .done
    inc ax

.done:
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

a20_wait_input:
    mov cx, KBC_TIMEOUT
.loop:
    in al, KBC_STATUS_PORT
    test al, 0x02
    jz .ready
    loop .loop
    stc
    ret
.ready:
    clc
    ret

a20_wait_output:
    mov cx, KBC_TIMEOUT
.loop:
    in al, KBC_STATUS_PORT
    test al, 0x01
    jnz .ready
    loop .loop
    stc
    ret
.ready:
    clc
    ret

a20_enable_kbc:
    pusha
    call a20_wait_input
    jc .fail
    mov al, KBC_DISABLE
    out KBC_STATUS_PORT, al

    call a20_wait_input
    jc .reenable
    mov al, KBC_READ_OUTPUT
    out KBC_STATUS_PORT, al
    call a20_wait_output
    jc .reenable
    in al, KBC_DATA_PORT
    mov bl, al
    or bl, A20_ENABLE_BIT

    call a20_wait_input
    jc .reenable
    mov al, KBC_WRITE_OUTPUT
    out KBC_STATUS_PORT, al
    call a20_wait_input
    jc .reenable
    mov al, bl
    out KBC_DATA_PORT, al

.reenable:
    call a20_wait_input
    jc .fail
    mov al, KBC_ENABLE
    out KBC_STATUS_PORT, al
    call a20_wait_input
    jc .fail
    clc
    popa
    ret

.fail:
    stc
    popa
    ret

enable_a20:
    call check_a20
    test ax, ax
    jnz .success

    mov ax, 0x2401
    int 0x15
    call check_a20
    test ax, ax
    jnz .success

    in al, FAST_A20_PORT
    and al, 0xFE
    or al, A20_ENABLE_BIT
    out FAST_A20_PORT, al
    call check_a20
    test ax, ax
    jnz .success

    call a20_enable_kbc
    jc .fail
    call check_a20
    test ax, ax
    jz .fail

.success:
    xor ax, ax
    mov ds, ax
    mov es, ax
    clc
    ret

.fail:
    stc
    ret

load_kernel:
    movzx eax, word [remaining]
    shl eax, 9
    add eax, [LOAD_DEST]
    jc load_overflow
    cmp eax, [LOAD_LIMIT]
    ja load_overflow

.read_loop:
    cmp word [remaining], 0
    je .done

    cmp byte [DISK_MODE], DISK_MODE_LBA
    jne .prepare_chs

    ; EDD nao depende de trilhas; o bounce buffer continua sendo o limite.
    mov cx, KERNEL_BUFFER_SECTORS
    cmp cx, [remaining]
    jbe .batch_ready
    mov cx, [remaining]
    jmp .batch_ready

.prepare_chs:
    ; Limita o lote ao fim da trilha, ao buffer e aos setores restantes.
    mov ax, [LBA]
    xor dx, dx
    div word [SPT]
    mov cx, [SPT]
    sub cx, dx
    cmp cx, KERNEL_BUFFER_SECTORS
    jbe .buffer_limited
    mov cx, KERNEL_BUFFER_SECTORS
.buffer_limited:
    cmp cx, [remaining]
    jbe .batch_ready
    mov cx, [remaining]
.batch_ready:
    mov [transfer_sectors], cx

    cmp byte [DISK_MODE], DISK_MODE_LBA
    jne .read_chs
    call read_kernel_lba
    jc lba_disk_error
    jmp .read_complete

.read_chs:
    call read_kernel_chs
    jc chs_disk_error

.read_complete:
    call copy_sector_high
    mov ax, [transfer_sectors]
    add [LBA], ax
    sub [remaining], ax
    jmp .read_loop

.done:
    ret

read_kernel_lba:
    mov byte [read_attempts], DISK_READ_ATTEMPTS

.retry:
    mov ax, [transfer_sectors]
    mov [disk_address_packet + 2], ax
    mov word [disk_address_packet + 4], 0
    mov word [disk_address_packet + 6], KERNEL_BUFFER_SEG
    mov ax, [LBA]
    mov [disk_address_packet + 8], ax
    mov word [disk_address_packet + 10], 0
    mov dword [disk_address_packet + 12], 0

    mov si, disk_address_packet
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jnc .success

    dec byte [read_attempts]
    jz .fail
    call reset_boot_disk
    jmp .retry

.success:
    clc
    ret

.fail:
    stc
    ret

read_kernel_chs:
    mov byte [read_attempts], DISK_READ_ATTEMPTS

.retry:
    ; Converte LBA para CHS usando a geometria informada pelo BIOS.
    mov ax, [LBA]
    xor dx, dx
    div word [SPT]
    inc dx
    mov bl, dl
    xor dx, dx
    div word [NUM_HEADS]
    mov ch, al
    mov dh, dl
    mov dl, [BOOT_DRIVE]
    mov cl, ah
    shl cl, 6
    or cl, bl

    mov ax, KERNEL_BUFFER_SEG
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, [transfer_sectors]
    int 0x13
    jnc .success

    dec byte [read_attempts]
    jz .fail
    call reset_boot_disk
    jmp .retry

.success:
    clc
    ret

.fail:
    stc
    ret

reset_boot_disk:
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    ret

copy_sector_high:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp dword GDT_CODE32_SEL:copy_protected

[BITS 32]
copy_protected:
    mov ax, GDT_DATA32_SEL
    mov ds, ax
    mov es, ax
    cld
    mov esi, KERNEL_BUFFER
    mov edi, [LOAD_DEST]
    movzx ecx, word [transfer_sectors]
    shl ecx, 7
    rep movsd
    movzx eax, word [transfer_sectors]
    shl eax, 9
    add [LOAD_DEST], eax

    ; A saida passa por um descritor de codigo 16-bit. Assim, a instrucao
    ; seguinte a limpeza de PE tem tamanho de operando inequivocamente real.
    jmp GDT_CODE16_SEL:copy_protected_16

[BITS 16]
copy_protected_16:
    mov ax, GDT_DATA16_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:copy_real

copy_real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

print16:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    popa
    ret

lba_disk_error:
    xor ax, ax
    mov ds, ax
    mov si, msg_lba_disk
    jmp fatal_error

chs_disk_error:
    xor ax, ax
    mov ds, ax
    mov si, msg_chs_disk
    jmp fatal_error

memory_error:
    xor ax, ax
    mov ds, ax
    mov si, msg_memory
    jmp fatal_error

a20_error:
    xor ax, ax
    mov ds, ax
    mov si, msg_a20
    jmp fatal_error

load_overflow:
    xor ax, ax
    mov ds, ax
    mov si, msg_overflow

fatal_error:
    call print16
    cli
.halt:
    hlt
    jmp .halt

[BITS 32]
protected_mode:
    mov ax, GDT_DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, KERNEL_STACK_TOP
    push dword VESA_INFO
    push dword MEMORY_MAP
    call RECOVERY_LOADER_OFFSET
    jmp $

gdt_start:
    dd 0x0, 0x0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_code_16:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00
gdt_data_16:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BOOT_DRIVE: db 0
DISK_MODE:  db DISK_MODE_CHS
read_attempts: db 0
mmap_count: dd 0
SPT:        dw 0
NUM_HEADS:  dw 0
LBA:        dw 0
LOAD_DEST:  dd 0
LOAD_LIMIT: dd KERNEL_LIMIT
remaining:  dw 0
transfer_sectors: dw 0
align 4
disk_address_packet:
    db EDD_PACKET_SIZE, 0
    dw 0
    dw 0
    dw KERNEL_BUFFER_SEG
    dq 0
msg_lba_disk: db "Kernel LBA read error!", 0
msg_chs_disk: db "Kernel CHS read error!", 0
msg_memory:   db "Kernel high memory unavailable!", 0
msg_a20:      db "A20 enable error!", 0
msg_overflow: db "Kernel load overflow!", 0

%if ($-$$) > (BIOS_GATEWAY_OFFSET - STAGE2_LOAD)
    %error "stage2 invade o endereco fixo do gateway BIOS"
%endif
times ((BIOS_GATEWAY_OFFSET - STAGE2_LOAD) - ($-$$)) db 0

[BITS 16]
bios_gateway_16:
    mov ax, GDT_DATA16_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:bios_gateway_real

bios_gateway_real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, STAGE2_STACK
    sti
    mov byte [BIOS_GATEWAY_STATUS], 1
    mov byte [BIOS_GATEWAY_ATTEMPTS], DISK_READ_ATTEMPTS

.retry:
    cmp byte [DISK_MODE], DISK_MODE_LBA
    jne .chs
    mov word [BIOS_GATEWAY_DAP + 2], 1
    mov si, BIOS_GATEWAY_DAP
    mov dl, [BOOT_DRIVE]
    cmp byte [BIOS_GATEWAY_OPERATION], 1
    je .write
    mov ah, 0x42
    int 0x13
    jmp .result

.write:
    mov ax, 0x4300
    int 0x13
    jmp .result

.chs:
    cmp dword [BIOS_GATEWAY_DAP + 12], 0
    jne .return_protected
    mov eax, [BIOS_GATEWAY_DAP + 8]
    xor edx, edx
    movzx ecx, word [SPT]
    test ecx, ecx
    jz .return_protected
    div ecx
    inc dl
    mov [BIOS_GATEWAY_CHS_SECTOR], dl
    xor edx, edx
    movzx ecx, word [NUM_HEADS]
    test ecx, ecx
    jz .return_protected
    div ecx
    cmp eax, 1023
    ja .return_protected
    mov ch, al
    mov cl, [BIOS_GATEWAY_CHS_SECTOR]
    mov bl, ah
    shl bl, 6
    or cl, bl
    mov dh, dl
    mov dl, [BOOT_DRIVE]
    mov ax, KERNEL_BUFFER_SEG
    mov es, ax
    xor bx, bx
    cmp byte [BIOS_GATEWAY_OPERATION], 1
    je .write_chs
    mov ax, 0x0201
    int 0x13
    jmp .result

.write_chs:
    mov ax, 0x0301
    int 0x13
    jmp .result

.result:
    jnc .success
    dec byte [BIOS_GATEWAY_ATTEMPTS]
    jz .return_protected
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    jmp .retry

.success:
    mov byte [BIOS_GATEWAY_STATUS], 0

.return_protected:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp dword GDT_CODE32_SEL:bios_gateway_protected_return

[BITS 32]
bios_gateway_protected_return:
    mov ax, GDT_DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [BIOS_GATEWAY_SAVED_ESP]
    jmp dword [BIOS_GATEWAY_RETURN]

%if ($-$$) > ((0x10000 - STAGE2_LOAD) - (SECTOR_SIZE - 1))
    %error "stage2 excede o limite de memoria reservado"
%endif

; O kernel comeca no setor seguinte, por isso o stage2 precisa ser alinhado.
times (((($-$$ + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE) - ($-$$)) db 0
