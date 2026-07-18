# MiniOS - Roadmap de Desenvolvimento

Sistema operacional educacional em C, do zero.

---

## Fase 1 - Bootloader
- [ ] Escrever bootloader em Assembly (16-bit Real Mode)
- [ ] Carregar kernel do disco para memória
- [ ] Switch para Protected Mode (32-bit)
- [ ] Configurar GDT (Global Descriptor Table)
- [ ] Jump para o kernel em C

## Fase 2 - Kernel Base
- [ ] Entry point do kernel em C
- [ ] Inicialização do vídeo (VGA Text Mode)
- [ ] Funções de impressão na tela (`print`, `clear`, `set_color`)
- [ ] Tratamento de erros (panic handler)

## Fase 3 - Drivers Básicos
- [ ] Driver de teclado (PS/2)
- [ ] Driver de vídeo (cores, cursor, scroll)
- [ ] Driver de temporizador (PIT - Programmable Interval Timer)
- [ ] IDT (Interrupt Descriptor Table) e ISRs

## Fase 4 - Memória
- [ ] Detecção de memória (BIOS int 0x15, eax=0xE820)
- [ ] Gerenciador de memória (bitmap allocator)
- [ ] Heap simples (malloc/free)
- [ ] Virtual Memory (page tables)

## Fase 5 - Processos
- [ ] TSS (Task State Segment)
- [ ] Gerenciador de processos (PID, estado, contexto)
- [ ] Scheduler básico (round-robin)
- [ ] Context switch em Assembly

## Fase 6 - Sistema de Arquivos
- [ ] Driver de disco (ATA/ATAPI PIO)
- [ ] Sistema de arquivos simples (FAT12 ou próprio)
- [ ] Leitura/escrita de arquivos
- [ ] Conceito de diretórios

## Fase 7 - Shell
- [ ] Terminal com input do usuário
- [ ] Parser de comandos
- [ ] Comandos básicos: `help`, `clear`, `ls`, `cat`, `echo`
- [ ] Sistema de arquivos explorável pelo usuário

## Fase 8 - Extras
- [ ] Som (PC Speaker beep)
- [ ] Modo gráfico (opcional)
- [ ] Multi-threading básico
- [ ] Syscalls (chamadas de sistema)

---

## Tecnologias
- **Linguagem:** C + Assembly (x86)
- **Build:** Makefile + GCC cross-compiler (i686-elf)
- **Teste:** QEMU / Bochs
- **Arquitetura:** x86 (32-bit)

## Referências
- [OSDev Wiki](https://wiki.osdev.org)
- [Writing a Simple Operating System — from Scratch (Nick Blundell)](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
- [James Molloy's Kernel Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/)
