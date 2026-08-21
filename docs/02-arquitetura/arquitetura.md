# 02 - Arquitetura do Sistema

## Visão Geral

O ZephyrOS é organizado em camadas, cada uma responsável por uma parte específica do sistema.

```
┌─────────────────────────────────────┐
│     Shell + Apps (zephyr>)          │  ← Interface com o usuário
│  (editor, mediaplayer, taskmgr)     │
├─────────────────────────────────────┤
│     Desktop / WM / Taskbar          │  ← Ambiente visual
│     (desktop, wm, taskbar,          │
│      settings, filemanager, icons)  │
├─────────────────────────────────────┤
│     GUI Primitives (gui.c)          │  ← Primitivas 2D (Modo Classic)
├─────────────────────────────────────┤
│         Sistema de Arquivos         │  ← FAT12, FAT32, BMP, WAV
├─────────────────────────────────────┤
│      Processos / Threads / IPC      │  ← Scheduler, mensagens, foco
├─────────────────────────────────────┤
│           Gerenciamento             │  ← Memória, paging, heap
│              de Memória             │
│           + Compressão LZSS         │
├─────────────────────────────────────┤
│           Drivers de Hardware       │  ← Teclado, timer, vídeo, disco
│ (ATA, AC97, PCI, VESA, font, mouse) │
├─────────────────────────────────────┤
│           IDT / IRQ / ISR           │  ← Interrupções e exceções
├─────────────────────────────────────┤
│     Kernel Core (log, panic)        │  ← Entry point, logging
├─────────────────────────────────────┤
│           Bootloader                │  ← Assembly, switch de modo
└─────────────────────────────────────┘
```

## Fluxo de Execução

```
BIOS → bootloader → Protected Mode → kernel_main() → Desktop/Shell
```

1. **BIOS** carrega o boot sector (512 bytes) em `0x7C00`
2. **Bootloader** detecta memória, carrega kernel, muda para 32-bit
3. **Kernel** inicializa todos os subsistemas em ordem
4. **Desktop** é a cena padrão; o processo Shell permanece pronto para ser
   aberto pelo Menu Iniciar, taskbar ou ícone do Desktop.

## Ordem de Inicialização

```
vesa_init()         → Modo gráfico VESA configurado
font_init()         → Fontes bitmap carregadas (8x16, 10x20, 12x24)
video_init()        → Tela pronta para mostrar mensagens
log_init()          → Sistema de logging ativo
recovery_init()     → Tabela de recuperação e saúde dos componentes
idt_init()          → Interrupções funcionando
keyboard_init()     → Teclado respondendo
timer_init(50)      → Timer PIT a 50 Hz
memory_init()       → Memória física detectada (E820) e alocável
acpi_init()         → Descoberta de tabelas ACPI antes do paging
app_api_init()      → Contrato público de aplicativos pronto
syscall_init()      → Dispatcher int 0x80 registrado inicialmente em DPL 0
paging_init()       → Paginação ativa e mapas identity-mapped
vesa_init_backbuffer() → Backbuffer alocado para double-buffering
tss_init()          → Kernel stack configurado no TSS
process_init()      → Gerenciador de processos pronto
process_bootstrap_idle() → Processo Idle disponível como fallback
ipc_init()          → Filas de mensagens e foco prontos
thread_init()       → Gerenciador de threads pronto
block_init()        → Registro de dispositivos de bloco (ATA e USB MSC)
ata_init()          → Discos ATA detectados
fs_init()           → Sistema de arquivos montado (FAT12/FAT32)
storage_init()      → Volumes e partições adicionais montáveis
file_index_init()   → Índice de busca global em RAM
update_init()       → Serviço de verificação/aplicação de atualizações ZUPD
speaker_init()      → PC Speaker pronto
pci_init()          → Varredura do barramento PCI
usb_manager_init()  → Controladores UHCI e portas raiz USB
storage_refresh()   → Reconciliação de discos/volumes ATA e USB MSC
ac97_init()         → Driver de áudio AC97 ativo
device_manager_init() → Inventário central de dispositivos
network_manager_init() → Controladores Ethernet (E1000/RTL8139) e pilha TCP/IP
update_remote_init() → Transporte remoto de atualizações
power_init()        → Diagnóstico de energia e desligamento ACPI S5
icons_init()        → Registro de ícones carregado
display_init()      → Escala gráfica Classic (pequena, normal, grande)
taskbar_init()      → Barra de tarefas desenhada
desktop_init()      → Desktop com ícones e suporte dual (Simple/Classic)
settings_init()     → Configurações carregadas
wm_init()           → Window Manager ativo
updater_init()      → Aplicativo System Updater
mouse_init()        → Mouse PS/2 configurado ou fallback por teclado
process_create("Zephyr System") → Processo cooperativo de tarefas de sistema
process_create("Shell")         → Processo terminal
process_create("Desktop")       → Processo da área de trabalho
syscall_enable_user_mode() → Gate int 0x80 elevado a DPL 3
app_loader_init()   → Loader ZAPP habilitado quando dependências existem
app_package_init()  → Gestor de pacotes ZPKG
app_remote_init()   → Repositório remoto de pacotes
app_catalog_init()  → Catálogo de pacotes instalados e disponíveis
appstore_init()     → Aplicativo nativo App Store
desktop_draw()      → Cena padrão desenhada; Shell abre por solicitação
```

