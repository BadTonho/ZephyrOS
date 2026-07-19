[BITS 16]
[ORG 0x5000]

KERNEL_OFFSET    equ 0x10000
KERNEL_LIMIT     equ 0x80000
STAGE2_LOAD      equ 0x5000
STAGE2_INFO      equ 0x4FFE
STAGE2_STACK     equ 0x1F00
MEMORY_MAP       equ 0x3000
VESA_INFO        equ 0x2000
VESA_MODE_INFO   equ 0x2400
E820_ENTRY_SIZE  equ 24
MAX_E820_ENTRIES equ ((STAGE2_LOAD - MEMORY_MAP) / E820_ENTRY_SIZE)

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS ((KERNEL_LIMIT - KERNEL_OFFSET) / 512)
%endif

%if KERNEL_SECTORS > ((KERNEL_LIMIT - KERNEL_OFFSET) / 512)
    %error "kernel excede o limite de memoria reservado"
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
    call set_vesa_mode

    ; O stage1 gravou a quantidade de setores do proprio stage2.
    mov ax, [STAGE2_INFO]
    inc ax
    mov [LBA], ax
    mov word [LOAD_SEG], (KERNEL_OFFSET >> 4)
    mov word [LOAD_OFF], 0
    mov word [remaining], KERNEL_SECTORS
    call load_kernel

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
    mov cx, 0x115
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
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov byte [VESA_INFO + 11], 1
    ret

.fail:
    mov byte [VESA_INFO + 11], 0
    ret

load_kernel:
.read_loop:
    cmp word [remaining], 0
    je .done

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

    mov bx, [LOAD_SEG]
    mov es, bx
    mov bx, [LOAD_OFF]
    mov ax, 0x0201
    int 0x13
    jc disk_error

    inc word [LBA]
    add word [LOAD_OFF], 512
    jnc .no_segment_carry
    add word [LOAD_SEG], 0x1000
.no_segment_carry:
    dec word [remaining]
    jmp .read_loop

.done:
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
    mov si, msg_err
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
    mov esp, 0x90000
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
LOAD_OFF:   dw 0
LOAD_SEG:   dw 0
remaining:  dw 0
msg_err:    db "Kernel disk error!", 0

%if ($-$$) > ((0x10000 - STAGE2_LOAD) - 511)
    %error "stage2 excede o limite de memoria reservado"
%endif

; O kernel comeca no setor seguinte, por isso o stage2 precisa ser alinhado.
times (((($-$$ + 511) / 512) * 512) - ($-$$)) db 0
