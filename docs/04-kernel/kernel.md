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
    push edi
    push esi
    call kernel_main
    add esp, 8
    jmp $
```

O recovery loader entrega o mapa E820 em `ESI` e o bloco VESA em `EDI`. A
entrada Assembly converte os dois registradores para
`kernel_main(mmap_addr, vesa_info_addr)`.

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
    if (mouse_init() == OK) {
        mouse_set_callback(global_mouse_handler);
    }
    if (timer_init(50U) != OK) {
        panic("TIMER: falha ao inicializar PIT");
    }

    /* Memoria, ACPI pré-paging e contratos básicos. */
    memory_init(mmap_addr);
    acpi_init(memory_get_mmap());
    app_api_init();
    syscall_init();                 // inicia com gate DPL 0
    paging_init();
    vesa_init_backbuffer();
    tss_init();
    process_init();
    process_bootstrap_idle();
    ipc_init();
    thread_init();

    /* Camada de bloco, filesystem, storage e índice. */
    block_init();
    ata_init();
    fs_init();
    storage_init();
    file_index_init();
    update_init();

    /* Dispositivos, áudio, USB, rede e energia. */
    speaker_init();
    pci_init();
    usb_manager_init();
    storage_refresh();
    ac97_init();
    device_manager_init();
    network_manager_init();
    update_remote_init();
    power_init();

    /* Interface gráfica Classic, display e janelas. */
    icons_init();
    display_init();
    taskbar_init();
    desktop_init();
    settings_init();
    wm_init();
    updater_init();

    /* Processos de sistema, Shell, modo usuário e App Store. */
    process_create("Zephyr System", system_process_main);
    process_create("Shell", shell_process_main);
    process_create("Desktop", desktop_process_main);
    syscall_enable_user_mode();     // eleva int 0x80 para DPL 3
    app_loader_init();
    app_package_init();
    app_remote_init();
    app_catalog_init();
    appstore_init();

    /* Desktop e a cena padrao; o Shell abre por solicitacao. */
    desktop_draw();
}
```

## Log circular e observabilidade

O serviço público de log mantém 32 registros estruturados em ordem
cronológica. Cada `log_record_t` contém sequência monotônica, primeiro e
último tick, nível, módulo de até 15 caracteres, mensagem de até 79 caracteres,
quantidade de ocorrências, flags de truncamento e um código de erro opcional.
Quando o ring fica cheio, o registro mais antigo é sobrescrito e a sequência
continua avançando. Logs emitidos antes da inicialização do timer usam tick
zero.

Os níveis de armazenamento e console são independentes e começam em `INFO`.
O buffer deve ser pelo menos tão detalhado quanto o console; por isso,
`log_set_buffer_level()` e `log_set_console_level()` retornam `ERR_INVALID`
quando a combinação quebraria essa regra. A API antiga permanece compatível:
`log_set_level()` altera os dois níveis, `log_get_level()` consulta o console,
as macros `LOG_ERROR`, `LOG_WARN`, `LOG_INFO` e `LOG_DEBUG` não mudaram e
`log_get_buffer()` serializa os registros como `[NIV] [MODULO] mensagem`.

Falhas que possuem um código podem usar `LOG_ERROR_CODE` ou `LOG_WARN_CODE`.
Consultas estruturadas usam `log_get_stats()` e `log_copy_recent()`. Strings
maiores que o registro são terminadas com segurança, marcadas por flags e
contabilizadas. Argumentos inválidos incrementam descartes sem fazer o próprio
logger entrar em recursão.

Registros consecutivos com o mesmo nível, módulo, código e mensagem são
agrupados. O ring atualiza `occurrences` e `last_tick`; o console preserva a
primeira ocorrência e só volta a imprimir resumos nas contagens 2, 4, 8, 16 e
assim por diante. O acesso ao ring salva EFLAGS, desabilita interrupções e
restaura o estado anterior sem spinlock; a escrita de vídeo acontece fora da
seção crítica.

`log_clear_buffer()` remove os registros e encerra o agrupamento corrente, mas
preserva níveis, sequência e contadores cumulativos, incrementando apenas o
contador de limpezas. `log_self_test()` usa um ring privado de quatro entradas
para validar ordem, wrap, agrupamento, truncamento, código opcional, limpeza,
serialização e filtragem sem alterar o histórico real.

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

## PWR1: Idle arquitetural e contabilidade PIT

O sistema unicore possui um único Idle, o processo PID 0. Ele fica fora do
round-robin e só é escolhido quando não há processo normal `READY`. Depois da
inicialização dos serviços, o kernel transfere o controle por um contexto de
bootstrap separado; o contexto e a stack preparados para o PID 0 não são
substituídos pelo contexto da stack de `kernel_main`.

O corpo do Idle executa `sti; hlt` e depois `process_yield()`. A instrução
`sti` imediatamente anterior ao `hlt` fecha a janela de corrida entre a
decisão de dormir e a chegada de uma IRQ. Timer, teclado, rede, IPC e
workqueue continuam usando seus mecanismos existentes para acordar processos.
O caminho degradado que mantém serviços no `kernel_main` também usa
`sti; hlt`; ele só é usado quando a criação do processo System falha.

`scheduler_stats_t` preserva seus campos existentes e acrescenta, ao final,
`idle_ticks` e `active_ticks`. O PIT incrementa exatamente um deles por tick e
incrementa `total_ticks` do processo corrente. `idle_ticks` deve permanecer
igual ao `total_ticks` do PID 0; `schedcheck` e `regcheck full` validam essa
invariante. `scheduler_get_stats()` copia os contadores com interrupções
protegidas e não registra logs no caminho de cada tick.

System e Desktop bloqueiam por um tick após o ciclo cooperativo quando não há
trabalho imediato, evitando loops ativos. `cpu usage` mostra a janela
acumulada desde o boot e `cpu usage reset` salva somente uma linha-base privada
do Shell. A porcentagem é uma estimativa de residência do scheduler baseada no
PIT de 50 Hz, não uma medição elétrica nem uma leitura RDTSC/PMU.

## Isolamento ring 3

O kernel possui segmentos de usuario em `0x1B` (codigo) e `0x23` (dados).
O processo de teste usa codigo em `0x00800000`, dados em `0x00801000`, pagina
de lancamento em `0x00802000` e stack em `0x00C00000`. Seu diretorio compartilha as tabelas supervisor do kernel,
mas as paginas do kernel permanecem com o bit `user` desativado.

O dispatcher de `int 0x80` valida `CS`, `SS`, o processo atual e todas as
faixas de memoria antes de copiar dados para as APIs internas. O comando
`usertest` exercita `console_write`, `uptime`, `memory_info` e `process_exit`.
`usertest fault` valida o encerramento controlado de uma page fault de usuario.

Na SYNC4, exceções ring3 são registradas como `SIGSEGV` e encerram somente o
processo afetado. Exceções ring0, falhas estruturais de paging e corrupção do
kernel continuam encaminhadas ao `panic`. Sinais capturados são preparados
depois dos handlers C e antes do `iret`, usando contexto salvo exclusivamente
no kernel e o trampoline reservado em `0x00802FF0`.

## Serviços de aplicativos

