[BITS 32]
[GLOBAL process_context_switch]
[GLOBAL process_user_enter]
[GLOBAL process_user_termination_enter]
[GLOBAL tss_flush]

[EXTERN process_finish_user_termination]

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

process_user_enter:
    ; O contexto ja deixou ESP apontando para um frame completo de IRET.
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iret

process_user_termination_enter:
    ; O IRET saiu do frame de ring 3 antes de chegarmos aqui. Recarregar
    ; segmentos de kernel evita que a troca use seletores de usuario.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call process_finish_user_termination

.termination_halt:
    cli
    hlt
    jmp .termination_halt

process_context_switch:
    ; [esp + 4] = prev
    ; [esp + 8] = next

    ; pushad preserva tambem os registradores usados como temporarios.
    pushad

    ; Depois de pushad: EDI, ESI, EBP, ESP original, EBX, EDX,
    ; ECX, EAX, retorno, prev e next.
    mov edx, [esp + 36]
    mov eax, [esp + 28]
    mov [edx + 0], eax
    mov eax, [esp + 16]
    mov [edx + 4], eax
    mov eax, [esp + 24]
    mov [edx + 8], eax
    mov eax, [esp + 20]
    mov [edx + 12], eax
    mov eax, [esp + 4]
    mov [edx + 16], eax
    mov eax, [esp + 0]
    mov [edx + 20], eax
    mov eax, [esp + 8]
    mov [edx + 24], eax

    mov eax, [esp + 12]
    add eax, 4
    mov [edx + 28], eax
    mov eax, [esp + 32]
    mov [edx + 32], eax
    pushfd
    pop eax
    mov [edx + 36], eax

    ; O CR3 e trocado antes de acessar o contexto do proximo processo.
    mov edx, [esp + 40]
    mov eax, [edx + 64]
    mov cr3, eax

    ; Converte o EIP salvo em um retorno artificial. Isso permite iniciar
    ; tanto uma funcao ring 0 quanto a trampoline que executa IRET para ring 3.
    mov eax, [edx + 32]
    mov ecx, [edx + 28]
    sub ecx, 4
    mov [ecx], eax
    push dword [edx + 36]
    popfd
    mov esp, ecx

    mov eax, [edx + 0]
    mov ebx, [edx + 4]
    mov ecx, [edx + 8]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    mov ebp, [edx + 24]
    mov edx, [edx + 12]
    ret
