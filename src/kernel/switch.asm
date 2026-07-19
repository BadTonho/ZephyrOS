[BITS 32]
[GLOBAL context_switch]
[GLOBAL tss_flush]

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

context_switch:
    push ebx
    push esi
    push edi
    push ebp

    mov eax, [esp + 20]
    mov [eax + 0],  eax
    mov [eax + 4],  ebx
    mov [eax + 8],  ecx
    mov [eax + 12], edx
    mov [eax + 16], esi
    mov [eax + 20], edi
    mov [eax + 24], ebp
    mov [eax + 28], esp

    push eax
    pushfd
    pop ecx
    mov [eax + 32], ecx
    pop eax

    mov eax, [esp + 24]
    mov ecx, [eax + 8]
    mov edx, [eax + 12]
    mov esi, [eax + 16]
    mov edi, [eax + 20]
    mov ebp, [eax + 24]
    mov esp, [eax + 28]

    push dword [eax + 32]
    popfd

    push dword [eax + 32]

    mov ebx, [eax + 4]

    ret
