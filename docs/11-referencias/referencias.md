# 11 - Referências e Glossário

## Referências

### Sites

| Site | Descrição |
|------|-----------|
| [OSDev Wiki](https://wiki.osdev.org) | Wiki principal de OS development |
| [OSDev Wiki - Getting Started](https://wiki.osdev.org/Getting_Started) | Primeiros passos |
| [OSDev Wiki - Bare Bones](https://wiki.osdev.org/Bare_Bones) | Guia mínimo para kernel básico |
| [OSDev Wiki - GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) | Como criar cross-compiler |

### Documentos

| Documento | Autor | Descrição |
|-----------|-------|-----------|
| [Writing a Simple OS from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf) | Nick Blundell | Referência completa de OS development |
| [James Molloy's Kernel Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/) | James Molloy | Referência de kernel development |
| [OS Development Series](http://www.osdever.net/bkerndev/Sections/Introduction) | Blaine Garfolo | Série de referências de OS development |

### Wiki Entries

| Tópico | Link |
|--------|------|
| FAT12 | https://wiki.osdev.org/FAT12 |
| ATA PIO Mode | https://wiki.osdev.org/ATA PIO_Mode |
| IDT | https://wiki.osdev.org/IDT |
| GDT | https://wiki.osdev.org/GDT |
| Paging | https://wiki.osdev.org/Paging |
| PIC | https://wiki.osdev.org/PIC |
| PIT | https://wiki.osdev.org/PIT |
| PS/2 Keyboard | https://wiki.osdev.org/PS/2_Keyboard |
| PC Speaker | https://wiki.osdev.org/PC_Speaker |

---

## Glossário

### A

**ACPI** - Advanced Configuration and Power Interface. Padrão para gerenciamento de energia.

**API** - Application Programming Interface. Conjunto de funções disponíveis para programadores.

**ATA** - AT Attachment. Padrão para comunicação com discos rígidos.

### B

**BIOS** - Basic Input/Output System. Firmware que inicia o computador.

**Bootloader** - Programa que carrega o sistema operacional na memória.

**BPB** - BIOS Parameter Block. Estrutura no boot sector com informações do disco.

### C

**Cluster** - Grupo de setores no disco. Unidade de alocação do FAT12.

**Context Switch** - Troca de contexto entre processos/threads.

**CPU** - Central Processing Unit. Processador.

### D

**Descriptor** - Entrada na GDT que define um segmento de memória.

**Driver** - Programa que permite ao kernel comunicar com hardware.

### E

**E820** - Função do BIOS para detectar mapa de memória.

**ELF** - Executable and Linkable Format. Formato de executáveis no Linux.

**Exception** - Interrupção gerada pelo CPU (erro, page fault, etc.).

### F

**FAT12** - File Allocation Table 12-bit. Sistema de arquivos para disquetes.

**Fork** - Criar um processo filho copiando o processo pai.

### G

**GDT** - Global Descriptor Table. Tabela de segmentos de memória.

### H

**Heap** - Área de memória para alocação dinâmica.

### I

**IDT** - Interrupt Descriptor Table. Tabela de handlers de interrupção.

**IRQ** - Interrupt Request. Interrupção de hardware.

**ISR** - Interrupt Service Routine. Handler de exceção do CPU.

### K

**Kernel** - Núcleo do sistema operacional.

### L

**LBA** - Logical Block Addressing. Endereçamento lógico de setores.

### M

**MBR** - Master Boot Record. Primeiro setor do disco (512 bytes).

**MMU** - Memory Management Unit. Unidade de gerenciamento de memória.

### P

**Paging** - Mecanismo de memória virtual.

**PIC** - Programmable Interrupt Controller. Controlador de interrupções.

**PIT** - Programmable Interval Timer. Timer programável.

**Protected Mode** - Modo de 32-bit do x86 com proteção de memória.

### R

**Ring 0** - Nível de privilégio do kernel (máximo acesso).

**Ring 3** - Nível de privilégio de usuário (mínimo acesso).

**Real Mode** - Modo de 16-bit do x86 (sem proteção).

### S

**Scheduler** - Algoritmo que decide qual processo roda.

**Setor** - Unidade mínima de leitura/escrita no disco (512 bytes).

**Syscall** - Chamada de sistema. Interface entre user e kernel.

### T

**TSS** - Task State Segment. Estrutura com kernel stack.

### V

**VGA** - Video Graphics Array. Padrão de vídeo.

**VM** - Virtual Memory. Memória virtual.

---

## Tabelas de Referência Rápida

### Cores VGA

| Código | Cor | Código | Cor |
|--------|-----|--------|-----|
| 0x0 | Preto | 0x8 | Cinza Escuro |
| 0x1 | Azul | 0x9 | Azul Claro |
| 0x2 | Verde | 0xA | Verde Claro |
| 0x3 | Ciano | 0xB | Ciano Claro |
| 0x4 | Vermelho | 0xC | Vermelho Claro |
| 0x5 | Magenta | 0xD | Magenta Claro |
| 0x6 | Marrom | 0xE | Amarelo |
| 0x7 | Cinza | 0xF | Branco |

### Portas de Hardware

| Porta | Uso |
|-------|-----|
| 0x20-0x21 | PIC Master |
| 0x40-0x43 | PIT Timer |
| 0x60 | Teclado (dados) |
| 0x61 | PC Speaker |
| 0x64 | Teclado (comando) |
| 0xA0-0xA1 | PIC Slave |
| 0x1F0-0x1F7 | ATA Primary |
| 0x3D4-0x3D5 | VGA Cursor |

### Registradores x86

| Registrador | Tamanho | Função |
|-------------|---------|--------|
| EAX | 32-bit | Acumulador / Return value |
| EBX | 32-bit | Base |
| ECX | 32-bit | Counter |
| EDX | 32-bit | Data |
| ESI | 32-bit | Source Index |
| EDI | 32-bit | Destination Index |
| EBP | 32-bit | Base Pointer |
| ESP | 32-bit | Stack Pointer |
| EIP | 32-bit | Instruction Pointer |
| EFLAGS | 32-bit | Flags |
| CR0 | 32-bit | Control (modo) |
| CR3 | 32-bit | Page Directory |
