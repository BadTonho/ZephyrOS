[BITS 16]
[ORG 0x7C00]

STAGE2_LOAD  equ 0x5000
STAGE2_INFO  equ 0x4FFE

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 1
%endif

    jmp short start
    nop

; BPB mantido para que o setor continue identificavel como FAT12.
bpb_oem:            db "ZEPHYR  "
bpb_bytes_per_sec:  dw 512
bpb_sec_per_clust:  db 1
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
    mov [BOOT_DRIVE], dl

    call detect_geometry

    mov word [LBA], 1
    mov word [LOAD_SEG], (STAGE2_LOAD >> 4)
    mov word [LOAD_OFF], 0
    mov word [remaining], STAGE2_SECTORS
    call load_sectors

    ; O stage2 le este valor para encontrar o primeiro setor do kernel.
    mov word [STAGE2_INFO], STAGE2_SECTORS
    mov dl, [BOOT_DRIVE]
    jmp 0x0000:STAGE2_LOAD

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
    mov word [SPT], bpb_secs_per_track
    mov word [NUM_HEADS], bpb_num_heads
    ret

load_sectors:
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

BOOT_DRIVE: db 0
SPT:        dw 0
NUM_HEADS:  dw 0
LBA:        dw 0
LOAD_OFF:   dw 0
LOAD_SEG:   dw 0
remaining:  dw 0
msg_err:    db "Stage2 disk error!", 0

times 510-($-$$) db 0
dw 0xAA55