## Estrutura de Arquivos

```
src/
├── boot/           → Bootloader (Assembly puro)
├── kernel/         → Código central do SO (entry, panic, switch)
├── core/           → Serviços centrais (log, string, devices, rede, crypto, update, usb, app)
├── drivers/        → Drivers de hardware (video, vesa, font, idt, isr, irq, keyboard, timer, tss, ata, speaker, pci, ac97, mouse, e1000, rtl8139, uhci, usb_msc, acpi)
├── memory/         → Gerenciamento de memória (memory, paging, compress)
├── fs/             → Sistema de arquivos (block, fat12, fat32, fs, storage, file_index, wav, bmp)
├── process/        → Gerenciador de processos e IPC
├── thread/         → Gerenciador de threads
├── shell/          → Shell e apps integrados (editor, mediaplayer, taskmanager, guitest, dispatchers)
├── filemanager/    → File Manager (Explorer)
├── taskbar/        → Barra de tarefas e menu Iniciar
├── desktop/        → Ambiente desktop
├── settings/       → Configurações do sistema
├── wm/             → Window Manager
├── icons/          → Sistema de ícones
├── gui/            → Primitivas gráficas 2D e métricas de Display
├── updater/        → Aplicativo System Updater
├── appstore/       → Aplicativo App Store
└── include/        → Headers organizados por módulo (apps, core, drivers, fs, memory, process, ui)
```

## Convenções de Código

- **Nomes**: `snake_case` para funções e variáveis
- **Structs**: `snake_case_t` (typedef)
- **Headers**: `#ifndef HEADER_H` para include guards
- **Kernel**: Tudo `freestanding` (sem libc, sem stdlib)
- **Assembly**: Sintaxe NASM, Intel syntax
- **Logging**: `LOG_INFO`, `LOG_ERROR`, `LOG_WARN`, `LOG_DEBUG` via `core/log.h`
- **Erros**: Retornam códigos (`OK`, `ERR_NULL`, `ERR_MEM`, `ERR_DISK`, `ERR_NOT_FOUND`)

## Contratos de estabilidade do kernel

As dependencias centrais seguem o fluxo `kernel -> processos -> IPC -> apps`.
O diagnostico consulta os modulos sem alterar a politica de escalonamento:

- o scheduler depende do registro de processos e sempre conserva o Idle como
  caminho de continuidade;
- o IPC resolve destinos por PID e nao depende da posicao do processo no vetor;
- o paging fornece `paging_map_page()` e `paging_is_ready()` para os modulos
  que precisam mapear ou diagnosticar memoria;
- o Shell agrega o estado de recovery e as metricas do kernel no comando
  `health`;
- Desktop, Explorer, Settings e Task Manager continuam usando suas interfaces
  simple e classic, apenas consumindo os contratos estabilizados.

Essa separacao permite evoluir memoria, processos e diagnostico em etapas sem
acoplar a fundacao do kernel ao visual das aplicacoes.
