[BITS 32]
[GLOBAL process_context_switch]
[GLOBAL tss_flush]

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

process_context_switch:
    ; [esp + 4] = prev
    ; [esp + 8] = next

    ; 1. Load prev pointer into EAX
    mov eax, [esp + 4]

    ; 2. Save registers into prev
    mov [eax + 4], ebx
    mov [eax + 16], esi
    mov [eax + 20], edi
    mov [eax + 24], ebp
    
    ; Save EFLAGS
    pushfd
    pop dword [eax + 36]

    ; Save ESP and EIP
    mov ebx, esp
    add ebx, 4 ; Caller's ESP (before CALL process_context_switch)
    mov [eax + 28], ebx

    mov ecx, [esp] ; Return address (EIP)
    mov [eax + 32], ecx

    ; 3. Load next pointer into EAX
    mov eax, [esp + 8]

    ; 4. Restore registers from next
    mov ebx, [eax + 4]
    mov esi, [eax + 16]
    mov edi, [eax + 20]
    mov ebp, [eax + 24]
    
    ; Restore EFLAGS
    push dword [eax + 36]
    popfd

    ; Restore ESP and EIP
    mov esp, [eax + 28]
    mov ecx, [eax + 32]

    ; Jump to EIP (Returns to caller, or jumps to new thread's entry_point)
    jmp ecx
