# 07 - Processos e Threads

## Visão Geral

O MiniOS suporta **processos** (com espaço de memória próprio) e **threads** (compartilham memória).

## Arquivos

```
src/process/
│   └── process.c         → Process manager + scheduler
src/thread/
│   └── thread.c          → Thread manager
src/kernel/
│   └── switch.asm        → Context switch Assembly
src/drivers/
│   └── tss.c             → Task State Segment
```

---

## Processos

### O que é um Processo?

Um processo é uma instância de um programa em execução, com:
- **Próprio espaço de memória** (via paging)
- **Próprio PID** (Process ID)
- **Estado** (pronto, rodando, bloqueado)
- **Contexto** (registradores do CPU)

### Estados do Processo

```
┌─────────┐     create     ┌─────────┐
│ UNUSED  │ ──────────────→│  READY  │
└─────────┘                └────┬────┘
                                │
                    schedule    │    process_block()
                                │
                           ┌────▼────┐
                           │ RUNNING │ ←→ scheduler
                           └────┬────┘
                                │
                    wake up     │    process_destroy()
                                │
                           ┌────▼─────┐
                           │ BLOCKED  │
                           └────┬─────┘
                                │
                                ▼
                           ┌─────────┐
                           │ ZOMBIE  │
                           └─────────┘
```

### Estrutura do Processo

```c
typedef struct {
    uint32_t pid;                    // ID do processo
    char name[32];                   // Nome
    process_state_t state;           // Estado atual
    process_context_t context;       // Registradores salvos
    page_directory_t* page_directory; // Espaço de memória
    uint32_t kernel_stack;           // Kernel stack
    uint32_t wait_ticks;             // Ticks para desbloquear
    uint32_t total_ticks;            // Ticks total rodando
} process_t;
```

### Criando um Processo

```c
process_t* proc = process_create("minha_task", entry_function);
```

Isso:
1. Aloca um slot livre no array de processos
2. Gera um PID único
3. Aloca kernel stack (4 KB)
4. Cria page directory próprio
5. Prepara o contexto inicial (pilha com EIP, EFLAGS, etc.)
6. Marca como READY

### Context Inicial

Quando um processo é criado, sua pilha é preparada assim:

```
┌─────────────────────┐
│ gs, fs, es, ds      │  ← Segmentos de dados
├─────────────────────┤
│ edi, esi, ebp       │  ← Registradores gerais
├─────────────────────┤
│ eax, ebx, ecx, edx  │
├─────────────────────┤
│ eip                 │  ← Ponto de entrada
├─────────────────────┤
│ cs = 0x08           │  ← Code segment
├─────────────────────┤
│ eflags = 0x202      │  ← Interrupções habilitadas
└─────────────────────┘
```

---

## Scheduler

### Round-Robin Preemptivo

O scheduler escolhe o próximo processo para rodar:

```c
process_t* scheduler_schedule(void) {
    process_t* best = 0;
    uint32_t min_ticks = 0xFFFFFFFF;

    // Escolhe o processo com menos ticks
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_READY) {
            if (processes[i].total_ticks < min_ticks) {
                min_ticks = processes[i].total_ticks;
                best = &processes[i];
            }
        }
    }
    return best;
}
```

### Preempção

A cada tick do timer (50 Hz), o scheduler é chamado:

```c
void scheduler_tick(void) {
    // Desbloqueia processos
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_BLOCKED) {
            if (processes[i].wait_ticks > 0) {
                processes[i].wait_ticks--;
                if (processes[i].wait_ticks == 0) {
                    processes[i].state = PROCESS_STATE_READY;
                }
            }
        }
    }
    current_process->total_ticks++;
}
```

### Yield

Um processo pode ceder voluntariamente o CPU:

```c
void process_yield(void) {
    process_t* next = scheduler_schedule();
    if (next && next != current_process) {
        context_switch(&current_process->context, &next->context);
    }
}
```

---

## Threads

### O que é uma Thread?

Uma thread é uma unidade de execução que **compartilha** o espaço de memória com outras threads do mesmo processo.

### Diferença entre Processo e Thread

| Característica | Processo | Thread |
|---------------|----------|--------|
| Memória | Própria (page directory) | Compartilhada |
| PID/TID | PID único | TID único |
| Custo | Alto (copia memória) | Baixo (só registradores) |
| Comunicação | IPC (complicado) | Memória compartilhada (fácil) |

### Estrutura da Thread

```c
typedef struct {
    uint32_t id;              // TID
    char name[32];            // Nome
    thread_state_t state;     // Estado
    uint32_t* stack;          // Stack propia
    uint32_t esp;             // Stack pointer salvo
    uint32_t eip;             // Instruction pointer
    void (*entry)(void);      // Função de entrada
    uint32_t wait_ticks;      // Ticks para desbloquear
} thread_t;
```

### Criando uma Thread

```c
thread_t* t = thread_create("minha_thread", thread_function);
```

---

## Context Switch

Quando o scheduler muda de processo/thread, ele:

1. **Salva** o contexto atual (registradores → memória)
2. **Restaura** o próximo contexto (memória → registradores)
3. **Retorna** via `iret`

### Fluxo

```
Timer IRQ → scheduler_tick() → scheduler_schedule()
    → context_switch(&prev->context, &next->context)
        → Salva EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP
        → Restaura registradores do próximo
        → iret
```

---

## TSS (Task State Segment)

O TSS é usado pelo CPU para encontrar o kernel stack quando muda de ring 3 (user) para ring 0 (kernel).

```c
tss.ss0 = 0x10;     // Kernel data segment
tss.esp0 = stack;   // Topo do kernel stack
```

Quando uma interrupção ocorre em ring 3, o CPU automaticamente:
1. Carrega SS0 e ESP0 do TSS
2. Troca para o kernel stack
3. Empilha SS, ESP, EFLAGS, CS, EIP
4. Executa o handler da interrupção
