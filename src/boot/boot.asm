[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET equ 0x10000
MEMORY_MAP    equ 0x8000
VESA_INFO     equ 0x7000
E820_ENTRY_SIZE equ 24

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 272
%endif

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
    ; Mantem as IRQs mascaradas ate o kernel carregar a IDT.
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
    mov word [SPT], bpb_secs_per_track
    mov word [NUM_HEADS], bpb_num_heads
.geom_ok:

    mov word [LBA], 1
    mov word [LOAD_SEG], (KERNEL_OFFSET >> 4)
    mov word [LOAD_OFF], 0x0000
    mov word [remaining], KERNEL_SECTORS

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

    call set_vesa_mode

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
    mov ecx, E820_ENTRY_SIZE
    mov edx, 0x534D4150
    int 0x15
    jc .dn
    cmp eax, 0x534D4150
    jne .dn

    cmp ecx, 20
    jb .dn
    inc dword [mmap_count]
    test ebx, ebx
    jz .dn
    cmp ecx, E820_ENTRY_SIZE
    jae .next_entry
    mov dword [es:di + 20], 0
.next_entry:
    add di, E820_ENTRY_SIZE
    jmp .lp
.dn:
    mov eax, [mmap_count]
    mov [MEMORY_MAP - 4], eax
    ret

set_vesa_mode:
    ; Query mode info for 800x600x32 (VESA mode 0x115)
    push es
    xor ax, ax
    mov es, ax
    mov di, 0x7E00             ; temp buffer for VESA mode info (256 bytes)
    mov ax, 0x4F01
    mov cx, 0x115              ; 800x600x32
    int 0x10
    pop es

    ; Check if mode is supported (bit 0 of ModeAttributes)
    test word [0x7E00], 0x01
    jz .vesa_fail

    ; Check if linear framebuffer is supported (bit 7)
    test word [0x7E00], 0x80
    jz .vesa_fail

    ; Save framebuffer info at VESA_INFO (0x7000)
    ; Offset 0: framebuffer address (32-bit)
    ; Offset 4: pitch (16-bit)
    ; Offset 6: width (16-bit)
    ; Offset 8: height (16-bit)
    ; Offset 10: bpp (8-bit)
    ; Offset 11: initialized flag (8-bit)
    mov eax, [0x7E00 + 40]     ; PhysBasePtr at offset 40
    mov [VESA_INFO], eax
    mov ax, [0x7E00 + 16]      ; BytesPerScanLine (pitch)
    mov [VESA_INFO + 4], ax
    mov ax, [0x7E00 + 18]      ; XResolution
    mov [VESA_INFO + 6], ax
    mov ax, [0x7E00 + 20]      ; YResolution
    mov [VESA_INFO + 8], ax
    mov al, [0x7E00 + 25]      ; BitsPerPixel
    mov [VESA_INFO + 10], al
    mov byte [VESA_INFO + 11], 1 ; initialized = true

    ; Set VESA mode 0x115 with linear framebuffer (bit 14 = 0x4000)
    mov ax, 0x4F02
    mov bx, 0x4115             ; 0x115 | 0x4000
    int 0x10
    ret

.vesa_fail:
    ; VESA not available, mark as not initialized
    mov byte [VESA_INFO + 11], 0
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
SPT: dw 0
NUM_HEADS: dw 0
LBA: dw 0
LOAD_OFF: dw 0
LOAD_SEG: dw 0
remaining: dw 0
msg_err:  db "Disk error!", 0

times 510-($-$$) db 0
dw 0xAA55
