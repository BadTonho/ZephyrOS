[BITS 32]
[EXTERN irq_handler]

%macro IRQ 1
[GLOBAL irq%1]
irq%1:
    cli
    push dword 0
    push dword %1 + 32
    jmp irq_common_stub
%endmacro

IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

irq_common_stub:
    pusha
    ; Mantem a pilha alinhada com registers_t usado pelo C.
    xor eax, eax
    mov ax, gs
    push eax
    xor eax, eax
    mov ax, fs
    push eax
    xor eax, eax
    mov ax, es
    push eax
    xor eax, eax
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call irq_handler
    add esp, 4

    pop eax
    mov ds, ax
    pop eax
    mov es, ax
    pop eax
    mov fs, ax
    pop eax
    mov gs, ax
    popa
    add esp, 8
    sti
    iret