Depois de memória, paging, TSS e processos essenciais, o kernel inicializa a
App API e o dispatcher `int 0x80`. O gate começa restrito a DPL 0 e é elevado
para DPL 3 somente quando a fronteira de modo usuário está pronta. A plataforma
atual inclui arquivos, IPC, imagens `.ZAP`/`ZAPP`, foco de aplicativo externo
e uma página de lançamento com argumentos. A App API 0.9 oferece descritores
VFS por processo, `file_lseek` na syscall 14, `chdir/getcwd` nas syscalls
15-16, `file_ioctl` na syscall 17, dispositivos em `/dev` e as ações,
máscaras, geração e retorno de sinais introduzidas nas
syscalls 10-13. O Shell continua
nativo; `echo` é a primeira migração ring 3 e mantém fallback nativo.

Consulte [API de Aplicativos e Syscalls](../melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md)
para a ABI estável e [Roadmaps por Etapa](../roadmaps/README.md) para a ordem
das próximas migrações.

## Servicos S1.1: dispositivos e energia

O kernel inicializa PCI antes do AC97 e, depois dos drivers, cria os servicos
de dispositivos e energia. Suas consultas sao somente de leitura, falham de
forma controlada e aparecem no `health`; `power_shutdown_request()` e a entrada terminal do coordenador.

- `device_manager`: mantem um snapshot estatico de PCI, ATA, AC97, PS/2, PIT,
  VGA, VESA e PC Speaker. Nao reinicializa drivers, nao grava no disco e nao
  habilita ou desabilita hardware. Na EP2, cada disco presente aparece como
  `ata0` a `ata3`; `ata-primary` continua como alias do primeiro disco legado.
- `power`: informa as capacidades reais do sistema atual. S0 e idle HLT/C1
  estao disponiveis; S1-S4 permanecem indisponiveis. S5 fica disponivel
  somente quando o snapshot ACPI atende ao contrato seguro do PWR3; nos
  demais casos, `poweroff` retorna `ERR_UNAVAILABLE` e preserva o sistema.

- `usb_manager`: apos `pci_init()` cria o inventario dos controladores PCI de
  classe `0x0C`, subclasse `0x03`. ProgIF `0x00` e UHCI, `0x20` e EHCI e os
  demais ficam fora do escopo. O servico copia vendor/device, classe, BDF, IRQ
  e seis BARs do snapshot PCI, limita-se a oito entradas e expoe `usb status`,
  `usb list`, `usb device <id>`, `usb ports`, `usb devices`, `usb storage` e
  `usb hid`. A EP7.1B inicializa EHCI somente para USB high-speed e mantem
  UHCI como caminho dos drivers HID/MSC legados.
- `uhci`: inicializa somente controladores UHCI apos o inventario PCI. Valida
  BAR I/O e IRQ, habilita I/O e bus mastering apenas nesse ProgIF, aloca frame
  list de 1024 entradas, queue head, TDs e buffers DMA alinhados, registra IRQ
  compartilhada e executa controle USB com deadlines baseados em ticks. A
  enumeracao fica restrita a portas raiz, Device/Configuration, uma interface
  e `SET_CONFIGURATION`; nao ha hubs, hot-plug ou strings. A EP4.4 acrescenta
  somente endpoints Interrupt IN para HID Boot, sem parser generico de Report
  Descriptor. O inventario tambem preserva `bcdDevice` e uma tabela limitada
  de endpoints, sem remover os campos derivados de Bulk/Interrupt. EHCI usa
  contrato proprio em `drivers/ehci.h`; o transporte comum em
  `core/usb_transport.h` seleciona UHCI/EHCI sem alterar estes chamadores.
- `usb_msc`: apos a enumeracao UHCI, reconhece somente interfaces MSC BOT
  SCSI com exatamente um Bulk IN e um Bulk OUT. Executa INQUIRY, TEST UNIT
  READY, READ CAPACITY(10) e READ(10), publica LUN 0 como provedor somente-
  leitura de `block_device_t` e deixa a montagem FAT para `storage mount`.
- `block`: registra estaticamente provedores ATA e USB MSC, preserva os IDs ATA
  legados, valida callbacks/LBA/setor de 512 bytes e recusa escrita em
  provedores somente-leitura. Desde o BLK0, `block_submit_sync()` recebe BIOs,
  publica estados de requisicao e encaminha a operacao ao callback do
  dispositivo; `block_read()` e `block_write()` continuam como wrappers
  compativeis. O buffer pertence ao chamador e a callback de conclusao e
  executada uma vez no estado terminal. No BLK1, `block_submit()` publica a
  fila FIFO estatica de 32 entradas, a `Zephyr kworker` despacha fora do lock e
  a fusao e limitada a BIOs adjacentes com buffers contiguos. O cancelamento
  remove apenas entradas enfileiradas; `block_get_stats()` e `blkstat` expoem
  profundidade, pico, fusoes, taxas e contadores por dispositivo. O codigo
  canonico `ERR_CANCELLED` foi acrescentado ao final de `errors.h`, sem
  renumerar os erros existentes.
  No BLK2, `block_read()` usa o cache estatico de 64 blocos de 512 bytes com
  hash por dispositivo/LBA/tamanho e LRU por indices. Hits evitam a leitura
  fisica; misses carregam pela fila BLK1, agrupam setores contiguos quando
  possivel e fazem bypass quando o cache esta cheio sem vitima elegivel.
  No BLK3, `block_write()` grava logicamente no cache com faixa suja e
  `block_submit_sync()` continua sendo o caminho fisico direto. O writeback
  periodico usa uma work item de 250 ticks, com limite de 8 blocos por ciclo;
  `sync`/`fsync` drenam os blocos sujos e submetem FLUSH ATA quando o IDENTIFY
  publica suporte. A ausencia de FLUSH e durabilidade `DEGRADED`; erros
  preservam `DIRTY`. `block_cache_sync_device()` e
  `block_cache_sync_all()` sao as APIs de sincronizacao, e
  `block_cache_get_stats()` publica bytes sujos, tentativas, sucessos, falhas,
  syncs, flushes e estado de durabilidade. `sync` e `fsync` sao explicitos;
  close e saida de processo nao sincronizam automaticamente. Entradas
  `READING`, fixadas, sujas ou em writeback nao podem ser removidas;
  invalidacoes sao atomicas.
  No BLK4, failpoints privados e one-shot cobrem submissao, execucao,
  conclusao, FLUSH, eviction e writeback abortado. Os autotestes confirmam
  callback unico, fila drenada, hash/LRU e pins integros, dados sujos ainda
  legiveis e retry somente por nova sincronizacao. `blkcheck` coordena essas
  verificacoes com as fixtures FAT12/FAT32 controladas, sem expor uma API de
  injecao ao restante do kernel. Montagens FAT32 externas refletem a capacidade
  de escrita do provedor ATA; FAT12 e USB permanecem somente-leitura.
  `storage_refresh()` reconcilia esse registro depois de cada atualizacao USB
  sem duplicar discos ou volumes.

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

Na PWR2, o snapshot inclui a RSDP validada, a raiz RSDT/XSDT e as SDTs aceitas
na ordem publicada pela raiz. Cada item informa endereco, comprimento, revisao
e checksum valido. A MADT e copiada para um inventario limitado a 64 entradas;
as consultas devolvem somente `acpi_madt_info_t` e `acpi_madt_entry_t` por
valor, sem ponteiros para firmware. A contagem separa entradas Local APIC,
processadores habilitados e I/O APICs, enquanto tipos desconhecidos validos sao
preservados como bytes copiados.

Essa descoberta nao executa AML, nao escreve PM1 ou qualquer outro registrador
e nao habilita APIC/SMP. Ausencia de MADT deixa a capacidade de APIC
indisponivel; uma MADT malformada torna o inventario parcial. `acpi tables`
apresenta o inventario para diagnostico, e `regcheck full` valida limites,
checksums, ordem, duplicatas e contagens. As transicoes reais sao coordenadas
pelo PWR3.

