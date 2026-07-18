[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET equ 0x1000
MEMORY_MAP    equ 0x8000

    jmp short start
    nop

bpb_oem:            db "MINI    "
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
bpb_volume_label:   db "MINIOS     "
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

    mov si, msg_boot
    call print_string_16

    mov si, msg_mem
    call print_string_16
    call detect_memory

    mov si, msg_load
    call print_string_16

    mov bx, KERNEL_OFFSET
    mov dh, 30
    mov dl, [BOOT_DRIVE]
    call disk_load

    mov si, msg_done
    call print_string_16

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

detect_memory:
    mov di, MEMORY_MAP
    xor ebx, ebx
    mov dword [mmap_count], 0

.mmap_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15

    jc .mmap_done

    cmp eax, 0x534D4150
    jne .mmap_done

    inc dword [mmap_count]

    cmp ebx, 0
    je .mmap_done

    add di, 24
    jmp .mmap_loop

.mmap_done:
    mov eax, [mmap_count]
    mov [MEMORY_MAP - 4], eax
    ret

print_string_16:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    popa
    ret

disk_load:
    push dx
    mov ah, 0x02
    mov al, dh
    mov ch, 0x00
    mov cl, 0x02
    mov dh, 0x00
    int 0x13
    jc disk_error
    pop dx
    cmp al, dh
    jne sectors_error
    ret

disk_error:
    mov si, msg_disk_err
    call print_string_16
    jmp $

sectors_error:
    mov si, msg_sec_err
    call print_string_16
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
    dd 0x0
    dd 0x0

gdt_code:
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10011010b
    db 11001111b
    db 0x0

gdt_data:
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BOOT_DRIVE: db 0
mmap_count: dd 0

msg_boot:    db "[Boot] Iniciando MiniOS...", 13, 10, 0
msg_mem:     db "[Boot] Detectando memoria...", 13, 10, 0
msg_load:    db "[Boot] Carregando kernel...", 13, 10, 0
msg_done:    db "[Boot] Kernel carregado!", 13, 10, 0
msg_disk_err:db "[ERRO] Falha ao ler disco!", 13, 10, 0
msg_sec_err: db "[ERRO] Setores incorretos!", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
