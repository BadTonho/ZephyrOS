# 07 - Processos e Threads

## Visão Geral

O ZephyrOS suporta **processos** (com espaço de memória próprio) e **threads** (compartilham memória).

## Arquivos

```
src/process/
│   ├── process.c         → Process manager + scheduler
│   └── signal.c          → sinais assíncronos de processos ring 3
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
    uint32_t parent_pid;              // Pai ou PID 0
    uint32_t pending_signals;         // Bitmap coalescido
    uint32_t blocked_signals;         // Máscara bloqueada
    app_signal_action_t signal_actions[18];
    uint32_t cancel_exit_code;
    uint8_t cancel_pending;
    vfs_fd_table_t fd_table;
} process_t;
```

Desde a VFS2, `fd_table` tambem armazena o diretorio de trabalho canonico do
processo. O Idle e processos sem pai iniciam em `/`; novos processos nativos e
ring 3 herdam o `cwd` do processo criador. A copia e independente: `chdir` em
um processo nao altera o pai nem os irmaos. Descarte, encerramento e destruicao
invalidam a tabela, e as invariantes globais recusam `cwd` vazio, nao absoluto
ou pertencente a uma montagem removida.

Desde a MM3, `process_t` acrescenta ao final os campos append-only
`user_code_image`, `user_data_image`, `user_data_size` e `user_launch`. Os dois
buffers pertencem ao processo e mantêm cópias kernel-owned do código e dos
dados até o descarte ou a destruição; `user_launch` mantém a cópia persistente
de `app_launch_info_t`. A criação suspensa de um processo ring 3 registra as
VMAs sem alocar páginas físicas de usuário; o paging lazy materializa cada
página no primeiro acesso e o ciclo de destruição libera os buffers e os frames
residentes sem alterar as assinaturas de criação ou a ABI das aplicações.

### Snapshot de introspeccao PROC2

`process_t` acrescenta ao final `event_generation`, uma geracao monotonicamente
crescente que nao e reutilizada junto com o PID. As APIs
`process_snapshot_copy()`, `process_snapshot_list()` e
`process_snapshot_copy_vmas()` retornam copias sem ponteiros para o procfs.
Elas incluem Idle/PID 0, processos nativos, processos ring 3 e zombies ainda
nao reapados. A memoria publicada soma stack de kernel, paginas de usuario
residentes e imagens de codigo/dados; VMAs reservadas nao contam como
residentes. A copia de VMA valida PID e geracao e retorna `ERR_AGAIN` quando a
identidade muda durante a captura.

### Consumidores PROC4

O Task Manager Classic passou a consumir `/proc/<pid>/status` por meio da VFS,
mantendo apenas uma matriz de valores e a identidade `pid + generation` por
linha. A lista é repetida uma vez quando há churn; processos que permanecem
instáveis não entram na atualização corrente. O cálculo de CPU usa diferenças
de `total_ticks` somente quando PID e geração coincidem.

As ações do Classic revalidam a geração antes de consultar ou sinalizar o
processo atual. O caminho Simple, a aba de threads e as estruturas internas de
processos permanecem inalterados. Campos de contexto e ponteiros internos não
são exportados pela visão Classic migrada.

### Criando um Processo

```c
process_t* proc = process_create("minha_task", entry_function);
```

Isso:
1. Aloca um objeto `process_t` do cache de processos e o associa a um slot livre
2. Gera um PID único
3. Aloca kernel stack (4 KiB nativo ou 8 KiB para ring 3)
4. Cria page directory próprio
5. Prepara o contexto inicial (pilha com EIP, EFLAGS, etc.)
6. Instala stdin, stdout e stderr na tabela de descritores
7. Marca como READY

Desde a VFS1, cada processo possui 32 descritores. A tabela e instalada em
toda criacao e liberada nos caminhos de descarte, encerramento e destruicao;
assim, a limpeza de arquivos nao depende exclusivamente do App Loader. O lock
da VFS protege tabelas e contadores, mas e liberado antes de callbacks de
filesystem, console ou espera IPC.

O cancelamento externo de um processo ring 3 bloqueado e dividido em duas
etapas. O solicitante registra o codigo e acorda a espera; o proprio processo
desempilha a syscall, encerra a operacao VFS e aplica o cancelamento antes do
retorno a ring 3. Isso impede que a destruicao libere descritores ou a stack
enquanto uma leitura bloqueante ainda esta ativa.

