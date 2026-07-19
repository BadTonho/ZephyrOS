[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET equ 0x10000
MEMORY_MAP    equ 0x8000

    jmp short start
    nop

bpb_oem:            db "ZEPHYR  "
bpb_bytes_per_sec:  dw 512
bpb_sec_per_clust: db 1
bpb_reserved_secs:  dw 1
bpb_num_fats:       db 2
bpb_root_entries:   dw 224
bpb_total_secs:     dw 2880
bpb_media_type:     db 0xF0
bpb_secs_per_fat:   dw 9
bpb_secs_per_track: dw 18
bpb_num_heads:      dw 2
bpb_hidden_secs:    dd 0
bpb_total_secs_big: dd 0
bpb_drive_num:      db 0
bpb_reserved:       db 0
bpb_signature:      db 0x29
bpb_volume_id:      dd 0x12345678
bpb_volume_label:   db "ZEPHYROS   "
bpb_filesystem:     db "FAT12   "

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [BOOT_DRIVE], dl

    call detect_memory

    ; Query drive geometry INT 13h AH=08h
    mov ah, 0x08
    xor di, di
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .no_geom
    and cl, 0x3F
    mov [SPT], cl
    movzx ax, dh
    inc ax
    mov [NUM_HEADS], ax
    jmp .geom_ok
.no_geom:
    mov word [SPT], 63
    mov word [NUM_HEADS], 16
.geom_ok:

    mov word [LBA], 1
    mov word [LOAD_SEG], (KERNEL_OFFSET >> 4)
    mov word [LOAD_OFF], 0x0000
    mov word [remaining], 272

.read_loop:
    cmp word [remaining], 0
    je .read_done

    ; LBA to CHS
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

    ; ES:BX
    mov bx, [LOAD_SEG]
    mov es, bx
    mov bx, [LOAD_OFF]

    ; Read 1 sector
    mov ax, 0x0201
    int 0x13
    jc disk_error

    inc word [LBA]
    add word [LOAD_OFF], 512
    jnc .no_seg
    add word [LOAD_SEG], 0x1000
.no_seg:
    dec word [remaining]
    jmp .read_loop

.read_done:
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

detect_memory:
    mov di, MEMORY_MAP
    xor ebx, ebx
    mov dword [mmap_count], 0
.lp:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .dn
    cmp eax, 0x534D4150
    jne .dn
    inc dword [mmap_count]
    test ebx, ebx
    jz .dn
    add di, 24
    jmp .lp
.dn:
    mov eax, [mmap_count]
    mov [MEMORY_MAP - 4], eax
    ret

print16:
    pusha
.lp:
    lodsb
    test al, al
    jz .dn
    mov ah, 0x0E
    xor bh, bh
    mov bl, 0x07
    int 0x10
    jmp .lp
.dn:
    popa
    ret

disk_error:
    mov si, msg_err
    call print16
    jmp $

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
SPT: dw 0
NUM_HEADS: dw 0
LBA: dw 0
LOAD_OFF: dw 0
LOAD_SEG: dw 0
remaining: dw 0
msg_err:  db "Disk error!", 0

times 510-($-$$) db 0
dw 0xAA55
