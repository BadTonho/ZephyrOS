# 02 - Arquitetura do Sistema

## Visão Geral

O MiniOS é organizado em camadas, cada uma responsável por uma parte específica do sistema.

```
┌─────────────────────────────────────┐
│            Shell (minios>)          │  ← Interface com o usuário
├─────────────────────────────────────┤
│         Sistema de Arquivos         │  ← FAT12, leitura/escrita
├─────────────────────────────────────┤
│        Processos / Threads          │  ← Scheduler, context switch
├─────────────────────────────────────┤
│           Gerenciamento             │  ← Memória, paging, heap
│              de Memória             │
├─────────────────────────────────────┤
│           Drivers de Hardware       │  ← Teclado, timer, vídeo, disco
├─────────────────────────────────────┤
│         IDT / IRQ / ISR             │  ← Interrupções e exceções
├─────────────────────────────────────┤
│            Kernel Core              │  ← Entry point, panic
├─────────────────────────────────────┤
│           Bootloader                │  ← Assembly, switch de modo
└─────────────────────────────────────┘
```

## Fluxo de Execução

```
BIOS → Boot.asm → Protected Mode → kernel_main() → Shell
```

1. **BIOS** carrega o boot sector (512 bytes) em `0x7C00`
2. **Bootloader** detecta memória, carrega kernel, muda para 32-bit
3. **Kernel** inicializa todos os subsistemas em ordem
4. **Shell** aguarda input do usuário

## Ordem de Inicialização

```
video_init()        → Tela pronta para mostrar mensagens
idt_init()          → Interrupções funcionando
keyboard_init()     → Teclado respondendo
timer_init(50)      → Timer a 50 Hz
memory_init()       → Memória detectada e alocável
paging_init()       → Paginação ativa
tss_init()          → Kernel stack configurado
process_init()      → Gerenciador de processos pronto
thread_init()       → Gerenciador de threads pronto
ata_init()          → Disco detectado
fat12_init()        → Sistema de arquivos montado
speaker_init()      → PC Speaker pronto
shell_init()        → Shell aguardando input
```

## Estrutura de Arquivos

```
src/
├── boot/           → Bootloader (Assembly puro)
├── kernel/         → Código central do SO
├── drivers/        → Drivers de hardware
├── memory/         → Gerenciamento de memória
├── fs/             → Sistema de arquivos (FAT12)
├── process/        → Gerenciador de processos
├── thread/         → Gerenciador de threads
├── shell/          → Terminal interativo
└── include/        → Headers compartilhados
```

## Convenções de Código

- **Nomes**: `snake_case` para funções e variáveis
- **Headers**: `#ifndef HEADER_H` para include guards
- **Structs**: `__attribute__((packed))` para alinhamento exato
- **Kernel**: Tudo `freestanding` (sem libc, sem stdlib)
- **Assembly**: Sintaxe NASM, Intel syntax
