# MiniOS - Roadmap de Desenvolvimento

Sistema operacional educacional em C + Assembly (x86), do zero.

---

## Progresso Geral: 8/8 fases concluídas

```
[████████████████████████████████████████] 100%
```

---

## Fase 1 - Bootloader ✅
> Arquivo: `src/boot/boot.asm`

- [x] Escrever bootloader em Assembly (16-bit Real Mode)
- [x] BPB (BIOS Parameter Block) para FAT12
- [x] Detecção de memória via BIOS int 0x15 (E820)
- [x] Carregar kernel do disco para memória (int 0x13)
- [x] Configurar GDT (Global Descriptor Table)
- [x] Switch para Protected Mode (32-bit)
- [x] Passar mapa de memória em ESI para o kernel

## Fase 2 - Kernel Base ✅
> Arquivos: `src/kernel/kernel.c`, `src/kernel/panic.c`, `src/kernel/entry.asm`

- [x] Entry point do kernel em Assembly
- [x] Inicialização do vídeo (VGA Text Mode 80x25)
- [x] Funções de impressão na tela (`video_print`, `video_clear`, `video_set_color`)
- [x] Tratamento de erros (kernel panic com tela vermelha)

## Fase 3 - Drivers Básicos ✅
> Arquivos: `src/drivers/idt.c`, `src/drivers/isr.asm`, `src/drivers/irq.asm`, `src/drivers/keyboard.c`, `src/drivers/timer.c`

- [x] ISRs - 32 exceções do CPU (div by zero, page fault, etc.)
- [x] IRQs - 16 interrupções de hardware mapeadas para IDT 32-47
- [x] Remapeamento PIC master (0x20→32) e slave (0xA0→40)
- [x] Driver de teclado PS/2 (scancode → ASCII)
- [x] Driver de temporizador PIT (50 Hz, incrementa ticks)

## Fase 4 - Memória ✅
> Arquivos: `src/memory/memory.c`, `src/memory/paging.c`

- [x] Detecção de memória (BIOS int 0x15, eax=0xE820)
- [x] Bitmap allocator (1 bit por página de 4KB)
- [x] Heap simples (first-fit com coalescência)
- [x] Funções `kmalloc()`, `kfree()`, `kmalloc_aligned()`
- [x] Paging (page directory/table, mapeamento virtual→físico)
- [x] Mapeamento de kernel e VGA memory

## Fase 5 - Processos ✅
> Arquivos: `src/drivers/tss.c`, `src/process/process.c`, `src/kernel/switch.asm`

- [x] TSS (Task State Segment) - kernel stack ring 0
- [x] Gerenciador de processos (PID, nome, estado, contexto)
- [x] Estados: UNUSED, READY, RUNNING, BLOCKED, ZOMBIE
- [x] Scheduler preemptivo (round-robin via timer)
- [x] Context switch em Assembly (salva/restaura registradores)

## Fase 6 - Sistema de Arquivos ✅
> Arquivos: `src/drivers/ata.c`, `src/fs/fat12.c`

- [x] Driver ATA PIO (ler/escrever setores via port 0x1F0)
- [x] Identificação de disco (ATA_CMD_IDENTIFY)
- [x] Sistema de arquivos FAT12 (BPB, FAT, root dir)
- [x] Leitura de arquivo (`fat12_read_file`)
- [x] Escrita de arquivo (`fat12_write_file`)
- [x] Listagem de diretório (`fat12_list_dir`)

## Fase 7 - Shell ✅
> Arquivo: `src/shell/shell.c`

- [x] Terminal interativo com input via teclado
- [x] Parser de comandos (split comando + argumentos)
- [x] Prompt `minios>` colorido
- [x] 13 comandos disponíveis

### Comandos

| Comando | Descrição |
|---------|-----------|
| `help` | Lista todos os comandos |
| `clear` | Limpa a tela |
| `ls` | Lista arquivos no disco |
| `cat <arq>` | Exibe conteúdo de arquivo |
| `echo <texto>` | Imprime texto |
| `mem` | Mostra memória total/livre/usada |
| `procs` | Lista processos ativos |
| `threads` | Lista threads ativas |
| `uptime` | Tempo desde boot |
| `beep` | Toca beep (freq duracao) |
| `melody` | Toca escala musical |
| `reboot` | Reinicia o sistema |
| `shutdown` | Desliga o sistema |

## Fase 8 - Extras ✅
> Arquivos: `src/drivers/speaker.c`, `src/thread/thread.c`

- [x] PC Speaker - beep com frequência/duração customizável
- [x] Reprodução de melodias (array de frequências)
- [x] Multi-threading básico (create, block, yield)
- [x] Thread scheduler (round-robin)

---

## Tecnologias

| Item | Tecnologia |
|------|-----------|
| Linguagem | C + Assembly (x86) |
| Build | Makefile + NASM + GCC cross-compiler |
| Teste | QEMU / Bochs |
| Arquitetura | x86 (32-bit, Protected Mode) |
| Padrão | Freestanding (sem libc) |

## Referências

- [OSDev Wiki](https://wiki.osdev.org) - Wiki principal de OS development
- [Writing a Simple OS from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf) - PDF didático
- [James Molloy's Kernel Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/) - Tutorial passo a passo
- [OSDev Wiki - FAT12](https://wiki.osdev.org/FAT12) - Formato FAT12
- [OSDev Wiki - ATA PIO](https://wiki.osdev.org/ATA PIO_Mode) - Driver ATA