O componente `ACPI` do `health` fica `READY` com raiz, FADT e DSDT validas,
`DEGRADED` quando existe apenas um inventario parcial e `DISABLED` quando nao
ha raiz utilizavel. `Power` publica `READY` quando S5 esta validado e
`DEGRADED` quando somente reboot ou idle permanecem disponiveis. Os campos
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
raiz ACPI valida, mas deixa o coordenador de energia em estado `DEGRADED`.

## Servico PWR3: desligamento e reinicializacao

`power_status_t` acrescenta `acpi_mode_enable_available` e
`acpi_s5_transition_ready`. Quando a transicao esta pronta, S5 e
`hardware_poweroff` passam a `POWER_CAPABILITY_AVAILABLE`; sem esse contrato,
S5 permanece simulado e o desligamento fisico permanece indisponivel.

`power_shutdown_request()` e a entrada canonica de desligamento; `poweroff` e
`shutdown` chegam a ela. A transacao executa admissao, notificacao reservada,
sync/flush limitado, quiescencia best effort de PC Speaker/AC97, commit de
hardware e estado terminal. `storage_sync_all_until()` aplica o orcamento de
1500 ticks entre operacoes de writeback/flush. Falhas antes do commit retornam
o codigo canonico, liberam a transacao e mantem o sistema ativo. Nao existe
fallback silencioso para `CLI+HLT` quando S5 nao esta validado.

`acpi_poweroff()` repete a validacao do snapshot antes de qualquer escrita,
preserva `acpi_enter_s5()` como wrapper e, depois do primeiro comando de
hardware, permanece em estado terminal mesmo que o firmware nao desligue.
O snapshot `acpi_power_info_t` inclui `RESET_REG`, seu valor e sua validade;
somente GAS System I/O de 8 bits compativel com i386 pode ser usado.

`system_reboot()` e a entrada canonica de reboot, com `power_reboot()` como
wrapper. A ordem de metodos e RESET_REG ACPI, operacao do driver PS/2 com
polling limitado e, por ultimo, triple fault com IDT nula. Um metodo que
retorna inesperadamente e registrado antes da tentativa seguinte. O triple
fault nao usa portas privadas de emuladores.

O coordenador publica `service_state`, `transaction_phase`, `last_error` e as
capacidades de reset no `power_status_t`. O PWR3 nao desmonta volumes, envia
notificacoes completas, encerra processos ou implementa `shutdown -h now` e
`shutdown -r now`; essas responsabilidades continuam reservadas ao PWR4.

## Contrato PWR0: coordenacao de energia

O PWR0 congela o contrato de coordenacao que o PWR3 agora implementa sem
alterar a App API, syscalls, layouts binarios ou bootloader. O estado do
servico e separado dos
estados ACPI S0-S5:

- `UNKNOWN`: o servico ainda nao publicou um diagnostico;
- `DISCOVERING`: dependencias e capacidades estao sendo observadas;
- `READY`: o coordenador esta utilizavel e conhece seus fallbacks seguros;
- `DEGRADED`: o coordenador funciona, mas alguma capacidade depende de
  fallback ou esta indisponivel;
- `UNAVAILABLE`: nenhuma operacao segura pode ser admitida.

Cada capacidade publica individualmente `available`, `simulated` ou
`unavailable`, com pre-condicao, fallback e erro canonico. `READY` nao promete
desligamento fisico: a ausencia de ACPI valido, `_S5_`, RESET_REG ou outro
metodo confirmado permanece explicita no diagnostico.

`shutdown` e `reboot` usam uma transacao comum, cujo alvo e fixado na
admissao:

1. `admission`: valida estado, capacidade, ausencia de outra transacao e
   pre-condicoes;
2. `notification`: notifica aplicativos, processos e drivers participantes;
3. `sync/flush`: conclui o writeback e publica qualquer falha de durabilidade;
4. `quiescence`: para novas atividades de filesystem, workqueues, processos,
   drivers e CPU conforme os contratos de cada participante;
5. `hardware commit`: executa somente o metodo detectado e validado;
6. `terminal`: confirma a transicao; no reboot, triple fault e o ultimo
   metodo arquitetural. Poweroff sem S5 validado e recusado antes do commit.

Os orcamentos sao expressos em ticks PIT de 50 Hz e nao podem ser emprestados
entre fases: notificacao, 250 ticks (5 s); sync/flush, 1500 ticks (30 s);
quiescencia, 250 ticks (5 s); commit de hardware, 100 ticks (2 s). O limite
total e 2100 ticks (42 s). Timeout antes do commit aborta a transacao e
preserva o estado operacional; depois da primeira escrita ou comando de
hardware nao existe cancelamento nem retorno a um estado ativo.

O coordenador possui a transacao, o cursor de fase, os prazos, o cancelamento
e o resultado. Cada participante possui seus proprios recursos e deve tornar
preparacao, quiescencia e liberacao idempotentes. Nenhum participante pode
reter ponteiros de outro subsistema depois do callback. Falhas sao propagadas
com `ERR_STATE`, `ERR_UNAVAILABLE`, `ERR_TIMEOUT`, `ERR_CANCELLED`,
`ERR_AGAIN` ou `ERR_INVALID`, conforme a causa, e a camada com contexto final
registra a fase, o erro e o fallback sem gerar ruido repetitivo.

O PWR0 separa descoberta/validacao ACPI, interpretacao AML e execucao de
metodos. O driver ACPI fornece snapshots validados; um parser de cabecalhos
nao e um interpretador AML e nenhuma porta privada de QEMU, Bochs ou
VirtualBox pode ser usada como fallback generico. O PWR3 implementa a
transacao, S5 e os metodos de reboot; notificacoes completas, desmontagem e
encerramento ordenado de processos permanecem no PWR4.

## Servicos S2.1-S2.8: Multi-NIC, Ethernet e pilha TCP/IP

`network_manager` filtra por copia o snapshot PCI e mantem ate quatro
controladores de classe `0x02`. O servico copia identificadores, localizacao,
IRQ e BAR0-BAR5; os IDs `net-pci-BB:DD.F` permanecem estaveis e consultas
tambem aceitam `net-pci-BB-DD.F`.

`network_manager_status_t` separa inventario, modelos reconhecidos, drivers
ativos, erros de driver e o ID da interface L3. `network_interface_info_t`
preserva os metadados PCI e informa modelo, estado do driver, link, MAC,
contadores, fila RX, ultimo erro, vinculo Ethernet, papel L3 e aquisicao DHCP.
`network_manager_refresh()` continua sendo apenas uma nova varredura PCI: nao
reseta, realoca DMA nem reinicializa E1000, RTL8139 ou a camada Ethernet.

O componente `Network` do `health` segue estas regras:

- `READY`: existe pelo menos uma interface com driver ativo e as camadas
  Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, sockets e HTTP foram
  inicializados;
- `DEGRADED`: controlador reconhecido com falha, controlador sem driver ou
  inventario parcial, mesmo quando outra NIC continua operacional;
- `DISABLED`: nenhum controlador detectado ou snapshot indisponivel.

Desde a S2.8, o kernel nao inicializa NICs diretamente. Depois do PCI,
`network_manager_init()` cria o registro Ethernet, percorre todos os
controladores reconhecidos e inicializa cada E1000 `8086:100E` ou RTL8139
`10EC:8139` pelo BDF exato. Falha de uma instancia fica registrada e nao
impede o probe nem o polling das demais. A sincronizacao com recovery e
idempotente para que `device-scan` repetido sem mudanca nao aumente o contador
de falhas.