`process_create()` mantém a stack nativa padrão de 4 KiB. Processos que
executam caminhos com maior consumo podem usar `process_create_with_stack_size()`
entre 4 KiB e 16 KiB, sempre alinhada a 16 bytes. A EP6.4 reserva 16 KiB para
o `Zephyr System`, e a SYNC3 reserva o mesmo para o processo ring0
`Zephyr kworker`; o Shell tambem conserva 16 KiB. Desktop, Idle e demais
processos nativos permanecem com 4 KiB. Processos ring 3 usam 8 KiB de kernel
stack para acomodar syscalls que atravessam VFS, Storage e FAT/LFN; a stack é
privada do kernel e essa reserva não altera a ABI nem a stack de usuário.

Cada stack nativa tem canários inferior/superior e área útil preenchida para
medir high-water. `process_stack_get_info()` consulta um PID,
`process_stack_validate_all()` verifica a tabela inteira e
`process_stack_check_current()` protege o worker HTTP/TLS. O Shell expõe
`stack status` e `stack check`. A margem de 1 KiB no worker encerra a sessão
HTTP com `ERR_OVERFLOW`; canário rompido registra PID/nome e interrompe o
kernel com `panic`.

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

### Round-Robin com Idle arquitetural

O scheduler percorre os PIDs normais em ordem circular e escolhe o primeiro
processo `READY` depois do ultimo selecionado. O PID 0 (`System Idle`) fica
fora dessa rotacao: ele so e escolhido quando nao ha nenhum processo normal
pronto. O bootstrap transfere o controle por um contexto separado para a stack
propria do PID 0; a stack do `kernel_main` nunca e salva como se fosse a stack
do Idle. Assim, o Idle preserva a continuidade do kernel sem disputar uma
fatia com trabalho real.

### Preempção e yield

O PIT executa `scheduler_tick()` a 50 Hz para atualizar bloqueios temporizados
e ticks do processo atual. Se a interrupcao ocorreu em ring 3, chama
`scheduler_preempt_user()` e aplica o quantum fixo de 1 tick (20 ms). Codigo
nativo em ring 0 nao e preemptado pelo timer porque compartilha estruturas nao
reentrantes; ele cede explicitamente com `process_yield()`.

```c
if (interrupted_user) {
    scheduler_preempt_user();
}
```

O desbloqueio temporizado continua no tick:

```c
void scheduler_tick(void) {
    // Desbloqueia processos
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i] && processes[i]->state == PROCESS_STATE_BLOCKED) {
            if (processes[i]->wait_ticks > 0) {
                processes[i]->wait_ticks--;
                if (processes[i]->wait_ticks == 0) {
                    processes[i]->state = PROCESS_STATE_READY;
                }
            }
        }
    }
    current_process->total_ticks++;
}
```

`process_yield()` continua sendo a cessao cooperativa usada por processos
nativos, pelo Idle e pelos caminhos de bloqueio. O Idle executa `sti; hlt` antes
de ceder, fechando a janela de corrida entre a verificacao de trabalho e a
espera; o timer e as demais IRQs acordam a CPU sem polling ativo. System e
Desktop bloqueiam por um tick depois de cada ciclo de servico quando nao ha
trabalho imediato.

O scheduler soma `idle_ticks` quando o PID 0 esta em execucao e `active_ticks`
para os demais processos. A contagem de Idle e mantida igual ao
`total_ticks` do PID 0. `cpu usage` mostra os deltas desde o boot ou desde
`cpu usage reset`, sem confundir essa residencia de scheduler com medicao
eletrica ou com RDTSC/PMU. Prioridades, quantum maior e mudancas na relacao
processo/thread continuam fora do PWR1.

---

## Espera por eventos e filas FIFO (R3 / SYNC2)

O contrato R3 adicionou espera cooperativa por canal sem alterar a ABI dos
aplicativos ring 3. A SYNC2 consolidou esse contrato como
`wait_queue_head_t`: uma fila intrusiva FIFO registrada por ID geracional.
`wait_channel_t` permanece como alias compativel. Cada processo/thread embute
sua propria `wait_queue_entry_t`, portanto bloquear nao aloca memoria.

