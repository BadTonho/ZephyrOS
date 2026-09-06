[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov [boot_drive], dl
    mov ax, 0x0800
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc boot_halt
    mov ax, 0x0500
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 3
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc boot_halt
    mov ax, 0x02B0
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 4
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc boot_halt
    mov ax, 0x0600
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 5
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc boot_halt
    xor ax, ax
    mov es, ax
%ifdef FIXTURE_VALID_HANDOFF
    mov dword [0x2B00], 0x4342535A
    mov word [0x2B04], 1
    mov word [0x2B06], 64
    mov dword [0x2B08], 2
    mov dword [0x2B0C], 0x7C00
    mov dword [0x2B10], 512
    mov dword [0x2B14], 0x5000
    mov dword [0x2B18], 0x0F00
    mov dword [0x2B1C], 0x100000
    mov dword [0x2B20], 1
    mov dword [0x2B24], 0x3000
    mov dword [0x2B28], 0x2000
    mov dword [0x2B2C], 0x2800
    mov dword [0x2B30], 0xBC6C57A1
%endif
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

[BITS 32]
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000
    mov esi, 0x00007C00
    mov edi, 0x00090000
    mov ecx, 128
    cld
    rep movsd
    push dword 0x00090000 + (trampoline - $$)
    ret

protected_halt:
    cli
    hlt
    jmp protected_halt

boot_drive: db 0

[BITS 16]
boot_halt:
    cli
    hlt
    jmp boot_halt

[BITS 32]
trampoline:
    mov esi, 0x00008000
    mov edi, 0x00007C00
    mov ecx, 128
    cld
    rep movsd
    mov byte [0x00100000], 0xC3
    push dword 0x00090000 + (trampoline_after - $$)
    push dword 0x00007C00
    ret

trampoline_after:
    cli
    hlt
    jmp trampoline_after

align 8
gdt:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt - 1
    dd gdt

times 510-($-$$) db 0
dw 0xAA55