### EP7.0: inventario PCI de candidatos Wi-Fi

`wifi_manager` percorre o snapshot PCI somente depois de `pci_init()` e
considera candidatos os controladores de classe `0x02` que nao sejam os
modelos Ethernet ja suportados (`8086:100E` e `10EC:8139`). O servico copia
Vendor ID, Device ID, classe, subclasse, ProgIF, revisao, BDF, IRQ e BAR0-BAR5
para um inventario estatico de ate oito entradas. Os IDs publicos usam o
formato `wifi-BB:DD.F`.

Esta etapa nao habilita BARs ou Bus Mastering, nao mapeia MMIO, nao aloca DMA,
nao registra IRQ e nao acessa firmware. Os candidatos ficam em
`UNSUPPORTED`, com `ERR_UNAVAILABLE`; a ausencia de candidatos e um estado
normal com `ERR_NOT_FOUND`. `wifi status` e `wifi scan` sao diagnosticos, e
`wifi connect` falha de forma controlada sem ler, armazenar ou registrar
credenciais. A futura interface Wi-Fi devera entregar frames 802.3 por
`ethernet_interface_t` antes de ser anexada ao `network_manager`.

Na EP7.1A, o mesmo snapshot tambem consulta os dispositivos configurados pelo
`usb_manager` e reconhece somente `0x0BDA:0xC811` com `bcdDevice 0x0200`. A
entrada preserva o ID USB, controlador, porta, endereco, revisao e contagem de
endpoints. `rtl8811cu_probe()` e somente-leitura; `rtl8811cu_init()` verifica
`RTL8811.BIN` no filesystem, mas permanece em `ERR_UNAVAILABLE` enquanto a
sequencia de radio nao tiver referencia tecnica verificavel. Portanto, a
EP7.1A nao anexa interface ao `network_manager`, nao executa TX/RX e nao faz
scan ou associacao.

Na EP7.1B, o EHCI high-speed e inicializado de forma isolada, com transporte
comum para controle, Bulk e Interrupt, mantendo HID/MSC legados no UHCI. O
`network_manager` reconhece o candidato USB e publica
`net-usb-BB:DD.F-pN`, mas somente anexa `ethernet_interface_t` depois de um
driver `READY`. O backend RTL8811CU valida o arquivo externo
`RTL8811.BIN`, porem mantem o radio desligado enquanto checksum, firmware e
sequencia de registradores nao forem confirmados.

`ethernet_interface_t` desacopla a camada L2 dos drivers por contexto opaco e
callbacks de status, servico de causas pendentes, RX pendente, recepcao e
transmissao. Um registro fixo aceita quatro interfaces e o polling usa
round-robin com orcamento global de oito frames. A IRQ somente reconhece o
dispositivo, acumula a causa e agenda o Bottom-Half. A `Zephyr kworker` drena
o trabalho com interrupcoes habilitadas; `service_pending` repete esse servico
no polling como fallback antes de consultar RX.
Nesse contexto o driver copia frames validos para uma fila estatica de oito
entradas, recicla o DMA e a camada Ethernet valida tamanho, destino, origem e
EtherType. Broadcast e unicast para a MAC local sao aceitos; outros destinos,
cabecalhos invalidos e saturacao da fila possuem contadores separados.

`ethernet_send(interface_id, ...)` direciona explicitamente a transmissao,
monta cabecalho, origem local e padding minimo antes de usar o callback do
driver. Uma tabela fixa de quatro handlers entrega uma visao sincrona com o
ID da interface e o EtherType; o ponteiro de payload so e valido durante o
callback. `network_manager_get_ethernet_diagnostic()` devolve por copia os
contadores agregados e os contadores, fila e ultimo frame da NIC solicitada.

Na S2.4, `arp_init()` registra o EtherType `0x0806` somente depois da camada
Ethernet. O modulo serializa os 28 bytes do pacote explicitamente em ordem de
rede, sem structs packed. A configuracao `arp_configure()` vincula uma unica
interface e um IPv4 local somente em RAM; o valor publico usa a forma canonica
`(A<<24)|(B<<16)|(C<<8)|D`. Repetir a mesma configuracao e idempotente;
trocar interface ou IP limpa cache, pendencias e contadores.

O cache ARP possui 32 entradas `INCOMPLETE`, `RESOLVED` ou `FAILED`.
`arp_resolve()` nunca bloqueia: um miss envia request imediato, repete apos
um e dois segundos e marca timeout no terceiro segundo. Entradas resolvidas
ou falhas expiram apos 30 segundos. A substituicao preserva pendencias e usa
entrada vazia/expirada ou a resolvida/falha mais antiga.

O aprendizado e restrito ao remetente de request dirigido ao IPv4 local e a
reply local que corresponda a uma resolucao pendente. Requests validos para o
host recebem reply automatico fora da IRQ. `arp_validate_state()` e as
consultas de status/cache sao somente-leitura; o `regcheck full` nao transmite
nem altera configuracao.

O status ARP contabiliza cache hits e ciclos de manutencao para que os testes
assincronos sejam observaveis sem inferencia pelos contadores do driver. Erro
transitorio ao transmitir um retry fica registrado e logado, mas nao desativa
o polling de rede; tentativas posteriores e o timeout continuam ativos.

Na S2.5, `ipv4_init()` registra o EtherType `0x0800` depois do ARP e
`icmp_init()` registra o protocolo IPv4 `1`. As duas tabelas de despacho
possuem quatro entradas fixas e entregam visoes sincronas: os ponteiros para
payload so sao validos durante o callback. Toda serializacao e leitura de
campos multibyte usa ordem de rede explicitamente, sem structs packed.

`network_manager_configure_ipv4()` coordena uma unica interface, IPv4, mascara
e gateway somente em RAM. A mascara deve ser contigua entre `/1` e `/30`; o
gateway zero remove a rota padrao e um gateway presente deve pertencer a mesma
sub-rede. Configuracao identica e idempotente. Qualquer mudanca limpa o cache
ARP e cancela ICMP; trocar interface ou IP tambem reinicia os contadores ARP.
Um `net arp config` incompatível invalida primeiro IPv4 e ICMP.

O transmissor IPv4 usa cabecalho fixo de 20 bytes, MTU 1500, TTL 64, flag DF e
identificacao incremental. `ipv4_send()` informa por `out_sent` se o frame foi
enviado ou se ainda aguarda ARP. Destinos locais usam rota direta; destinos
externos usam o gateway. Recepcao aceita somente unicast para o IPv4 local e
descarta, com contadores separados, checksum incorreto, opcoes, fragmentos,
TTL zero, comprimentos invalidos e protocolos sem handler.

O ICMP implementa Echo Request/Reply com checksum sobre a mensagem inteira,
inclusive payload impar. O host responde automaticamente a requests unicast
validos e conserva um unico reply pendente enquanto aguarda ARP. O ping usa uma
sessao fixa sem alocacao, identificador nao zero, sequencias a partir de um,
payload deterministico de 32 bytes e timeout de um segundo depois de cada
request efetivamente transmitido. A espera por ARP nao consome esse timeout.
Desde a R2, esse prazo usa um timer one-shot pertencente ao ICMP. Reply, reset,
falha ou mudança de configuração cancelam tanto timers armados quanto
vencimentos pendentes; a comparação manual de ticks não faz mais parte da
manutenção ICMP.