Cada processo possui uma fila IPC embutida; sockets mantem filas por slot. O
armazenamento da fila deve iniciar zerado, e o servico recusa reinicializacao
ou destruicao enquanto houver waiters. O processo tambem conserva a ultima
geracao IPC consumida. Assim, a condicao de `ipc_wait()` aceita tanto uma
mensagem publicada quanto um novo sinal do canal, inclusive quando o produtor
nao precisa criar uma mensagem artificial.

```c
typedef enum {
    WAIT_REASON_NONE,
    WAIT_REASON_EVENT,
    WAIT_REASON_TIMEOUT,
    WAIT_REASON_CANCELLED,
    WAIT_REASON_DEVICE_UNAVAILABLE,
    WAIT_REASON_SIGNAL
} wait_reason_t;

typedef int (*wait_condition_fn_t)(void* context, uint8_t* out_ready);

int init_waitqueue_head(wait_queue_head_t* queue, const char* owner);
int wait_event(wait_queue_head_t* queue, wait_condition_fn_t condition,
               void* context, wait_reason_t* out_reason);
int wait_event_timeout(wait_queue_head_t* queue,
                       wait_condition_fn_t condition, void* context,
                       uint32_t timeout_ticks, wait_reason_t* out_reason);
int wake_up(wait_queue_head_t* queue, uint32_t* out_woken);
int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken);

int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason);
int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken);
int process_cancel_wait(process_t* process);

int thread_wait(wait_channel_t* channel, uint32_t observed_condition,
                uint32_t timeout_ticks, wait_reason_t* out_reason);
int thread_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                        wait_reason_t reason, uint32_t* out_woken);
int thread_cancel_wait(thread_t* thread);
```

`WAIT_TIMEOUT_IMMEDIATE` retorna `TIMEOUT` sem bloquear e
`WAIT_TIMEOUT_INFINITE` usa espera sem deadline. Demais prazos sao convertidos
para um tick absoluto e comparados com aritmetica segura de wraparound. A
transicao para `BLOCKED`, a verificacao da geracao e o encadeamento FIFO
ocorrem na mesma regiao critica, evitando perder um evento entre o teste da
condicao e o bloqueio. Um wake apenas move a tarefa para o estado executavel;
a troca de contexto continua pertencendo ao scheduler.

O produtor deve publicar os dados antes de acordar um ou todos os waiters.
`wake_up` remove o primeiro waiter FIFO; `wake_up_all` remove todos. A
condicao e reavaliada depois de cada evento, usando o mesmo deadline absoluto,
pois uma notificacao nao garante que outro consumidor ainda nao retirou o
recurso.
Cancelamento individual, cancelamento coletivo e indisponibilidade registram
motivos distintos. Processos e threads mantem o mesmo contrato de metadados,
mas os canais continuam estaticos e fornecidos pelo consumidor. O scheduler
continua aceitando `process_block()` e `thread_block()` para os consumidores
legados; esperas R3 usam deadline e metadados de canal.

`WAIT_REASON_SIGNAL` identifica a remoção de um processo da fila por um sinal
entregável. A remoção usa a própria entrada intrusiva e ocorre exatamente uma
vez. Um sinal bloqueado permanece pendente e não acorda a tarefa; um processo
suspenso pelo loader, sem entrada vinculada a uma Wait Queue, também não é
liberado prematuramente.

O Shell expoe `wait status`, `wait list`, `wait check` e `wqinfo`. IPC, Editor,
Explorer e Task Manager dormem quando a fila de mensagens esta vazia e sao
acordados por `ipc_send()` depois da publicacao. Workers que publicam seu
proprio estado acordam pelo incremento da geracao do mesmo canal; a geracao
permanece pendente ate ser observada pelo consumidor. `wqinfo` usa snapshots
somente-leitura do registro e preserva a ordem FIFO dos waiters.
Na Fase 5, o mesmo canal funciona como agregador de eventos: o processo de
sistema o sinaliza para progresso de rede, indice, timer e conclusao de
processo, sem criar mensagens artificiais. `process_get_event_generation()`
permite ao Shell rejeitar conclusoes de processos que pertencem a uma geracao
anterior do job.

---

## Sinais assíncronos (SYNC4)

`src/include/process/signal.h` define o contrato do núcleo de sinais para
processos ring3. Cada processo possui pendências coalescidas por bitmap,
máscara bloqueada, tabela de ações, contexto salvo no kernel e métricas. O
registro aceita `SIGINT`, `SIGKILL`, `SIGSEGV`, `SIGTERM` e `SIGCHLD`.

