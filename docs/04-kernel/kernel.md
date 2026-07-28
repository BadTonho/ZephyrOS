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
    pci_init();
    ac97_init();
    device_manager_init();
    power_init();
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

## Servicos S1.1: dispositivos e energia

O kernel inicializa PCI antes do AC97 e, depois dos drivers, cria os servicos
de dispositivos e energia. Suas consultas sao somente de leitura, falham de
forma controlada e aparecem no `health`; apenas `power_shutdown()` e terminal.

- `device_manager`: mantem um snapshot estatico de PCI, ATA, AC97, PS/2, PIT,
  VGA, VESA e PC Speaker. Nao reinicializa drivers, nao grava no disco e nao
  habilita ou desabilita hardware.
- `power`: informa as capacidades reais do sistema atual. S0 e idle HLT/C1
  estao disponiveis; S1-S4 permanecem indisponiveis. S5 fica disponivel
  somente quando o snapshot ACPI atende ao contrato seguro da S1.4; nos
  demais casos, `shutdown` usa o fallback terminal HLT.

Os headers `core/device_manager.h` e `core/power.h` definem as estruturas de
snapshot e status. O snapshot de dispositivos guarda somente metadados;
`device_manager_format_text()` monta as strings de exibicao sob demanda para
evitar reservar memoria estatica por descricao. Suas consultas retornam
codigos de erro quando chamadas antes da inicializacao ou com destinos nulos.

## Servico S1.2: descoberta ACPI

Depois de `memory_init()` e antes de `paging_init()`, o kernel entrega ao
driver ACPI a referencia temporaria do mapa E820. O driver valida os intervalos
fisicos, cria um snapshot estatico e descarta essa referencia antes da
ativacao do paging. Isso preserva o layout High Memory e evita mapear EBDA,
BIOS ou tabelas de firmware no espaco virtual permanente.

O componente `ACPI` do `health` fica `READY` com raiz, FADT e DSDT validas,
`DEGRADED` quando existe apenas um inventario parcial e `DISABLED` quando nao
ha raiz utilizavel. `Power` continua `READY` em todos esses cenarios porque
seu diagnostico ainda informa S0/HLT e as limitacoes reais. Os campos
`acpi_power_tables_available` e `acpi_partial` nao habilitam transicoes.

## Servico S1.3: preparacao observavel do S5

O snapshot ACPI agora copia os descritores PM1a/PM1b, observa `SCI_EN` durante
o bootstrap e reconhece somente a declaracao AML `_S5_` da DSDT. Nenhuma
dessas informacoes e usada para escrever no hardware. Valores ambiguos,
malformados, MMIO ou incompativeis permanecem indisponiveis para transicao.

`power_status_t` expoe `acpi_pm1_control_available`,
`acpi_mode_known`, `acpi_mode_enabled` e `acpi_s5_declared`. Esses campos
separam a capacidade declarada pelo firmware da capacidade implementada pelo
kernel: S5 continua `POWER_CAPABILITY_SIMULATED` e `hardware_poweroff`
continua `POWER_CAPABILITY_UNAVAILABLE`.

As regras do `health` nao mudam. A ausencia de PM1 ou `_S5_` nao degrada uma
raiz ACPI valida, e o componente `Power` permanece `READY` por ser um servico
diagnostico com fallback.

## Servico S1.4: desligamento fisico ACPI S5

`power_status_t` acrescenta `acpi_mode_enable_available` e
`acpi_s5_transition_ready`. Quando a transicao esta pronta, S5 e
`hardware_poweroff` passam a `POWER_CAPABILITY_AVAILABLE`; sem esse contrato,
S5 permanece simulado e o desligamento fisico permanece indisponivel.

`power_shutdown()` e a unica operacao terminal de desligamento. Ela para PC
Speaker e AC97 em best effort, tenta `acpi_enter_s5()` apenas quando a
capacidade consolidada esta pronta e termina em `CLI+HLT` se a tentativa for
bloqueada. Shell, kernel, Menu Iniciar Classic/Modern e Task Manager usam esse
mesmo servico; nao existem mais loops locais de shutdown nem escrita na porta
privada `0xB004` do QEMU.