Um trabalho `NORMAL` da kworker executa Ethernet e as manutencoes de ARP e
ICMP. O processo System conserva polling USB, entrega de entrada e UI. Os
protocolos permanecem limitados a uma manutencao por tick. O Shell nao usa
mais um polling fixo de um tick: o executor do trabalho sinaliza o canal IPC
do Shell quando ha progresso, e o job recalcula o timeout ate o deadline. Os
campos
`event_generation` de DNS, ICMP e HTTP continuam identificando eventos
observaveis; a geracao do Shell rejeita resultados de outra execucao.
`ipv4_validate_state()` e
`icmp_validate_state()` incluem vetores puros de checksum e nao configuram,
transmitem, limpam cache nem avancam sessoes.
Expiracoes de timer agendam um trabalho `HIGH`, que despacha ate oito
callbacks fora da IRQ e se reagenda ao esgotar o orcamento. A manutencao de
rede continua podendo cancelar um timeout `PENDING` antes de seu callback.

Na S2.6, `udp_init()` registra o protocolo IPv4 `17`. Uma tabela fixa de 16
endpoints entrega `udp_datagram_view_t` apenas durante o callback. O modulo
valida portas, tamanho e pseudo-checksum IPv4, aceita checksum RX zero e
sempre gera checksum TX. O envio normal continua assincrono em relacao ao ARP;
o caminho separado de broadcast limitado e reservado ao bootstrap DHCP.

O IPv4 aceita `255.255.255.255` somente em frame Ethernet broadcast e somente
para UDP. `ipv4_send_limited_broadcast(interface_id, ...)` aceita origem zero
durante a aquisicao DHCP ou o endereco local durante rebinding, sem criar
entrada ARP.
Unicast, ICMP e filtros da S2.5 permanecem inalterados; o roteamento agora
consulta a tabela limitada da NET4.

Na NET4, `route.h` mantem em RAM uma tabela de ate 16 rotas IPv4. Cada entrada
usa rede canonica, mascara contigua `/0` a `/32`, gateway unicast opcional e
ID de interface; o lookup escolhe o maior prefixo correspondente. A rota
direta usa gateway zero, `route_reset()` restaura as rotas da configuracao
IPv4 atual e `route_validate_state()` verifica contagem, duplicidade e campos.
`ipv4_send()` consulta essa tabela antes do ARP, mas rejeita rotas para uma
interface diferente da L3 ativa porque o forwarding multi-NIC permanece fora
da etapa.

`TCP_STATE_LISTEN` foi anexado ao final do enum TCP para preservar a ABI
numerica; `tcp_state_name()` o torna legivel, embora o TCP passivo ainda
retorne `ERR_UNAVAILABLE`. O `netstat` agrega a tabela de conexoes TCP, os
sockets `AF_UNIX` da camada generica e os contadores RX/TX, erros e descartes
publicados por `network_manager`.

O cliente DHCP usa UDP 68/67 e os estados `SELECTING`, `REQUESTING`, `BOUND`,
`RENEWING` e `REBINDING`, alem dos estados terminais e de aplicacao. Discover
e Request iniciais usam broadcast e repetem em 1, 2 e 4 segundos. ACKs
validados geram um evento de lease; somente o Network Manager aplica
ARP/IPv4/ICMP/DNS e confirma o estado `BOUND`. T1 e T2 usam as opcoes do
servidor ou 50%/87,5% do lease. NAK, expiracao e release removem apenas uma
configuracao pertencente ao DHCP. Uma configuracao IPv4 existente permanece
ativa durante uma aquisicao sem resposta, inclusive quando a negociacao ocorre
em outra NIC. Somente um ACK valido aplica atomicamente a nova interface e
encerra clientes HTTP, sockets e conexoes TCP anteriores.

Depois de inicializar o Network Manager, o kernel inicia uma aquisicao DHCP
na primeira interface ativa, vinculada ao Ethernet e com link, seguindo a
ordem PCI. A chamada envia o primeiro Discover e retorna imediatamente: o
boot nao espera o lease, e as retentativas continuam no trabalho periodico de
rede. Ausencia de NIC, link ou servidor DHCP permanece somente como
diagnostico e nao impede Desktop ou Shell. A tentativa automatica ocorre uma
vez por boot; `net dhcp acquire <id>` continua disponivel para nova tentativa
ou para selecionar outra interface.

O cliente DNS usa uma porta efemera e uma consulta A/IN ativa. O parser
limitado a 512 bytes valida pergunta, resposta, limites e nomes comprimidos,
detecta ciclos e segue ate quatro CNAMEs. Tres tentativas usam timeout de um
segundo somente depois do envio UDP; espera por ARP nao consome esse prazo.
O cache possui 16 entradas, respeita TTL, ignora TTL zero e substitui a
entrada expirada ou mais antiga. Servidor manual e configuracao entregue por
DHCP permanecem somente em RAM.

O trabalho de rede executa Ethernet e, no maximo uma vez por tick, ARP, ICMP,
DHCP e DNS. UDP e os callbacks de DHCP/DNS rodam nesse contexto, nunca na IRQ.
`udp_validate_state()`, `dhcp_validate_state()` e `dns_validate_state()`
incluem vetores puros de checksum, opcoes, truncamento, compressao e CNAME.

Na S2.7, `tcp_init()` registra o protocolo IPv4 `6` e mantem ate 16 conexoes
clientes identificadas por handles geracionais. O TCP implementa abertura
ativa, sequenciamento, ACK imediato, FIN/RST, MSS local de 536 bytes, janela
RX de 4096 bytes e um unico segmento nao confirmado por conexao. Segmentos
duplicados ou fora de ordem sao descartados; opcoes bem formadas sao
percorridas e a opcao MSS e aplicada.

O RTO inicia em um segundo, usa SRTT/RTTVAR, backoff exponencial, regra de
Karn e no maximo tres retransmissoes. O temporizador so comeca depois que o
IPv4 realmente transmitiu o segmento, portanto espera por ARP nao consome o
prazo TCP. Conexoes inativas e `TIME_WAIT` expiram em 30 segundos. Nao existe
`LISTEN`, abertura passiva ou remontagem de segmentos fora de ordem.

`net_socket` oferece 16 sockets `STREAM` para kernel, Shell e servicos
nativos. Cada entrada possui fila TX de 2048 bytes e ring RX de 4096 bytes;
`send` e `receive` apenas transferem a quantidade que cabe e retornam
imediatamente. A camada ajusta a janela TCP pelo espaco do ring e drena TX em
segmentos limitados pelo MSS. A API nao e exposta como syscall de userspace.

Desde a SYNC2, cada slot ativo tambem possui uma wait queue FIFO. A API
`net_socket_wait()` bloqueia a tarefa ate `CONNECTED`, `READABLE`, `EOF`,
`ERROR`, `CLOSED` ou timeout, sem mudar a semantica nao bloqueante de
`net_socket_receive()`. Dados acordam um consumidor; transicoes terminais
acordam todos. Fechamento, abort e reset tornam a fila indisponivel e removem
os waiters antes de reciclar o handle geracional. A kworker executa o polling
TCP/sockets e nunca bloqueia nessas filas.

O cliente HTTP mantem uma sessao GET. Ele aceita somente
`http://host[:porta]/caminho`, resolve nomes pelo DNS, envia HTTP/1.1 com
`Host`, `User-Agent`, `Accept` e `Connection: close`, e armazena ate 8192
bytes de headers. `http_get_start()` preserva o corpo bufferizado de ate 16
KiB. Desde a U5, `http_get_stream_start()` entrega o corpo incrementalmente a
um callback e respeita o limite definido pelo chamador, de ate 128 KiB no
transporte ZUPD. O framing aceito usa `Content-Length`, `chunked` sem trailers
ou EOF; `Content-Length` exige tamanho exato. Outras codificacoes de
transferencia, headers conflitantes, mensagens malformadas e excesso de corpo
falham de forma controlada. HTTPS,
redirects, POST e compressao permanecem fora do contrato.