`SIGKILL` e `SIGSEGV` são fatais, não bloqueáveis e não capturáveis.
`SIGCHLD` tem ação padrão de ignorar; os demais sinais capturáveis encerram o
processo por padrão. Repetir um sinal que já está pendente não cria uma
segunda entrega, apenas incrementa a coalescência. Sinais recebidos durante
um handler aguardam o retorno, pois não existem handlers aninhados.

No fim dos handlers C de exceção, syscall e IRQ, o kernel seleciona no máximo
um sinal entregável antes do `iret`. Sinais fatais têm prioridade; depois é
usado o menor número. Para uma ação capturada, o kernel salva o frame original
em `process_t`, grava na stack ring3 o endereço do trampoline e o número do
sinal e redireciona `EIP` ao handler validado dentro do código do ZAPP. Um
`ret` do handler chega ao trampoline da página de lançamento, que executa a
syscall `signal_return` e restaura o contexto original.

O PID pai é registrado na criação. Ao entrar em `ZOMBIE`, o filho atualiza o
último filho do pai e gera `SIGCHLD` uma única vez. O App Loader continua
coletando processos; não existe `waitpid`. Quando o pai é destruído, seus
filhos são reparentados para PID 0.

As APIs públicas somente-leitura fornecem snapshots, métricas e
`process_signal_validate_state()`. `regcheck full` consulta somente as
invariantes; `health check` denuncia contexto salvo órfão ou vínculo inválido.
`kill` restringe o destino a ring3 e `sigtest` executa a fixture privada.

---

## Trabalho assincrono do kernel (R4 / SYNC3)

A `Zephyr kworker` e um processo ring0 dedicado que usa a Wait Queue
`KWORKER`. Quando as filas `HIGH` e `NORMAL` estao vazias, ele permanece
bloqueado ate um novo agendamento ou o prazo absoluto mais proximo. Um wake
somente o torna `READY`; a troca de contexto continua pertencendo ao
scheduler cooperativo.

Bottom-Halves e timers usam prioridade alta. Rede, sockets e indexacao usam
prioridade normal. Cada ciclo possui orcamento para as duas classes, e todos
os callbacks executam fora de IRQ com interrupcoes habilitadas. System e o
loop principal drenam a mesma fila apenas se a kworker estiver ausente ou
morta.

Nesta etapa a kworker ainda consome um slot e uma stack de processo e nao
participa do scheduler independente de `thread_t`. Essa limitacao aceita esta
registrada como `DT100-002` e deve ser quitada pela K5 antes da v1.0.0.
O comando `workq check` inclui um percurso real Shell -> Wait Queue -> kworker
-> wake, alem da fixture privada das filas.

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
    uint32_t owner_pid;       // PID do processo criador
} thread_t;
```

### Criando uma Thread

```c
thread_t* t = thread_create("minha_thread", thread_function);
```

`thread_create()` reserva uma stack cooperativa de quatro paginas (16 KiB).
Esse limite cobre workers que atravessam VFS, Storage e FAT/LFN, como os
estagios do pipeline do Shell, sem alterar a ABI dos processos ou das
aplicacoes ring 3.

Threads criadas em contexto de processo registram o PID proprietario. Quando
um worker continua executando durante o bloqueio do processo criador, suas
operacoes VFS usam a tabela de descritores desse processo, mesmo que o
scheduler de processos esteja executando outro contexto.

Desde a MM1, a tabela interna de processos armazena ponteiros para objetos
obtidos do cache `process`; a tabela interna de threads usa o cache `thread`.
Os objetos sao devolvidos ao cache em todos os caminhos de descarte e
destruicao. As stacks continuam no `kmalloc`, com canarios e limites proprios.
`schedcheck` e `regcheck full` tambem validam a integridade global dos caches.
`thread_is_ready()` permite ao kernel publicar falha de criacao do cache sem
confundir scheduler vazio com scheduler indisponivel.

---

## Context Switch

Quando o scheduler muda de processo/thread, ele:

1. **Salva** o contexto atual (registradores → memória)
2. **Restaura** o próximo contexto (memória → registradores)
3. **Retorna** ao contexto salvo; processos novos ring 3 entram via `iret`

### Fluxo

```
Timer IRQ → scheduler_tick()
    → se interrompeu ring 3: scheduler_preempt_user()
        → scheduler_schedule() → context_switch(&prev->context, &next->context)
            → Salva EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP
            → Restaura registradores do próximo
            → troca CR3 e retorna ao contexto salvo