O servico nao altera processos ou filesystem e nao implementa flush,
desmontagem, suspensao, hibernacao ou reboot. `power_shutdown()` nunca retorna;
`acpi_enter_s5()` retorna apenas quando sua pre-validacao impede qualquer
escrita no hardware.

## Servicos S2.1/S2.2: rede observavel e E1000

`network_manager` filtra por copia o snapshot PCI e mantem ate quatro
controladores de classe `0x02`. O servico copia identificadores, localizacao,
IRQ e BAR0-BAR5; os IDs `net-pci-BB:DD.F` permanecem estaveis e consultas
tambem aceitam `net-pci-BB-DD.F`.

`network_manager_status_t` separa inventario, modelos reconhecidos e drivers
ativos. `network_interface_info_t` preserva os metadados PCI e informa modelo,
estado do driver, link, MAC, contadores e ultimo erro. `network_manager_refresh()`
continua sendo apenas uma nova varredura PCI: nao reseta, realoca DMA nem
reinicializa o E1000.

O componente `Network` do `health` segue estas regras:

- `READY`: existe pelo menos uma interface com driver ativo;
- `DEGRADED`: controlador detectado sem driver ou inventario parcial;
- `DISABLED`: nenhum controlador detectado ou snapshot indisponivel.

Na S2.2, o kernel tenta inicializar o E1000 `8086:100E` depois do PCI e antes
do snapshot de rede. A inicializacao usa BAR0 MMIO, DMA PMM, IRQ `32 + linha
PCI`, MAC RAL/RAH e filas Ethernet L2. Sucesso deixa `Network` pronto e
RX/TX disponivel; IPv4 continua indisponivel. NIC ausente, BAR/IRQ invalido,
timeout, falta de memoria ou conflito de IRQ deixam apenas Network degradado.
RTL8139 `10EC:8139` continua sem driver. A sincronizacao com recovery e
idempotente para que `device-scan` repetido sem mudanca nao aumente o contador
de falhas.

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

Desde a K3, `memory_get_heap_stats()` expoe capacidade, uso, blocos livres e
ocupados, maior bloco livre, fragmentacao externa, falhas de alocacao e
rejeicoes de `kfree`. A inspecao valida limites, encadeamento e metadados
antes de percorrer o heap; corrupcao produz diagnostico controlado, sem seguir
um ciclo invalido. `memory_get_pmm_stats()` separa paginas entregues pelo PMM,
falhas de alocacao e liberacoes rejeitadas. Memoria por processo continua fora
do contrato ate haver atribuicao confiavel para todas as alocacoes.

`health` preserva todos os blocos existentes e acrescenta fragmentacao,
rejeicoes do PMM e diretorios/paginas de usuario ativos. Esses campos sao
diagnosticos internos; `mem` e a App API continuam mostrando apenas memoria
global.

O contrato de `memory.h` mantém os bitmaps do PMM em `0x88000–0x98000` e a
stack inicial em `0x98000–0x9F000`, mas posiciona kernel e BSS em
`0x00100000–0x00800000`. A ABI ZAPP continua em `0x00800000–0x01000000`, o
heap ocupa `0x01000000–0x01400000` e o PMM entrega páginas mapeadas por
identidade somente a partir de `0x01400000`.

A inicialização exige 32 MiB de RAM e confirma no E820 que as áreas baixas,
o kernel e o heap são utilizáveis. Páginas abaixo de `0x01400000` permanecem
reservadas, impedindo colisões entre boot, kernel, ZAPP e heap.

Falhas recuperaveis retornam erro e desabilitam somente o componente afetado.
Excecoes fatais e corrupcao estrutural continuam encaminhadas para `panic`.
O `boot.asm` permanece inalterado e a politica de escalonamento nao faz parte
desta etapa.
