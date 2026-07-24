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
8. Monta FAT12/FAT32
9. Inicia o shell
10. Habilita o gate DPL3 somente depois do TSS, paging, Idle e processos
    essenciais estarem prontos

### Ordem de Inicialização

```c
void kernel_main(uint32_t mmap_addr, uint32_t vesa_info_addr) {
    /* Video, logs, IDT, teclado, mouse e timer. */
    vesa_init(vesa_info_addr);
    video_init();
    log_init();
    recovery_init();
    idt_init();
    keyboard_init();
    mouse_init();
    timer_init(50);

    /* Memoria e contratos basicos. */
    memory_init(mmap_addr);
    app_api_init();
    syscall_init();                 // inicia com gate DPL 0
    paging_init();
    vesa_init_backbuffer();
    tss_init();
    process_init();
    process_bootstrap_idle();
    ipc_init();
    thread_init();

    /* Dispositivos, filesystem e interfaces nativas. */
    ata_init();
    fs_init();
    speaker_init();
    ac97_init();
    icons_init();
    taskbar_init();
    desktop_init();
    settings_init();
    wm_init();

    /* Servicos em segundo plano, Shell e cena inicial. */
    process_create("Zephyr System", system_process_main);
    process_create("Shell", shell_process_main);
    process_create("Desktop", desktop_process_main);
    syscall_enable_user_mode();     // eleva int 0x80 para DPL 3
    app_loader_init();
    desktop_draw();                 // Shell nao e a tela padrao
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

- Exceção originada no kernel (div by zero, page fault, GPF e similares)
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
    mov cr3, [next_cr3]   ; Troca o espaço de endereços
    ret                    ; Retorna ao contexto salvo
```

## Isolamento ring 3

O kernel possui segmentos de usuario em `0x1B` (codigo) e `0x23` (dados).
O processo de teste usa codigo em `0x00800000`, dados em `0x00801000`, pagina
de lancamento em `0x00802000` e stack em `0x00C00000`. Seu diretorio compartilha as tabelas supervisor do kernel,
mas as paginas do kernel permanecem com o bit `user` desativado.

O dispatcher de `int 0x80` valida `CS`, `SS`, o processo atual e todas as
faixas de memoria antes de copiar dados para as APIs internas. O comando
`usertest` exercita `console_write`, `uptime`, `memory_info` e `process_exit`.
`usertest fault` valida o encerramento controlado de uma page fault de usuario.

Excecoes de ring 3 encerram somente o processo afetado. Excecoes de ring 0,
falhas estruturais de paging e corrupcao do kernel continuam encaminhadas ao
`panic`.

## Serviços de aplicativos

Depois de memória, paging, TSS e processos essenciais, o kernel inicializa a
App API e o dispatcher `int 0x80`. O gate começa restrito a DPL 0 e é elevado
para DPL 3 somente quando a fronteira de modo usuário está pronta. A plataforma
atual inclui arquivos, IPC, imagens `.ZAP`/`ZAPP`, foco de aplicativo externo
e uma página de lançamento com argumentos. O Shell continua nativo; `echo` é a
primeira migração ring 3 e mantém fallback nativo.

Consulte [API de Aplicativos e Syscalls](../melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md)
para a ABI estável e [Roadmaps por Etapa](../roadmaps/README.md) para a ordem
das próximas migrações.

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

## Fundacao e invariantes de estabilidade

A etapa de fundacao preserva o scheduler round-robin e adiciona contratos
defensivos nas APIs centrais:

- `process_init()` limpa os registros e reinicia o PID e o indice do scheduler;
- o PID 0 e o processo Idle nao podem ser destruidos;
- `process_get_by_pid()` procura pelo PID real, sem usar PID como indice;
- quando nao ha processo `READY`, o scheduler retorna ao Idle;
- `ipc_send()` valida mensagem, destino e capacidade da fila, e acorda um
  processo bloqueado quando entrega uma mensagem valida;
- `paging_map_page()` valida alinhamento, flags e a existencia do diretorio;
- `paging_is_ready()` permite que interfaces diagnostiquem o estado do paging;
- `health` exibe processos, threads, ticks, IPC, paging, memoria e recovery.

Falhas recuperaveis retornam erro e desabilitam somente o componente afetado.
Excecoes fatais e corrupcao estrutural continuam encaminhadas para `panic`.
O `boot.asm` e a politica de escalonamento nao fazem parte desta etapa.