O polling da S2.7 acrescenta manutencao TCP, sockets e HTTP, nessa ordem,
depois de DNS. Mudanca efetiva de IPv4, lease removido/expirado ou ARP
incompativel aborta HTTP e sockets antes de remover a configuracao. Boot,
renovacao DHCP sem mudanca e configuracao identica nao criam trafego nem
interrompem conexoes.

Na S2.8, `ethernet_frame_view_t`, `ipv4_packet_view_t` e
`udp_datagram_view_t` carregam `interface_id`. ARP e IPv4 unicast aceitam
somente a interface L3 configurada; DHCP filtra broadcasts pela interface em
aquisicao. Continua existindo uma unica configuracao ARP/IPv4/DHCP/TCP ativa,
selecionada pelo ID estavel, embora ate quatro NICs possam operar em L2.
`ethernet_validate_state()` e `regcheck full` verificam IDs unicos, somas de
contadores, vinculos com o inventario, uma unica interface L3 e limites das
IRQs compartilhadas.

Persistencia, sockets de usuario, servidor TCP, IPv6, TLS, mDNS, DNS
TCP/EDNS/DNSSEC, DHCPDECLINE, loopback, forwarding, erros ICMP,
fragmentacao/remontagem, multiplas rotas/IPs simultaneos, hot-plug, shutdown
de NIC, VLAN, multicast e modo RTL8139 C+ permanecem fora do contrato.

