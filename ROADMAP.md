# ZephyrOS - Roadmap de Desenvolvimento

Sistema operacional em C + Assembly (x86), do zero.

---

## Progresso Geral: base do sistema concluída; Fase 6C da plataforma de aplicativos validada

```
Núcleo original (Fases 1–9): [████████████████████████████████████████████] 100%
Plataforma de aplicativos:   [██████████████████████████████████░░░░░░░░] Fase 6C validada
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
- [x] Shell com comandos nativos, diagnósticos, modos de interface e subcomandos `app`

### Comandos

| Comando | Descrição |
|---------|-----------|
| `help` | Lista todos os comandos |
| `clear` | Limpa a tela e o histórico do terminal |
| `desktop` | Abre a área de trabalho |
| `settings` | Abre o painel de configurações |
| `wm` | Abre gerenciador de janelas |
| `ls` | Lista arquivos no disco |
| `cat <arq>` | Exibe conteúdo de arquivo |
| `echo <texto>` | Imprime texto pela app ring 3 com fallback nativo |
| `mem` | Mostra memória total/livre/usada |
| `procs` | Lista processos ativos |
| `threads` | Lista threads ativas |
| `threadtest` | Valida troca cooperativa de threads |
| `uptime` | Tempo desde boot |
| `beep` | Toca beep (freq duracao) |
| `melody` | Toca escala musical |
| `explorer` | Abre gerenciador de arquivos |
| `taskmgr` | Abre gerenciador de tarefas |
| `usertest` | Testa processo isolado em ring 3 |
| `taskcfg` | Configura a barra de tarefas |
| `compress` | Liga/desliga compressão de RAM |
| `stats` | Mostra estatísticas de compressão |
| `play` | Toca arquivo WAV |
| `view` | Exibe imagem BMP |
| `stop` | Para player de mídia |
| `edit` | Editor de texto (edit ARQUIVO.TXT) |
| `mouse` | Mostra status do mouse PS/2 |
| `guitest` | Testa primitivas GUI 2D |
| `guimode` | Altera entre gui classica e moderna |
| `health` | Exibe metricas e estado de recovery |
| `appcheck` | Testa API, arquivos, IPC e carregador |
| `app run <arq> [args]` | Executa aplicativo ring 3 com argumentos simples |
| `app inputtest` | Testa teclado e foco de aplicativo ring 3 |
| `app outputtest [fail]` | Testa saida ZAPP acima de 1 KiB e codigos de saida |
| `app argtest <texto>` | Testa argumentos de aplicativo ring 3 |
| `reboot` | Reinicia o sistema |
| `shutdown` | Desliga o sistema |

## Fase 8 - Extras ✅
> Arquivos: `src/drivers/speaker.c`, `src/thread/thread.c`

- [x] PC Speaker - beep com frequência/duração customizável
- [x] Reprodução de melodias (array de frequências)
- [x] Multi-threading cooperativo (create, block, yield e troca de contexto)
- [x] Thread scheduler round-robin com `threadtest`

## Fase 9 - File Manager ✅
> Arquivos: `src/filemanager/filemanager.c`, `src/include/ui/filemanager.h`

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
- [x] TUI de diagnóstico e janela gráfica própria, com abas Processos, Memória e Threads
- [x] Métricas de CPU, memória, espera, ATA e detalhes do processo selecionado
- [x] Atualização periódica, seleção, ordenação e ações pelo teclado
- [x] Comando `taskmgr` no shell; taskbar e Desktop usam a janela gráfica no modo moderno

### File Manager (`src/filemanager/filemanager.c`)
- [x] Interface Explorer clássica com colunas e status bar, e modo moderno com moldura, painéis e seleção gráfica
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

## Fase 10 - GUI Moderna (Em andamento)
> Arquivos principais: `src/drivers/video.c`, `src/drivers/vesa.c`, `src/desktop/desktop.c`, `src/taskbar/taskbar.c`, `src/wm/wm.c`, `src/gui/gui.c`

- [x] **Desktop gráfico compatível**: Cards 3D, seleção azul, layout responsivo e fallback TUI.
- [x] **Modo explícito de interface**: Comando `guimode classic|modern`.
- [x] **Interação gráfica do Desktop**: Clique para selecionar e duplo clique para abrir aplicativos.
- [x] **Renderização de Textos Livres**: Substituir `video_put_char_at` por texto gráfico (desenho pixel a pixel) que possa ser renderizado em qualquer X/Y, não apenas na grade (col/row) - `gui_draw_text`.
- [x] **Aplicativos modernos**: Explorer, Task Manager e Settings seguem a identidade visual existente e mantêm fallback TUI.
- [x] **Double Buffering (Backbuffer)**: Renderização no VRAM em dois estágios para prevenir *flickering* (cintilação) durante o redesenho (Vesa Flip).
- [x] **Entrada gráfica básica**: Roteamento de teclado e mouse preserva prioridade da taskbar e do Menu Iniciar.
- [ ] **Taskbar moderna**: Redesenhar a taskbar sem mudar sua semântica atual de botões, relógio e Menu Iniciar.
- [ ] **Primitive Graphics 2D**: Avaliar novas primitivas somente quando houver necessidade visual comprovada.
- [ ] **Desktop Gráfico Imagens**: Mudar os ícones do Desktop para carregar imagens reais em `.bmp` do disco.
- [ ] **Window Manager Gráfico**: Mudar as molduras (bordas e titlebars) das janelas do modo texto para primitivas visuais em VESA. Botões de "Fechar/Minimizar/Maximizar" gráficos.
- [ ] **Integração Plena de Mouse**: Implementar arrastar (Drag & Drop) de janelas pelo mouse.

---

## Plataforma de Aplicativos — evolução em fases

> Documentação principal: `docs/melhorias futuras/api de aplicativos e syscalls.md`

### Fases 1–5 — base isolada ✅

- [x] Contrato da App API e diagnóstico com `appcheck`.
- [x] Dispatcher de syscalls `int 0x80` com validação de argumentos.
- [x] Serviços controlados de arquivos e IPC.
- [x] Primeiro processo em ring 3, com encerramento e falhas isoladas.
- [x] Carregador assíncrono de imagens `.ZAP`/`ZAPP`.

### Fase 6A — foco e teclado para `.ZAP` ✅

- [x] Um aplicativo externo em primeiro plano por vez.
- [x] Teclado entregue por IPC ao aplicativo focado.
- [x] `F12` cancela somente o aplicativo externo em foco.
- [x] Foco, handles e prompt do Shell são restaurados ao encerrar.

### Fase 6B — argumentos e primeira migração nativa ✅

- [x] App API `0.3` e ABI de lançamento em página própria.
- [x] `app run <arquivo.ZAP> [arg1 arg2 ...]`, com até 8 argumentos e 511 caracteres.
- [x] Imagens `.ZAP` antigas continuam recebendo uma estrutura de lançamento vazia válida.
- [x] Imagem ZAPP interna para `echo`, executada em ring 3.
- [x] Fallback nativo de `echo` quando loader, paging, modo usuário ou filesystem não estiverem disponíveis.
- [x] Comando `app argtest <texto>` para validação visual.
- [x] Validado no QEMU: `echo`, `app argtest`, encerramento por `Enter` e `F12`,
  `appcheck`, `health`, `usertest`, interfaces existentes e os dois modos gráficos.

### Fase 6C — migração gradual de ferramentas nativas ✅

- [x] Selecionados `uptime` e `mem`, que dependem apenas de consultas já expostas pela App API.
- [x] Ambos executam como imagens ZAPP internas em ring 3, mantendo fallback nativo quando o loader está indisponível.
- [x] `appcheck` valida lançamento concorrente, execução, foco retornado e ausência de zumbis; `health` mostra o estado individual das migrações.
- [x] Validado no QEMU: `uptime`, `mem`, `appcheck`, `health`, `echo`, `usertest`, falha isolada e cancelamento por `F12`.
- [ ] Não migrar Explorer, Settings, Task Manager ou Desktop antes de uma API gráfica segura.

### Fase 6D — contrato de console e ciclo de vida ✅

- [x] `console_write` em blocos síncronos de até 1024 bytes, sem fila ou quota total.
- [x] Códigos de saída: `0` para sucesso, não-zero como erro do app e `0xF120` reservado ao runtime.
- [x] `app outputtest [fail]` valida nove blocos de 128 bytes e término normal com sucesso ou erro.
- [x] Validado no QEMU: `app outputtest`, `app outputtest fail`, argumento inválido,
  `appcheck`, `app inputtest` por `Enter` e `F12`, `usertest fault`, `health`,
  `echo`, `uptime`, `mem` e ausência de ZAPPs ou zumbis residuais.

### Etapas posteriores da plataforma

- [ ] Empacotador no host e formato completo `.zephyrosapp`.
- [ ] Manifesto, versão e verificação de integridade dos pacotes.
- [ ] Argumentos mais completos, incluindo aspas e escapes.
- [ ] Serviços adicionais da App API conforme a necessidade dos aplicativos migrados.
- [ ] Avaliar permissões e isolamento adicional somente após métricas e testes de estabilidade.

---

## Roadmaps por etapa

O roadmap principal mantém a visão geral. Os próximos trabalhos executáveis foram
separados por dependência para não misturar estabilização, plataforma de apps,
kernel, interface e novos serviços:

| Ordem | Documento | Objetivo |
|-------|-----------|----------|
| 1 | [`docs/roadmaps/01-estabilizacao-e-qualidade.md`](docs/roadmaps/01-estabilizacao-e-qualidade.md) | Regressão, diagnóstico e fallbacks antes de novas migrações. |
| 2 | [`docs/roadmaps/02-plataforma-de-aplicativos.md`](docs/roadmaps/02-plataforma-de-aplicativos.md) | Fase 6C, ciclo de vida do console e pacotes futuros. |
| 3 | [`docs/roadmaps/03-kernel-e-desempenho.md`](docs/roadmaps/03-kernel-e-desempenho.md) | Métricas, scheduler, memória e otimização baseada em evidências. |
| 4 | [`docs/roadmaps/04-interface-e-experiencia.md`](docs/roadmaps/04-interface-e-experiencia.md) | Taskbar, Window Manager, ícones e interação gráfica. |
| 5 | [`docs/roadmaps/05-sistema-e-ecossistema.md`](docs/roadmaps/05-sistema-e-ecossistema.md) | Dispositivos, energia, rede, atualizações e ecossistema. |

O índice desses arquivos está em [`docs/roadmaps/README.md`](docs/roadmaps/README.md).
Os documentos em `docs/melhorias futuras/` continuam sendo o backlog detalhado
de cada produto; o status técnico atual deve ser confirmado neste arquivo e nos
roadmaps por etapa.

---

## Backlog e Melhorias Futuras

O projeto conta com uma extensa lista de melhorias e novos módulos planejados, documentados na pasta `docs/melhorias futuras/`. O backlog está organizado nas seguintes categorias:

### Kernel e Sistema
- **Fundacao do Kernel** (`fundacao do kernel.md`) - etapa prioritaria para organizar as bases antes das otimizacoes
- **Atualizacao e Otimizacao do Kernel** (`atualizacao do kernel.md`) - etapa posterior, guiada por metricas
- **API de Aplicativos e Syscalls** (`api de aplicativos e syscalls.md`) - Fases 1 a 6B validadas; Fase 6C é a próxima etapa
- **Resiliência do Sistema** (`resiliencia do sistema.md`)
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
- **Explorer Moderno** (`explorer moderno.md`)
- **Painel de Configurações** (`configurações.md`)
- **Gerenciador de Processos** (`gerenciador de processos.md`)

### Novos Módulos e Aplicativos (App Store / Opcionais)
- **Gerenciador de Aplicativos (App Store)** (`gerenciador de aplicativos.md`) - App Store e pacotes completos ficam depois do loader ZAPP
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