```

## Metricas e invariantes K1/K2/PWR1

`scheduler_get_stats()` informa contadores acumulados de trocas reais de
contexto, yields cooperativos, preempcoes de ring 3 e fallbacks para o Idle,
alem do quantum atual de usuario (1 tick). Os campos append-only
`idle_ticks` e `active_ticks` registram a residencia baseada no PIT.
`kmetrics` mostra os deltas desses contadores na janela desde o boot ou o
ultimo reset; `cpu usage` calcula as porcentagens sem dividir quando a janela
nao tem ticks.

`scheduler_validate_invariants()` consulta o estado protegido do scheduler e
suas estruturas de processos. Ele valida processo atual, Idle, unicidade e
contagem dos PIDs, estados de bloqueio/zumbi e `idle_accounting_valid`; um
processo ring 3 suspenso pelo loader pode permanecer `BLOCKED` com espera zero.
A funcao retorna `OK`, `ERR_NULL` ou `ERR_STATE` e
nunca tenta reparar uma inconsistencia. O comando Shell `schedcheck` apresenta
esse resultado de forma compacta.

`ipc_get_pending_count()` soma mensagens ainda pendentes, enquanto
`ipc_get_stats()` mantem os contadores acumulados de envio, recebimento,
falhas e fila cheia. Essas metricas descrevem atividade do scheduler e das
filas; nao medem uso real de CPU.

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
Lancamento: 0x00802000
Stack:  0x00C00000
```

O diretorio de paginas compartilha somente os mapeamentos supervisor do
kernel. O processo entra por uma trampoline com `iret`, usa `int 0x80` para
as syscalls e recebe uma kernel stack propria pelo TSS.

O contexto de lancamento e uma pagina independente com `app_launch_info_t`.
Ela armazena argumentos como offsets e tamanhos relativos ao texto bruto, sem
expor ponteiros internos do kernel. Essa separacao preserva a compatibilidade
das imagens que ja usam a pagina de dados em `0x00801000`.

Os comandos de validacao sao:

```text
usertest
usertest fault
```

`usertest fault` provoca uma page fault controlada. Excecoes originadas no
processo de usuario registram o erro e marcam somente ele como `ZOMBIE`.
Excecoes originadas no kernel continuam exibindo `KERNEL PANIC`.

Shell, Desktop, Explorer, Settings e Task Manager permanecem em ring 0. O
`echo` é a primeira ferramenta nativa migrada para uma imagem ZAPP em ring 3,
com fallback nativo. A Fase 6C fará novas migrações de comandos CLI simples,
uma por vez, sem antecipar a migração de interfaces gráficas.

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

O encaminhador de teclado usa lotes pequenos por ciclo do processo System.
Isso preserva a ordem dos scancodes, mas permite que o processo em foco
consuma mensagens entre os lotes e evita saturar a fila IPC apos saidas longas
do Shell.

O mouse e o window manager usam o foco para direcionar eventos ao processo correto.

As solicitacoes nativas do menu Iniciar sao acrescentadas somente ao fim de
`ipc_app_request_t`, preservando os valores anteriores. A U4 acrescenta
`IPC_APP_OPEN_UPDATER`: o processo System a envia ao Shell, que suspende o
terminal, fecha uma interface Classic conflitante quando necessario e abre ou
focaliza a instancia unica do System Updater. O pedido nao transporta pacote,
senha ou dados de atualizacao.

O AS3 acrescenta `IPC_APP_OPEN_APP_STORE` ao fim da mesma enumeracao. O pedido
abre ou focaliza a instancia unica da App Store e nao transporta caminho de
pacote, credencial ou argumento de ZAPP.

### Integração

- **Mouse**: envia eventos de clique/movimento ao processo com foco
- **Window Manager**: atualiza foco ao criar/destruir janelas
- **Keyboard**: envia teclas ao processo com foco (via callback)
O ciclo de diagnosticos do Shell usa `process_cancel_user_test()` para
cancelar explicitamente o processo ring 3 reservado ao `usertest`, mantendo o
resultado no mesmo canal de coleta usado pelo encerramento normal. A rotina
recusa PIDs que nao pertencem a um UserTest e nao altera a API publica de
`shell.h`.
