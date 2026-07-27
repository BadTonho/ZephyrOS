[BITS 16]
[ORG 0x5000]

KERNEL_OFFSET    equ 0x00100000
KERNEL_LIMIT     equ 0x00800000
KERNEL_STACK_TOP equ 0x0009F000
STAGE2_LOAD      equ 0x5000
STAGE2_INFO      equ 0x4FFE
STAGE2_STACK     equ 0x1F00
MEMORY_MAP       equ 0x3000
VESA_INFO        equ 0x2000
VESA_MODE_INFO   equ 0x2400
VESA_MODE_SIZE   equ 256
KERNEL_BUFFER    equ 0x2600
SECTOR_SIZE      equ 512
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

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS ((KERNEL_LIMIT - KERNEL_OFFSET) / SECTOR_SIZE)
%endif

%if KERNEL_SECTORS <= 0
    %error "kernel nao possui setores para carregar"
%endif

%if KERNEL_SECTORS > ((KERNEL_LIMIT - KERNEL_OFFSET) / SECTOR_SIZE)
    %error "kernel excede a reserva de memoria alta"
%endif

%if KERNEL_BUFFER < (VESA_MODE_INFO + VESA_MODE_SIZE) || (KERNEL_BUFFER + SECTOR_SIZE) > MEMORY_MAP
    %error "bounce buffer do kernel sobrepoe buffers do stage2"
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

    call detect_memory
    call detect_geometry

    call validate_kernel_memory
    jc memory_error

    call enable_a20
    jc a20_error

    ; O stage1 gravou a quantidade de setores do proprio stage2.
    mov ax, [STAGE2_INFO]
    inc ax
    mov [LBA], ax
    mov dword [LOAD_DEST], KERNEL_OFFSET
    mov word [remaining], KERNEL_SECTORS
    call load_kernel

    ; Mantem o modo texto durante o carregamento para tornar erros visiveis.
    call set_vesa_mode

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp 0x08:protected_mode

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

validate_kernel_memory:
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
    cmp eax, KERNEL_OFFSET
    ja .advance

    mov ebx, [si + 8]
    mov ecx, [si + 12]
    add ebx, eax
    adc ecx, edx
    test ecx, ecx
    jnz .success
    cmp ebx, KERNEL_LIMIT
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
.read_loop:
    cmp word [remaining], 0
    je .done
    cmp dword [LOAD_DEST], KERNEL_LIMIT - SECTOR_SIZE
    ja load_overflow

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

    xor ax, ax
    mov es, ax
    mov bx, KERNEL_BUFFER
    mov ax, 0x0201
    int 0x13
    jc disk_error

    call copy_sector_high
    inc word [LBA]
    dec word [remaining]
    jmp .read_loop

.done:
    ret

copy_sector_high:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp dword 0x08:copy_protected

[BITS 32]
copy_protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov esi, KERNEL_BUFFER
    mov edi, [LOAD_DEST]
    mov ecx, SECTOR_SIZE / 4
    rep movsd
    add dword [LOAD_DEST], SECTOR_SIZE

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp word 0x0000:copy_real

[BITS 16]
copy_real:
    xor ax, ax
    mov ds, ax
    mov es, ax
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

disk_error:
    xor ax, ax
    mov ds, ax
    mov si, msg_disk
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
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, KERNEL_STACK_TOP
    mov esi, MEMORY_MAP
    mov edi, VESA_INFO
    call KERNEL_OFFSET
    jmp $

gdt_start:
    dd 0x0, 0x0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BOOT_DRIVE: db 0
mmap_count: dd 0
SPT:        dw 0
NUM_HEADS:  dw 0
LBA:        dw 0
LOAD_DEST:  dd 0
remaining:  dw 0
msg_disk:     db "Kernel disk error!", 0
msg_memory:   db "Kernel high memory unavailable!", 0
msg_a20:      db "A20 enable error!", 0
msg_overflow: db "Kernel load overflow!", 0

%if ($-$$) > ((0x10000 - STAGE2_LOAD) - (SECTOR_SIZE - 1))
    %error "stage2 excede o limite de memoria reservado"
%endif

; O kernel comeca no setor seguinte, por isso o stage2 precisa ser alinhado.
times (((($-$$ + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE) - ($-$$)) db 0
