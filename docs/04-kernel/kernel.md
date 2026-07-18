# 04 - Kernel

## O que é o Kernel?

O kernel é o coração do sistema operacional. Ele controla tudo: memória, processos, drivers, e fornece serviços para as aplicações.

## Arquivos

```
src/kernel/
├── entry.asm        → Entry point Assembly
├── kernel.c         → Inicialização principal
├── panic.c          → Tratamento de erros fatais
└── switch.asm       → Context switch
```

## Entry Point (`entry.asm`)

O bootloader chama o kernel em Assembly, que por sua vez chama `kernel_main()` em C:

```nasm
_start:
    push esi              ; Passa endereço do mapa de memória
    call kernel_main      ; Chama função C
    add esp, 4
    jmp $                 ; Loop infinito se retornar
```

## Kernel Principal (`kernel.c`)

A função `kernel_main()` é o ponto de entrada em C. Ela:

1. Inicializa o vídeo
2. Mostra mensagens de boot
3. Configura IDT (interrupções)
4. Inicializa drivers
5. Detecta memória
6. Configura paging
7. Cria processos
8. Monta FAT12
9. Inicia o shell

### Ordem de Inicialização

```c
void kernel_main(uint32_t mmap_addr) {
    video_init();        // VGA 80x25
    idt_init();          // Interrupções
    keyboard_init();     // PS/2
    timer_init(50);      // PIT 50 Hz
    memory_init(mmap_addr); // E820 + heap
    paging_init();       // Page tables
    tss_init();          // Kernel stack
    process_init();      // Processos
    thread_init();       // Threads
    ata_init();          // Disco
    fat12_init();        // FAT12
    speaker_init();      // PC Speaker
    shell_init();        // Shell interativo
}
```

## Panic Handler (`panic.c`)

Quando algo crítico falha, o kernel chama `panic()`:

```c
panic("Mensagem de erro");
```

Isso:
1. Limpa a tela
2. Mostra tela vermelha com "KERNEL PANIC"
3. Exibe a mensagem de erro
4. Desliga o CPU (`hlt`)

### Quando usar

- Exceção não tratada (div by zero, page fault)
- Falha em alocação de memória
- Driver não encontrado
- Erro crítico no sistema

## Context Switch (`switch.asm`)

Quando o scheduler muda de processo/thread, ele salva o contexto atual e restaura o próximo:

```nasm
context_switch:
    pusha                 ; Salva todos os registradores
    push ds
    push es
    push fs
    push gs

    mov eax, [esp + 20]   ; Ponteiro para contexto anterior
    mov [eax + 0], eax    ; Salva EAX
    mov [eax + 4], ebx    ; Salva EBX
    ; ... outros registradores

    mov eax, [esp + 24]   ; Ponteiro para próximo contexto
    mov ebx, [eax + 4]    ; Restaura EBX
    ; ... outros registradores

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret                  ; Retorna da interrupção
```

## Struct `registers_t`

Usada para passar contexto entre handlers:

```c
typedef struct {
    uint32_t ds;                    // Segmento de dados
    uint32_t edi, esi, ebp, esp;   // Registradores gerais
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;     // Número da interrupção
    uint32_t eip, cs, eflags;      // Contexto do CPU
    uint32_t useresp, ss;
} registers_t;
```
