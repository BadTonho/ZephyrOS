# ZephyrOS - Roadmap de Desenvolvimento

Sistema operacional em C + Assembly (x86), do zero.

---

## Progresso Geral: 9/9 fases concluídas + Pós-Fase

```
[████████████████████████████████████████████] 100%
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
- [x] Prompt `zephyr>` colorido
- [x] 27 comandos disponíveis

### Comandos

| Comando | Descrição |
|---------|-----------|
| `help` | Lista todos os comandos |
| `clear` | Limpa a tela |
| `desktop` | Abre a área de trabalho |
| `settings` | Abre o painel de configurações |
| `wm` | Abre gerenciador de janelas |
| `ls` | Lista arquivos no disco |
| `cat <arq>` | Exibe conteúdo de arquivo |
| `echo <texto>` | Imprime texto |
| `mem` | Mostra memória total/livre/usada |
| `procs` | Lista processos ativos |
| `threads` | Lista threads ativas |
| `uptime` | Tempo desde boot |
| `beep` | Toca beep (freq duracao) |
| `melody` | Toca escala musical |
| `explorer` | Abre gerenciador de arquivos |
| `taskmgr` | Abre gerenciador de tarefas |
| `taskcfg` | Configura a barra de tarefas |
| `compress` | Liga/desliga compressão de RAM |
| `stats` | Mostra estatísticas de compressão |
| `play` | Toca arquivo WAV |
| `view` | Exibe imagem BMP |
| `stop` | Para player de mídia |
| `edit` | Editor de texto (edit ARQUIVO.TXT) |
| `mouse` | Mostra status do mouse PS/2 |
| `guitest` | Testa primitivas GUI 2D |
| `reboot` | Reinicia o sistema |
| `shutdown` | Desliga o sistema |

## Fase 8 - Extras ✅
> Arquivos: `src/drivers/speaker.c`, `src/thread/thread.c`

- [x] PC Speaker - beep com frequência/duração customizável
- [x] Reprodução de melodias (array de frequências)
- [x] Multi-threading básico (create, block, yield)
- [x] Thread scheduler (round-robin)

## Fase 9 - File Manager ✅
> Arquivos: `src/filemanager/filemanager.c`, `src/include/filemanager.h`

- [x] Funções de vídeo TUI (set_cursor, put_char_at, fill_rect, draw_box)
- [x] fat12_delete_file() - exclusão de arquivos
- [x] fat12_get_file_count() / fat12_get_file_info() - listagem detalhada
- [x] Interface estilo Windows Explorer (TUI)
- [x] Navegação com setas, Page Up/Down, Home/End
- [x] Visualização de conteúdo de arquivos (F3)
- [x] Criação de arquivos (F7)
- [x] Exclusão de arquivos com confirmação (F8)
- [x] Renomeação de arquivos (F2)
- [x] Barra de título, menu, colunas, status bar
- [x] Comando `explorer` no shell

---

## Pós-Fase 9 - Módulos Adicionais

### FAT32 (`src/fs/fat32.c`)
- [x] BPB FAT32 (sectors_per_fat > 0)
- [x] Cluster chain de 32 bits (0x0FFFFFFF = EOF)
- [x] Leitura/escrita/exclusão de arquivos
- [x] Listagem de diretório com suporte a cluster chain

### FS Unificado (`src/fs/fs.c`)
- [x] Interface única: read, write, delete, list_dir
- [x] Detecção automática FAT12 ou FAT32
- [x] fs_get_info() com informações do FS ativo

### BMP (`src/fs/bmp.c`)
- [x] Leitura de BMP (1, 4, 8, 24 bpp)
- [x] Renderização na tela VESA (bmp_draw, bmp_draw_scaled)
- [x] Suporte a paleta de cores (bpp <= 8)

### WAV (`src/fs/wav.c`)
- [x] Parse de arquivos WAV (RIFF/WAVE)
- [x] Suporte a múltiplos formatos (sample rate, bits, canais)
- [x] Reprodução via AC97

### PCI (`src/drivers/pci.c`)
- [x] Enumeração do barramento PCI (256 buses × 32 devices × 8 functions)
- [x] Leitura/escrita de configuração (BARs, IRQ, classe/subclasse)
- [x] Busca por vendor/device ID e classe/subclasse
- [x] Bus Mastering enable

### AC97 (`src/drivers/ac97.c`)
- [x] Driver de áudio via controladora AC97 no PCI
- [x] Configuração de sample rate (44100 Hz)
- [x] Play/Stop com buffer, controle de volume (0-31)
- [x] Handler de interrupção

### VESA (`src/drivers/vesa.c`)
- [x] Modo gráfico via VESA BIOS Extensions
- [x] Enumeração automática de modos (640x480 a 1920x1200, 32bpp)
- [x] Primitivas: pixel, retângulo, linha, círculo, bitmap, texto com fonte

### Font (`src/drivers/font.c`)
- [x] Fonte bitmap 8x16 para renderização em modo gráfico

### RAM Compression (`src/memory/compress.c`)
- [x] Algoritmo LZSS com dicionário deslizante (4KB)
- [x] compress_data() / decompress_data()
- [x] Estatísticas de compressão
- [x] Comando shell `compress on|off|status`

### Desktop Environment (`src/desktop/desktop.c`)
- [x] Ambiente desktop com ícones grade (5 colunas)
- [x] Navegação por setas, Enter para abrir apps
- [x] Integração com taskbar

### Window Manager (`src/wm/wm.c`)
- [x] Múltiplas janelas com foco, Z-order, título
- [x] Botões: fechar, minimizar, maximizar (posição e ordem configuráveis)
- [x] Barra de título com texto
- [x] Redimensionamento e movimentação
- [x] Estatísticas de CPU por janela

### Taskbar (`src/taskbar/taskbar.c`)
- [x] Botões de aplicativos com indicador ativo/inativo
- [x] Menu Iniciar (Desktop, Shell, Explorer, TaskMgr, Config, Reiniciar, Desligar)
- [x] Menu de configuração (F1): posição, tamanho, fixar
- [x] Relógio HH:MM (atualizado a cada segundo)
- [x] 5 posições: baixo, cima, esquerda, direita, custom

### Settings (`src/settings/settings.c`)
- [x] Sistema completo com 7 categorias: Tela, Taskbar, Janelas, Ícones, Sistema, Som, Sobre
- [x] Tipos de opção: toggle, lista, ação
- [x] Editor visual de ícones (caractere, cor, cor de seleção)
- [x] Aplicação em tempo real das configurações

### Icons (`src/icons/icons.c`)
- [x] Registry com 4 categorias: desktop, WM, file manager, taskbar
- [x] Funções get/set para cada ícone
- [x] Restauração de valores padrão

### Editor de Texto (`src/shell/editor.c`)
- [x] Buffer de linhas dinâmicas (até 1000 linhas, 256 chars cada)
- [x] Syntax highlight: C, Python, Assembly, Markdown
- [x] Word wrap automático
- [x] Detecção de encoding (ASCII, Latin1, UTF-8)
- [x] Detecção de line ending (LF, CR, CRLF)
- [x] Numeração de linhas e scroll vertical
- [x] Comando `edit ARQUIVO.TXT` no shell

### Media Player (`src/shell/mediaplayer.c`)
- [x] Reprodução de WAV via driver AC97
- [x] Display com estado, informações do áudio, duração
- [x] Controles: Play/Pause, Stop, Volume
- [x] Comando `play MUSICA.WAV` no shell

### Task Manager (`src/shell/taskmanager.c`)
- [x] 4 guias: Processos, CPU, Memória, Threads
- [x] Barras de uso com cores (verde/amarelo/vermelho)
- [x] Cálculo de CPU por processo (delta ticks)
- [x] Atualização periódica
- [x] Comando `taskman` no shell

### File Manager (`src/filemanager/filemanager.c`)
- [x] Interface Explorer TUI com colunas e status bar
- [x] Navegação: setas, Page Up/Down, Home/End
- [x] F2: Renomear, F3: Visualizar, F5: Refresh, F7: Novo, F8: Excluir
- [x] Integração com taskbar
- [x] Comando `explorer` no shell

### Mouse Driver (`src/drivers/mouse.c`)
- [x] Driver PS/2 mouse via IRQ12
- [x] Inicialização do controlador (comandos 0xA8, 0xD3, 0xF4)
- [x] Ring buffer de eventos (fila de 128 eventos)
- [x] API de callback para apps
- [x] Renderização de cursor (salvar/desenhar/restaurar pixels)
- [x] Comando `mouse` no shell (status X, Y, botões)

### IPC (`src/process/ipc.c`)
- [x] Sistema de mensagens entre processos (send/receive)
- [x] Foco de janela (process_set_focus / process_get_focus)
- [x] Integração com mouse e window manager

### GUI Moderna (`src/gui/gui.c`)
- [x] Primitivas gráficas 2D (pixel-level)
- [x] gui_draw_text() - texto renderizado pixel a pixel
- [x] gui_draw_button() - botão com estado pressed
- [x] gui_draw_window_frame() - moldura de janela gráfica
- [x] Comando `guitest` no shell

### Core Enhancements
- [x] Sistema de logging com 4 níveis e buffer (`src/core/log.c`)
- [x] Utilitários de string (`src/core/string.c` - kmemset, kmemcpy, kstrcmp, kstrlen)
- [x] Códigos de erro padronizados (`src/include/core/errors.h`)
- [x] Spinlock para sincronização (`src/include/core/spinlock.h`)

## Fase 10 - GUI Moderna (Planejado)
> Arquivos a serem modificados: `src/core/video.c`, `src/desktop/desktop.c`, `src/taskbar/taskbar.c`, `src/wm/wm.c`, `src/drivers/font.c`

- [ ] **Primitive Graphics 2D**: Funções de desenho de retângulos arredondados, gradientes e sombras.
- [ ] **Renderização de Textos Livres**: Substituir `video_put_char_at` por texto gráfico (desenho pixel a pixel) que possa ser renderizado em qualquer X/Y, não apenas na grade (col/row).
- [ ] **Desktop Gráfico**: Mudar os ícones do Desktop para carregar imagens reais em `.bmp` do disco, removendo a caixa TUI de texto.
- [ ] **Barra de Tarefas (Taskbar) Moderna**: Desenhar o botão Iniciar e botões dos apps usando primitive graphics (cores preenchidas e texto solto) e um menu em popup flutuante.
- [ ] **Window Manager Gráfico**: Mudar as molduras (bordas e titlebars) das janelas do modo texto para primitivas visuais em VESA. Botões de "Fechar/Minimizar/Maximizar" gráficos.
- [ ] **Integração Plena de Mouse**: Usar o `mouse_event_t` para lidar com cliques reais nos botões gráficos, double-clicks em ícones e arrastar (Drag & Drop) de janelas pelo mouse, não mais pelo teclado.

---

## Backlog e Melhorias Futuras

O projeto conta com uma extensa lista de melhorias e novos módulos planejados, documentados na pasta `docs/melhorias futuras/`. O backlog está organizado nas seguintes categorias:

### Kernel e Sistema
- **Multitarefa Preemptiva Avançada** (`multitarefa preemptiva.md`)
- **Gerenciador de Energia** (`gerenciador de energia.md`)
- **Gerenciador de Dispositivos** (`gerenciador de dispositivos.md`)
- **Gerenciador de Rede / Conexão** (`gerenciador de rede.md`, `gerenciador de conexao.md`)
- **Atualizações do Sistema** (`atualizações.md`)
- **Formatação Inteligente** (`formatacao inteligente.md`)

### Melhorias na Interface Atual (UI/UX)
- **Barra de Tarefas Avançada** (`barra de tarefas.md`)
- **Gerenciador de Janelas (WM)** (`gerenciador de janelas.md`)
- **Gerenciador de Arquivos** (`gerenciador de arquivos.md`)
- **Painel de Configurações** (`configurações.md`)
- **Gerenciador de Processos** (`gerenciador de processos.md`)

### Novos Módulos e Aplicativos (App Store / Opcionais)
- **Gerenciador de Aplicativos (App Store)** (`gerenciador de aplicativos.md`)
- **Gerenciador de Mídia** (`gerenciador de midia.md`)
- **Gerenciador de Jogos** (`gerenciador de jogos.md`)
- **Anti-Virus** (`anti virus.md`)
- **PCSista** (`pcsista.md`)
- **Ferramentas para Programadores** (`programadores.md`)

*(Nota: O suporte inicial ao Mouse documentado em `mouse.md` já foi concluído e integrado ao sistema)*

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
- [Writing a Simple OS from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf) - Referência de OS development
- [James Molloy's Kernel Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/) - Referência de kernel development
- [OSDev Wiki - FAT12](https://wiki.osdev.org/FAT12) - Formato FAT12
- [OSDev Wiki - ATA PIO](https://wiki.osdev.org/ATA PIO_Mode) - Driver ATA
