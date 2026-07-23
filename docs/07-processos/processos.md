# 07 - Processos e Threads

## Visão Geral

O ZephyrOS suporta **processos** (com espaço de memória próprio) e **threads** (compartilham memória).

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
3. **Retorna** ao contexto salvo; processos novos ring 3 entram via `iret`

### Fluxo

```
Timer IRQ → scheduler_tick() → scheduler_schedule()
    → context_switch(&prev->context, &next->context)
        → Salva EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP
        → Restaura registradores do próximo
        → troca CR3 e retorna ao contexto salvo
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

## Primeiro processo em modo usuario

A Fase 4 adiciona um processo de teste isolado sem migrar os aplicativos
nativos. Os segmentos de usuario sao `0x1B` para codigo e `0x23` para dados.
O processo usa:

```text
Codigo: 0x00800000
Dados:  0x00801000
Stack:  0x00C00000
```

O diretorio de paginas compartilha somente os mapeamentos supervisor do
kernel. O processo entra por uma trampoline com `iret`, usa `int 0x80` para
as syscalls e recebe uma kernel stack propria pelo TSS.

Os comandos de validacao sao:

```text
usertest
usertest fault
```

`usertest fault` provoca uma page fault controlada. Excecoes originadas no
processo de usuario registram o erro e marcam somente ele como `ZOMBIE`.
Excecoes originadas no kernel continuam exibindo `KERNEL PANIC`.

Shell, Desktop, Explorer, Settings e Task Manager permanecem em ring 0 ate a
validacao do carregador e da biblioteca de usuario.

---

## IPC (Comunicação entre Processos)

### O que é?

IPC permite que processos se comunicuem trocando mensagens e compartilhando estado de foco.

### Arquivo

```
src/process/ipc.c
```

### API

```c
void     ipc_init(void);
int      ipc_send(uint32_t pid, ipc_msg_t* msg);
int      ipc_receive(ipc_msg_t* msg);
int      process_set_focus(uint32_t pid);
int      process_set_focus_fallback(uint32_t pid);
int      process_restore_focus(void);
int      process_cancel_focused_user(uint32_t exit_code);
uint32_t process_get_focus(void);
```

### Foco de Janela

O sistema de foco rastreia qual processo está em primeiro plano:

```c
if (process_set_focus(target_pid) != OK) {
    /* processo inexistente ou em estado inativo */
}
uint32_t current = process_get_focus();  // Retorna PID com foco
```

O Shell e configurado como fallback durante o boot. Quando um processo ring 3
em primeiro plano encerra, falha ou e cancelado, `process_restore_focus()`
devolve o teclado ao Shell sem depender do PID zero.

Para aplicativos externos em ring 3, `process_cancel_focused_user()` marca
somente o processo em foco como `ZOMBIE`. O teclado usa esse caminho para
`F12`; a tecla `Esc` continua sendo enviada normalmente ao aplicativo.

O mouse e o window manager usam o foco para direcionar eventos ao processo correto.

### Integração

- **Mouse**: envia eventos de clique/movimento ao processo com foco
- **Window Manager**: atualiza foco ao criar/destruir janelas
- **Keyboard**: envia teclas ao processo com foco (via callback)