Referencias da implementacao: [RFC 791](https://www.rfc-editor.org/rfc/rfc791.html)
para cabecalho/checksum IPv4, [RFC 792](https://www.rfc-editor.org/rfc/rfc792.html)
para ICMP Echo e [RFC 1122](https://www.rfc-editor.org/rfc/rfc1122.html) para o
comportamento de host.

S2.6 segue [RFC 768](https://www.rfc-editor.org/rfc/rfc768.html) para UDP,
[RFC 2131](https://www.rfc-editor.org/rfc/rfc2131.html) para DHCP e
[RFC 1035](https://www.rfc-editor.org/rfc/rfc1035.html) para DNS.

S2.7 segue [RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html) para TCP,
[RFC 6298](https://www.rfc-editor.org/rfc/rfc6298.html) para RTO e
[RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html) para framing HTTP/1.1.

### NET0: contrato de ownership e lifetime de buffers

O runtime de buffers de rede e definido por `src/include/core/net_buffer.h` e
implementado em `src/core/net_buffer.c`. Cada descriptor possui estado,
owner (`DRIVER`, `ETHERNET`, `PROTOCOL` ou `SOCKET`), referencia, capacidade,
headroom, length, tailroom, alinhamento e erro de conclusao. As transicoes
permitidas sao:

`ALLOCATED -> RX | QUEUED | IN_FLIGHT | DROPPED`,
`RX -> QUEUED | IN_FLIGHT | DELIVERED | DROPPED`,
`QUEUED -> IN_FLIGHT | DROPPED`, `IN_FLIGHT -> DELIVERED | DROPPED` e
`DELIVERED | DROPPED -> FREED`. `FREED` e terminal e a ultima referencia
somente pode ser liberada apos conclusao ou descarte.

O rastreamento e estatico e limitado a 32 descriptors; nenhum ponteiro de
payload externo e retido. No NET1, a Ethernet integra o descriptor ao
`sk_buff_t`, que mantem um `net_buffer_t` privado como fonte unica de estado,
owner, referencias e inventario. Os objetos sao obtidos do SLAB, com storage
interno de no maximo 2048 bytes e sem `kmalloc` por pacote. RX e TX usam
buffers emprestados e callbacks sincronos: os dados recebidos e enviados
somente sao validos durante o callback. Nenhum driver transfere ownership de
DMA; a copia fallback e contabilizada. Clones, fragmentos reais, zero-copy,
sockets genericos e `poll/select` permanecem fora do NET1.

`ethernet_validate_state()` inclui a validacao global dos buffers. O
`net_buffer_self_test()` usa fixtures privadas e restaura suas metricas;
`regcheck full` e `net check` o executam, enquanto `health check` denuncia
buffers ativos, invariantes quebradas e erros residuais. `boot.asm` nao faz
parte desta etapa.

O NET1 acrescenta `skb_self_test()` ao mesmo fluxo de validacao. `skbstat`
expoe alocacoes, liberacoes, conclusoes, descartes, copias, clones e
fragmentos reservados. O caminho continua sincrono e baseado em copia, sem
transferencia de ownership de DMA ou garantia de zero-copy real. `boot.asm`
nao foi alterado.

### NET2: camada generica de sockets e AF_UNIX

`src/include/core/socket.h` acrescenta sockets genericos sobre a VFS, sem
novo syscall ou alteracao da ABI ring 3. `socket_create()` cria um descritor
VFS real do tipo `VFS_NODE_SOCKET`; o fechamento usa o caminho normal da VFS e
o encerramento do processo continua passando por `vfs_fd_table_release()`.
`lseek` e `fsync` retornam `ERR_UNAVAILABLE` para sockets.

`AF_UNIX/SOCK_STREAM` usa um listener por caminho, backlog limitado e pares
cliente/servidor com `bind`, `listen`, `connect` e `accept`. Cada direcao tem
fila limitada por buffers e bytes. O payload e copiado para `sk_buff_t` de
ate 2048 bytes, com fragmentacao em buffers independentes quando necessario;
leitura parcial preserva o buffer na fila. A sequencia de ownership e
`ALLOCATED -> QUEUED -> DELIVERED -> FREED`, com descarte para fila cheia ou
fechamento.

`AF_INET/SOCK_STREAM` adapta o cliente TCP ativo de `net_socket`; `bind`,
`listen` e `accept` passivos IPv4 permanecem indisponiveis. UDP, TCP passivo,
`poll/select`, `socketpair`, datagramas UNIX, clones, fragmentos
compartilhados, zero-copy e transferencia de ownership DMA continuam fora da
NET2. O caminho permanece sincrono e baseado em copia, e ponteiros de drivers
continuam validos somente durante seus callbacks.

Sockets bloqueiam por padrao e liberam locks antes de esperar. O modo
`SOCKET_FLAG_NONBLOCK`, ou `socket_set_nonblocking()`, retorna `ERR_AGAIN`
quando nao ha progresso; cancelamento de espera retorna `ERR_CANCELLED`.
`socket_validate_state()` verifica o vinculo entre objetos privados, FDs,
filas, peers e o lifetime SKB. `socket_self_test()` e executado por `net
check`, `regcheck full` e `health check`; `sockstat` lista os FDs genericos,
proprietarios, estados, caminhos/destinos, filas, flags e erros. `boot.asm`
nao foi alterado.

## U2: criptografia e verificacao local ZUPD

`src/include/core/version.h` centraliza a versao `0.1.0`, epoch `0` e o texto
de exibicao usado pelo banner do kernel e pelo Settings. Esses valores tambem
definem o baseline quando a persistencia U3 ainda nao existe.

`src/include/core/crypto.h` oferece SHA-256 incremental, SHA-512 e verificacao
Ed25519 incremental. `crypto_self_test()` valida SHA-2 e os vetores 1 e 2 do
RFC 8032 antes de habilitar Update. A equacao Ed25519 e o SHA-512 usam um
subconjunto verify-only adaptado do Monocypher 4.0.3, com encodings de ponto e
assinaturas nao canonicos recusados. A versao 4.0.3 foi fixada para incorporar
a correcao upstream de vazamento temporal na verificacao Ed25519.

`src/include/core/update.h` expoe os motivos publicos `0` a `11`, metadados
autenticados, capacidades e:

- `update_init()`;
- `update_is_ready()`;
- `update_verify_file()`;
- `update_get_capabilities()`;
- `zupd_reason_name()`.

O parser le o arquivo por `fs_read_file_range_at()`, decodifica little-endian
sem casts de structs empacotadas e usa buffers estaticos. A ordem de
validacao e estrutura, hashes, chave/assinatura e aplicabilidade. A trava do
modulo impede duas verificacoes simultaneas. Nenhum caminho de U2 chama APIs
de escrita.

`src/include/core/update_trust.h` e gerado da raiz publica versionada. O
kernel confirma no boot que o `key_id` e o prefixo de 16 bytes do SHA-256 da
chave publica e desabilita o componente se a raiz ou qualquer autoteste
falhar.

`RECOVERY_COMPONENT_UPDATE` foi anexado ao fim da enumeracao para preservar os
IDs anteriores. Ele fica `READY` quando chave, criptografia e leitura local
estao disponiveis, `DEGRADED` quando o filesystem falta e `DISABLED` em falha
criptografica.

O formato e a politica completos estao em
[`contrato-zupd-v1.md`](../14-atualizacoes/contrato-zupd-v1.md).

## U3: estado instalado e recuperacao no boot

Em FAT12, `update_init()` e executado imediatamente depois de `fs_init()` e
antes de `icons_init()`. Essa ordem permite selecionar as copias redundantes de
estado/journal e recuperar uma transacao interrompida antes de qualquer BMP ser
carregado.

O workspace permanece estatico e reutiliza um buffer de 64 KiB para copiar um
arquivo por vez. Nao existe alocacao de um pacote completo de 128 KiB. A mesma
trava do modulo recusa verificacao, aplicacao ou rollback concorrente.

O servico acrescenta:

- `update_get_installed_version()` para consultar a versao de conteudo;
- `update_apply_file()` com modo dry-run ou confirmacao;
- `update_rollback()` com modo dry-run ou confirmacao;
- callback cooperativo de cancelamento entre etapas;
- failpoint one-shot para interromper a aplicacao apos um alvo;
- resultado com progresso, motivo, reboot e recuperacao pendente.

Uma aplicacao reexecuta integralmente o verificador U2, valida os hashes
persistidos dos arquivos atuais, reserva staging/backup/copy-on-write e so
entao cria o journal. O ponto de commit e o journal `COMMITTED`; a versao
instalada muda somente depois dele.

No boot, aplicacao anterior ao commit e restaurada, rollback interrompido
continua e commit pendente e finalizado. Falha irrecuperavel preserva o
journal, bloqueia novas escritas e marca `RECOVERY_COMPONENT_UPDATE` como
`DEGRADED`.

As capacidades de `health` sao independentes: verificacao local continua
`READY`; aplicacao exige FAT12 e estado integro; rollback exige uma geracao
valida; remoto continua `DISABLED (U5)`.

## U4: diagnosticos e historico do Update

O servico Update carrega `ZUPD0.HIS` e `ZUPD1.HIS` depois dos registros de
estado e journal e antes de recuperar uma transacao pendente. O historico e um
ring buffer redundante de oito eventos `ZUH1`; o layout canonico permanece em
[`contrato-zupd-v1.md`](../14-atualizacoes/contrato-zupd-v1.md).

As novas consultas publicas sao:

- `update_get_status()`, que agrega versoes de build, instalada e de rollback,
  epochs, integridade, journal, capacidades e ultimo evento;
- `update_get_history_count()`;
- `update_get_history_entry()`, com indice zero para o evento mais recente;
- conversores estaveis de estado, operacao e resultado para texto.

Essas consultas usam a trava existente e nunca gravam. Aplicacao e rollback
confirmados acrescentam eventos depois de sair da regiao transacional critica.
Quando ha journal pendente, a escrita e adiada ate o boot. A recuperacao
concluida registra a falha interrompida e um evento `RECOVERED`. Falha isolada
do historico nao desfaz um estado U3 ja comprometido.

`RECOVERY_COMPONENT_SYSTEM_UPDATER` foi anexado ao final da enumeracao de
Recovery. Ele fica `READY` quando interface, worker cooperativo e servico local
estao disponiveis, `DEGRADED` quando filesystem, estado ou historico permitem
apenas operacao parcial, e `DISABLED` quando Update nao foi inicializado ou o
worker nao pode ser criado. O componente
`RECOVERY_COMPONENT_UPDATE` continua representando o servico transacional.

O kernel inicializa o System Updater depois do Window Manager. A abertura pelo
menu Iniciar usa `IPC_APP_OPEN_UPDATER`, tambem anexado ao fim da enumeracao
IPC para preservar todos os valores anteriores. Em Classic o aplicativo e
hospedado pelo WM; se isso nao for possivel, abre automaticamente em Classic.

Depois de inicializar o catalogo e o servico `PKG`, o kernel inicializa a
interface da App Store. `IPC_APP_OPEN_APP_STORE` tambem e append-only; o
Classic hospeda sua janela singleton e o fallback Simple mantem uma TUI
funcional quando a hospedagem nao estiver disponivel.

## AS1: catalogo local da App Store

Depois de inicializar o loader ZAPP e o servico `PKG`, o kernel inicializa
`app_catalog_init()`. O snapshot combina ate 16 fontes `.ZPK` da raiz e os
registros validos em `APPS/`, mantendo ate 32 entradas em memoria estatica.
Fontes sao verificadas pelo parser ZPKG existente e nunca sao mantidas
integralmente no catalogo.

`RECOVERY_COMPONENT_APP_STORE` foi anexado depois do System Updater para
preservar os IDs anteriores. Ele fica `READY` para um snapshot completo,
`DEGRADED` quando ha fonte invalida, leitura parcial ou limite excedido, e
`DISABLED` quando filesystem, loader ou servico `PKG` nao estao disponiveis.
O `health` completo e o resumo exibem esse componente; fontes invalidas nao
impedem consultas das entradas validas.

Na EP2, `RECOVERY_COMPONENT_STORAGE` e anexado ao fim do enum. Ele fica
`READY` quando o inventario ATA/volumes foi criado, `DEGRADED` quando existe
disco mas a descoberta geral terminou parcialmente e `DISABLED` quando ATA ou
o inventario Storage nao estao disponiveis. Uma particao MBR/BPB invalida fica
registrada somente em seu volume e nao degrada os demais. O kernel inicializa
Storage depois do filesystem legado e antes dos consumidores de interface.

Na EP4.1, `RECOVERY_COMPONENT_USB` foi anexado depois de Storage para
preservar os valores anteriores. Na EP4.2, UHCI funcional deixa USB `READY`
mesmo sem dispositivos; uma porta com erro, falha de inicializacao UHCI,
inventario parcial ou somente EHCI deixa USB `DEGRADED`. Sem PCI ou sem
controlador USB, USB fica `DISABLED`. `device-scan` atualiza o inventario de
forma idempotente e o polling UHCI roda nos loops normal e fallback do kernel.

Na EP4.3, somente um MSC BOT validado conta como driver de classe ativo; falhas
SCSI/BOT marcam o registro MSC como `DEGRADED`, preservando o controlador UHCI
e os demais dispositivos. Na EP4.4, interfaces HID Boot de teclado e mouse
publicam eventos no `input core` comum por transferencias Interrupt IN
persistentes. `usb hid status` exibe relatorios, erros, timeouts, descartes e
cancelamentos; `usb hid check` valida o driver HID, as filas de entrada e a
fila de conclusoes diferidas. Hubs, hot-plug real e HID generico continuam
fora do escopo.

O contrato detalhado esta em
[`app-store.md`](../13-aplicativos/app-store.md).

## U5: distribuicao remota opcional

`src/include/core/update_remote.h` separa transporte remoto do servico
transacional. O kernel inicializa o modulo depois de filesystem, Update e
Network Manager. A inicializacao recupera somente o cache local, deixa a
sessao `DISABLED` e nunca abre conexao nem configura rede.

O manifesto fixo `ZUM1` e autenticado com a mesma raiz Ed25519 do ZUPD. O
download HTTP usa callback streaming, SHA-256 incremental e a escrita
sequencial FAT12; depois do ultimo byte, `update_verify_file()` revalida
integralmente o pacote antes do commit do cache. Timeout transitorio permite
uma repeticao do byte zero. Cancelamento, assinatura, hash ou politica
invalidos nao repetem. Consulta e download iniciados pelo System Updater rodam
no processo nativo `Updater Worker`: enquanto ele bloqueia cooperativamente
entre ciclos HTTP, o trabalho de rede continua na kworker, enquanto
`Zephyr System` atende teclado, mouse e composicao do Window Manager.

Os registros `ZUR0.STA` e `ZUR1.STA` recuperam download interrompido sem
rede. Os slots `ZUR0.ZUP` e `ZUR1.ZUP` alternam somente depois da autenticacao,
preservando o pacote anterior em toda falha. FAT32 permite consulta do
manifesto, mas nao download. O contrato binario completo esta em
[`distribuicao-remota.md`](../14-atualizacoes/distribuicao-remota.md).

## SYNC3: Kernel Workqueues

`src/core/workqueue.c` mantem um registro estatico geracional de 64 trabalhos,
duas filas FIFO `HIGH`/`NORMAL` e uma fila ordenada por prazo absoluto. O
contrato publico esta em `src/include/core/workqueue.h`; nao ha alocacao por
agendamento nem execucao de callbacks dentro de IRQ.

`schedule_work()` promove um trabalho atrasado e coalesce duplicatas.
Agendamento durante `RUNNING` solicita uma unica reexecucao;
`schedule_delayed_work()` preserva o prazo mais proximo. `cancel_work()` remove
entradas prontas/atrasadas e cancela a reexecucao de callback ja iniciado.

A `Zephyr kworker` e um processo ring0 bloqueado na Wait Queue `KWORKER`. Ela
despacha Bottom-Halves e timers com prioridade alta e rede/sockets e indice com
prioridade normal. System e o loop principal usam a mesma API somente como
fallback. A migracao futura para `thread_t` esta registrada em `DT100-002`.

`workq status|list|check`, `health check` e `regcheck full` expoem metricas,
contexto, saturacao, falhas de wake e invariantes. Duracao sub-tick permanece
`N/D`, pois nao ha RDTSC/PMU nesta etapa. `workq check` tambem bloqueia o Shell numa fila
privada e exige que a kworker execute o trabalho de prova e acorde o waiter.

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
falhas de alocacao e liberacoes rejeitadas.

Na MM4, `memory_zone_t` e `memory_detailed_stats_t` publicam a contabilidade
exclusiva das paginas fisicas em `KERNEL`, `HEAP`, `SLAB`, `PROCESS`, `BUFFER` e
`FREE`. `pmm_alloc_page_in_zone()` e `pmm_alloc_pages_in_zone()` classificam
novas alocacoes; `pmm_alloc_page()` e `pmm_alloc_pages()` continuam wrappers
compativeis para alocacoes genericas do kernel. O metadata de ownership usa a
janela de bitmap do PMM, e liberacoes de paginas reservadas sao rejeitadas sem
alterar os contadores.

`memory_get_detailed_stats()` retorna `OK` com soma das zonas, runs livres,
maior run, paginas livres isoladas e fragmentacao fisica. A fragmentacao e
`((free_pages - largest_free_run) * 100) / free_pages`, ou zero sem paginas
livres; a fragmentacao interna do heap permanece separada. A consulta percorre
o metadata sob demanda. O Task Manager usa um snapshot com no maximo uma
atualizacao por segundo, e nenhum caminho de IRQ ou page fault executa essa
varredura.

`health` preserva todos os blocos existentes e acrescenta fragmentacao,
rejeicoes do PMM e diretorios/paginas de usuario ativos. Esses campos sao
diagnosticos internos; `mem` continua mostrando o resumo legado e `mem detailed`
mostra as categorias MM4. A App API permanece com o resumo global.

## MM1: caches SLAB/SLUB

O kernel inicializa `kmem_cache_init()` depois de `memory_init()` e antes da
App API. A criacao de caches registra metadados estaticos; a primeira pagina de
um slab so e solicitada ao PMM depois que o paging esta pronto. O alocador
mantem ate 16 caches, 128 slabs globais e 128 objetos por slab, com listas
`full`, `partial` e `empty`, bitmap e freelist.

Processos, threads, arquivos e vnodes usam caches dedicados. A Ethernet usa
`sk_buff_t` interno para os frames RX/TX; buffers especificos dos protocolos
nao fazem parte da migracao. Stacks continuam em `kmalloc` por exigirem tamanho
e guardas proprios. Falhas de criacao de cache desabilitam o consumidor e sao
registradas; liberacoes invalidas, double free e destruicao ocupada sao
recusadas e contabilizadas.

`health`, `memcheck`, `schedcheck`, `regcheck full` e `vfs_validate_state()`
validam a integridade do alocador. `slabinfo` expoe as estatisticas por cache e
`slabtest` verifica alinhamento, reutilizacao, estados, erros de posse e
devolucao das paginas ao PMM.

O contrato de `memory.h` mantém os bitmaps do PMM em `0x88000–0x98000` e a
stack inicial em `0x98000–0x9F000`, mas posiciona kernel e BSS em
`0x00100000–0x00800000`. A ABI ZAPP continua em `0x00800000–0x01000000`, o
heap ocupa `0x01000000–0x01400000` e o PMM entrega páginas mapeadas por
identidade somente a partir de `0x01400000`.

O contexto fixo recebido do boot ocupa a página `0x2000–0x2FFF`, incluindo
VESA e o handoff `ZSBH` em `0x2800`. O diretório do kernel preserva somente
essa página baixa como identity-mapped e supervisor-only até a confirmação do
slot. Diretórios ring 3 compartilham a tabela sem a flag `USER`; página zero,
E820 em `0x3000` e as demais lacunas baixas permanecem não mapeadas.

A inicialização exige 32 MiB de RAM e confirma no E820 que as áreas baixas,
o kernel e o heap são utilizáveis. Páginas abaixo de `0x01400000` permanecem
reservadas, impedindo colisões entre boot, kernel, ZAPP e heap.

Falhas recuperaveis retornam erro e desabilitam somente o componente afetado.
Excecoes fatais e corrupcao estrutural continuam encaminhadas para `panic`.
O `boot.asm` permanece inalterado e a politica de escalonamento nao faz parte
desta etapa.
