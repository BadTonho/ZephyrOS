# ZephyrOS - Roadmap de Desenvolvimento

Sistema operacional em C + Assembly (x86), do zero.

---

## Progresso Geral: Fase 7, K1-K4, UI1-UI7, S2.8, U1-U5 e EP1-EP3 validadas

```
Núcleo original (Fases 1–9): [████████████████████████████████████████████] 100%
Plataforma de aplicativos:   [██████████████████████████████████████████] Fase 7 validada
Interface e experiência:     [██████████████████████████████████████████] UI1-UI7 validadas
Sistema e ecossistema:       [██████████████████████████████████████████] S2.8 e U1-U5 concluídas
Evolução da plataforma:      [███████████████-----------------------------] EP1-EP3 validadas
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
| `mouse` | Mostra status ou altera preferências do mouse PS/2 em RAM |
| `storage ...` | Lista, inspeciona e monta volumes ATA adicionais somente-leitura |
| `guitest [modern]` | Testa primitivas GUI 2D e a base visual Modern |
| `guimode` | Altera entre gui simple e classic |
| `health` | Exibe metricas e estado de recovery |
| `log ...` | Consulta, configura, limpa e testa o log circular |
| `timer [status|list|check]` | Inspeciona e testa o serviço de temporizadores |
| `store` | Abre a App Store nativa; fallback TUI local no modo Simple |
| `store status|list|info` | Consulta o catalogo local da App Store |
| `store install|update|rollback|remove|run` | Planos locais, ciclo confirmado, rollback e execucao instalada |
| `store remote ...` | Consulta, armazena e aplica planos remotos autenticados manualmente |
| `devices [-v]` | Lista o inventario nativo de hardware |
| `device-info <id>` | Mostra detalhes de um dispositivo inventariado |
| `device-scan` | Refaz somente a varredura PCI e atualiza o inventario |
| `net status` | Mostra inventario e capacidades reais de rede |
| `net devices` | Lista controladores PCI de rede |
| `net info <id>` | Mostra metadados PCI de uma interface |
| `net ethernet <id>` | Inspeciona fila, parsing e contadores Ethernet L2 |
| `net test <id>` | Envia frame Ethernet pela E1000 ou RTL8139 escolhida |
| `net arp config <id> <ip>` | Vincula interface e IPv4 local em RAM |
| `net arp status` | Mostra configuracao, cache e contadores ARP |
| `net arp resolve <ip>` | Inicia ou consulta resolucao IPv4 para MAC |
| `net arp table` | Lista cache ARP, estado, idade e tentativas |
| `net arp clear` | Limpa cache e preserva a configuracao local |
| `net ipv4 config <id> <ip> <mask> <gw>` | Configura IPv4 estatico em RAM |
| `net ipv4 status` | Mostra IPv4, ICMP, rotas, perdas e RTT |
| `net udp status` | Mostra endpoints, datagramas e checksum |
| `net dhcp acquire/status/renew/release` | Gerencia configuracao dinamica |
| `net dns config/status/table/clear` | Configura DNS e inspeciona o cache |
| `net tcp status/connect` | Inspeciona TCP ou testa uma abertura ativa |
| `net socket status/table` | Inspeciona sockets nativos e filas |
| `net check qemu multi <id-a> <id-b>` | Valida isolamento de duas NICs |
| `http get <url>/status` | Executa HTTP GET limitado e mostra a sessao |
| `nslookup <dominio>` | Resolve registro DNS A cooperativamente |
| `ping <ip-ou-dominio> [quantidade]` | Executa ICMP Echo cooperativo |
| `net check [id]` | Agrupa toda a pilha de rede e invariantes |
| `net check qemu <id> <ip>` | Executa a suite ARP, IPv4 e ICMP no QEMU |
| `net check qemu dhcp <id> <dominio>` | Executa a suite UDP, DHCP e DNS |
| `net check qemu tcp <id> <dominio>` | Executa a suite TCP, sockets e HTTP |
| `acpi status` | Mostra tabelas, PM1, modo ACPI, `_S5_` e prontidao S5 |
| `power status` | Mostra prontidao S5, desligamento fisico e fallback HLT |
| `kmetrics [reset]` | Coleta linha-base de PIT, filas, memoria e VESA, incluindo media de bytes por apresentacao |
| `memcheck` | Valida heap, PMM, coalescencia e limpeza de diretorios ring 3 |
| `schedcheck` | Valida invariantes do scheduler sem alterar processos |
| `q2check` | Executa diagnostico compacto da etapa Q2 |
| `regcheck [full]` | Valida a regressao compacta; `full` soma PCI, Devices, Network, ACPI e Power |
| `appcheck` | Testa API, arquivos, IPC e carregador |
| `app run <arq> [args]` | Executa aplicativo ring 3 com argumentos simples |
| `app inputtest` | Testa teclado e foco de aplicativo ring 3 |
| `app outputtest [fail]` | Testa saida ZAPP acima de 1 KiB e codigos de saida |
| `app argtest <texto>` | Testa argumentos de aplicativo ring 3 |
| `reboot` | Reinicia o sistema |
| `shutdown` | Desliga por ACPI S5 ou usa fallback terminal HLT |

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
- [x] Habilitacao confirmada de Memory Space + Bus Master para drivers MMIO

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
- [x] Estatísticas de ticks por janela

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
- [x] Métricas de ticks, memória, espera, ATA e detalhes do processo selecionado
- [x] Atualização periódica, seleção, ordenação e ações pelo teclado
- [x] Comando `taskmgr` no shell; taskbar e Desktop usam a janela gráfica no modo Classic

### File Manager (`src/filemanager/filemanager.c`)
- [x] Interface Explorer Simple com colunas e status bar, e modo Classic com moldura, painéis e seleção gráfica
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

### GUI Classic (`src/gui/gui.c`)
- [x] Primitivas gráficas 2D (pixel-level)
- [x] gui_draw_text() - texto renderizado pixel a pixel
- [x] gui_draw_button() - botão com estado pressed
- [x] gui_draw_window_frame() - moldura de janela gráfica
- [x] Comando `guitest` no shell

### Core Enhancements
- [x] Servico R3 de espera por eventos, timeout e cancelamento, integrado ao
  canal IPC do Shell (implementado; validacao no QEMU pendente)
- [x] Logging com 4 níveis, ring estruturado e observabilidade (`src/core/log.c`)
- [x] Serviço R2 de timers canceláveis, com callbacks diferidos e piloto ICMP
  (implementado; validação no QEMU pendente)
- [x] Utilitários de string (`src/core/string.c` - kmemset, kmemcpy, kstrcmp, kstrlen)
- [x] Códigos de erro padronizados (`src/include/core/errors.h`)
- [x] Spinlock para sincronização (`src/include/core/spinlock.h`)

## Fase 10 - GUI Classic ✅
> Arquivos principais: `src/drivers/video.c`, `src/drivers/vesa.c`, `src/desktop/desktop.c`, `src/taskbar/taskbar.c`, `src/wm/wm.c`, `src/gui/gui.c`

- [x] **Desktop gráfico compatível**: Cards 3D, seleção azul, layout responsivo e fallback TUI.
- [x] **Modo explícito de interface**: Comando `guimode simple|classic`;
  `modern` fica reservado para a interface futura.
- [x] **Interação gráfica do Desktop**: Clique para selecionar e duplo clique para abrir aplicativos.
- [x] **Renderização de Textos Livres**: Substituir `video_put_char_at` por texto gráfico (desenho pixel a pixel) que possa ser renderizado em qualquer X/Y, não apenas na grade (col/row) - `gui_draw_text`.
- [x] **Aplicativos modernos**: Explorer, Task Manager e Settings usam janelas Classic Modern Dark e mantêm fallback TUI.
- [x] **Double Buffering (Backbuffer)**: Renderização no VRAM em dois estágios para prevenir *flickering* (cintilação) durante o redesenho (Vesa Flip).
- [x] **Entrada gráfica básica**: Roteamento de teclado e mouse preserva prioridade da taskbar e do Menu Iniciar.
- [x] **Taskbar Classic**: Botões, relógio e Menu Iniciar preservam sua semântica nas cinco posições suportadas.
- [x] **Primitive Graphics 2D**: Novas primitivas foram adicionadas somente para necessidades visuais comprovadas.
- [x] **Desktop Gráfico Imagens**: Shell, Explorer e Task Manager usam BMPs com cache e fallback desenhado.
- [x] **Window Manager Gráfico**: Molduras, titlebars e controles gráficos integram foco, Z-order e composição VESA.
- [x] **Integração Plena de Mouse**: Janelas suportam arraste e redimensionamento; ícones usam encaixe em grade.
- [x] **Aplicativos hospedados**: Shell, Explorer, Settings e Task Manager usam janelas singleton no modo Classic.
- [x] **Acessibilidade**: Roda PS/2 e atalhos `Alt+Tab`, `Alt+F4`, `Alt+F9` e `Alt+F10` integrados ao WM.

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

### Fase 7 - Pacotes locais e distribuicao ✅

- [x] Empacotador host para criar, verificar e injetar `.zephyrosapp` como
  alias FAT12 `ID.ZPK`.
- [x] Container `ZPKG` v1 com manifesto, versao, arquitetura i386, CRC32 e
  uma imagem ZAPP validada antes da escrita.
- [x] Servico interno `PKG`, comandos `pkg`, `pkgcheck` e instalacao em
  `APPS/<ID>/APP.ZAP` mais `META.DAT`.
- [x] Validado no host e QEMU: pacote demo, remocao, caminho instalado, F12,
  diagnosticos, interfaces Simple/Classic e ausencia de processos residuais.

### AS1 - Catalogo local e observabilidade (validada no QEMU)

- [x] Snapshot somente-leitura de fontes `.ZPK` e pacotes instalados, com
  ordem deterministica, estados, motivos e capacidades.
- [x] Comandos `store status|list|info`, recovery e integracao com `health`.
- [x] Fixtures publicos, auditor host e alvos `store-test`/`store-demo`.
- [x] Build limpo e matriz QEMU aprovados: ordem e motivos deterministas,
  transicao `AVAILABLE -> SAME_VERSION -> AVAILABLE`, memoria estavel,
  `appcheck`, `memcheck` e `regcheck full` em `OK`, sem processos residuais.

### AS2 - Ciclo de vida local (validada no QEMU)

- [x] Preflights sem escrita, motivos estaveis, espaco e bloqueadores por ID.
- [x] Gate unico para mutacoes `pkg`/`store` e repeticao apos `--confirm`.
- [x] Comandos `store install|remove|run` com workspace estatico no Shell.
- [x] Fixtures separados `WAITAPP`, `BASE` e `DEPEND`, auditor e alvos AS2.
- [x] Autoteste, auditorias AS1/AS2, `q3check` e `diff --check`.
- [x] Build limpo e matriz QEMU aprovados: preflights sem escrita, confirmacao
  explicita, bloqueio por dependencia, execucao e cancelamento por `F12`,
  memoria estavel em `20680 KB` e nenhuma aplicacao ou processo residual.

### AS3 - App Store nativa (concluida e validada)

- [x] Janela singleton Classic com visual Modern Dark e fallback TUI Simple.
- [x] Catalogo, instalados, detalhes, confirmacao contextual e worker
  cooperativo sobre os contratos AS1/AS2.
- [x] Integracao no Menu Iniciar, IPC, Window Manager e comando `store`.
- [x] Host e matriz QEMU validados: catalogo valido/invalido, dependencias,
  confirmacao, instalacao/remocao, `WAITAPP`/`F12`, foco, singleton, Simple e
  diagnosticos finais sem residuos.

### AS4 - Atualizacao local transacional (concluida e validada)

- [x] Planos topologicos locais, update/downgrade confirmado e dependencias
  transitivas ausentes no FAT12.
- [x] Staging, journal redundante, recuperacao no boot, rollback manual e
  historico compacto sem alterar ZPKG, App API ou loader.
- [x] Store Classic/Simple e Shell com update, rollback, historico, failpoint
  e fixtures seed/update `UPTARGET`/`UPDEPA`/`UPDEPB`.
- [x] Host, build e matriz QEMU AS4 pelo usuario, incluindo planos,
  update/downgrade, rollback, failpoints com recuperacao e interface Classic.

### AS5 - Repositorio remoto autenticado (concluida e validada)

- [x] Servico publico `app_remote`, catalogo `ZAC1`, Ed25519, SHA-256 por
  pacote e tabela de confianca de teste separada da ZUPD.
- [x] Opt-in por sessao, consulta manual, planejador remoto isolado das fontes
  locais e cache FAT12 A/B com recuperacao do slot pendente.
- [x] Integracao com a transacao AS4 para instalacao/update offline, rollback,
  historico, procedencia e gate unico de mutacao.
- [x] Comandos `store remote`, aba Remoto Classic, cancelamento e failpoint;
  Simple permanece congelado e usa o Shell como fallback remoto.
- [x] Fixtures seed/update e negativos, chave publica, auditoria do empacotador,
  Q3Check e alvos `store-as5-*`, sem chave privada versionada.
- [x] Gates host, build e matriz QEMU AS5 executados pelo usuario, incluindo
  autenticacao, cache A/B, instalacao offline, persistencia, interface Classic,
  recuperacao e diagnosticos de integridade.

### Q4 - Regressao compacta ✅

- [x] `regcheck` concentra health, processos, servicos, scheduler, memoria,
  pacotes, threads e dois ciclos ZAPP silenciosos, incluindo cancelamento real
  por `F12`.
- [x] Validado no host com `make q3check` e build limpo; no QEMU confirmou
  `RegCheck: OK`, uso invalido controlado, um prompt e ausencia de processos,
  zumbis e diretorios de usuario residuais.

### Etapas posteriores da plataforma

- [ ] Argumentos mais completos, incluindo aspas e escapes.
- [ ] Serviços adicionais da App API conforme a necessidade dos aplicativos migrados.
- [ ] Avaliar permissões e isolamento adicional somente após métricas e testes de estabilidade.

---

## S1.1 - Servicos observaveis de sistema (concluida)

- [x] Inventario somente de leitura de PCI, ATA, AC97, PS/2, PIT, VGA, VESA e
  PC Speaker, acessivel por `devices`, `device-info` e `device-scan`.
- [x] Diagnostico `power status` com estados reais: S0 e idle HLT/C1 ativos,
  S1-S4 indisponiveis sem ACPI e S5 explicitamente simulado.
- [x] Componentes `Devices` e `Power` no `health`, com degradacao controlada
  para inventario PCI parcial.
- [x] Build limpo e matriz manual no QEMU validados pelo usuario: inventario
  compacto e verboso, consulta por ID, re-scan PCI, diagnostico de energia,
  `health` e regressao sem erros bloqueantes.

## S1.2 - Fundacao ACPI observavel (concluida)

- [x] Descoberta somente de leitura de RSDP, RSDT/XSDT, FADT, DSDT e FACS,
  validada contra o mapa E820 antes da ativacao do paging.
- [x] Snapshot estatico limitado, consultas por copia e componente `ACPI` no
  `health`, com estados pronto, degradado e indisponivel.
- [x] Comandos `acpi status` e `power status` distinguem tabelas detectadas de
  transicoes realmente implementadas.
- [x] Build limpo e QEMU validados pelo usuario: ACPI completo no cenario
  padrao, fallback sem ACPI, sintaxe invalida e matriz de regressao sem erros.

## S1.3 - Preparacao observavel do S5 (concluida)

- [x] FADT ampliada com snapshot normalizado de PM1a/PM1b, SMI_CMD, valores
  enable/disable, comprimento PM1 e indicador hardware-reduced.
- [x] Modo ACPI observado por leitura segura de `SCI_EN`, sem habilitar ACPI
  nem escrever em registradores de energia.
- [x] Reconhecedor AML limitado para `_S5_`, com validacao de pacote,
  constantes, limites, ambiguidade e fallback fechado.
- [x] `acpi status` e `power status` separam firmware observado de transicoes
  implementadas; S5 e `shutdown` continuam simulados.
- [x] Build e validacao manual concluidos no QEMU padrao e sem ACPI:
  `health`, PM1, `_S5_`, fallback, comandos diagnosticos, entrada ZAPP e
  matriz de regressao Simple/Classic permaneceram operacionais.

## S1.4 - Desligamento fisico ACPI S5 (concluida)

- [x] Prontidao S5 fechada por snapshot completo, FADT/DSDT, PM1 System I/O,
  `_S5_` inequivoco e modo ACPI habilitado ou ativavel.
- [x] `acpi_enter_s5()` adquire o modo por `SMI_CMD` quando necessario,
  confirma `SCI_EN` e escreve PM1a antes de PM1b sem caminho de retorno apos
  a primeira escrita.
- [x] `power_shutdown()` centraliza Shell, kernel, Menu Iniciar e Task Manager,
  para audio em best effort e usa `CLI+HLT` como fallback terminal.
- [x] Porta privada `0xB004` do QEMU removida; reboot, filesystem, boot e
  paging permanecem inalterados.
- [x] Validacao manual concluida no QEMU padrao: `health`, `acpi status`,
  `power status`, `memcheck`, `schedcheck`, inventario, re-scan PCI,
  `regcheck`, entrada ZAPP, sintaxe invalida e desligamento fisico real.
- Cobertura complementar recomendada: repetir o fallback sem ACPI e os
  atalhos de desligamento pelos menus Simple/Classic em VMs separadas.

## S2.1 - Fundacao de rede observavel (concluida)

- [x] Snapshot estatico de ate quatro controladores PCI de classe `0x02`,
  somente por copia e sem acessar BARs ou modificar configuracao.
- [x] E1000 `8086:100E` e RTL8139 `10EC:8139` reconhecidas como candidatas,
  mantendo driver, link, RX/TX e IPv4 explicitamente indisponiveis.
- [x] Componente `Network` integrado ao `health`, degradado quando existe NIC
  sem driver e desabilitado quando nenhum controlador e encontrado.
- [x] Comandos `net status`, `net devices` e `net info <id>`, com IDs estaveis
  e sintaxe invalida controlada.
- [x] `device-scan` atualiza tambem o snapshot de rede sem iniciar driver.
- [x] `regcheck full` concentra a varredura PCI e os contratos de Devices,
  Network, ACPI e Power, mantendo o cancelamento real por `F12`.
- [x] Validada manualmente com `regcheck full` no QEMU padrao e com
  `-nic none`; ambos concluiram em `RegCheck: OK` apos o cancelamento por
  `F12`.

## S2.2 - Driver E1000 Ethernet L2 (implementada e validada)

- [x] Driver para Intel `8086:100E` com BAR0 MMIO de 32 bits, Memory Space,
  bus mastering, reset limitado por PIT, MAC RAL/RAH e IRQ PCI legado.
- [x] Filas DMA PMM de oito descritores RX/TX, buffers de 2 KiB, contadores,
  link observavel, descarte RX controlado e transmissao L2 de frames ate 1518
  bytes.
- [x] `network_manager`, `health`, `net status`, `net devices` e `net info`
  integram MAC, estados, contadores e ultimo erro sem alterar IDs estaveis.
- [x] `net test <id>` envia um unico frame broadcast `0x88B5` sob demanda;
  IPv4, ARP, DHCP, sockets e RTL8139 continuam fora do escopo.
- [x] Validada pelo usuario: `make q3check`, build limpo, QEMU padrao com
  TX, `device-scan`, `regcheck full` e sintaxe invalida; tambem sem NIC e
  com RTL8139, ambos com falhas controladas e `RegCheck: OK`.

## S2.3 - Camada Ethernet e entrega de RX (concluida e validada)

- [x] Criar uma API de recepcao por fila fixa ou polling entre o E1000 e a
  camada Ethernet; a IRQ continuara curta e nao executara ARP/IP diretamente.
- [x] Validar, montar e analisar cabecalhos Ethernet, incluindo unicast para
  a MAC local e broadcast, sobre uma abstracao minima de interface de rede.
- [x] Expor observabilidade e comando Shell para inspecionar o fluxo Ethernet
  sem transmitir automaticamente no boot.
- [x] Manter ARP, IPv4, DHCP, DNS, sockets, servicos remotos e RTL8139 fora
  desta entrega.
- [x] Validada pelo usuario com Q3, build limpo e QEMU padrao: camada ativa,
  polling ocioso zerado, TX contabilizado no driver e na camada,
  `device-scan` aprovado e `regcheck full` concluido em `OK`.
- [ ] Cobertura complementar: injetar RX externo e frame invalido e repetir a
  regressao visual Simple/Classic; esses cenarios nao bloqueiam a conclusao.

## S2.4 - ARP com cache e resolucao assincrona (concluida e validada)

- [x] Despacho Ethernet por EtherType com quatro handlers e contadores de
  entrega, ausencia de protocolo e erro do callback.
- [x] Requests, replies automaticos e serializacao ARP explicita em ordem de
  rede, processados fora da IRQ.
- [x] Cache fixo de 32 entradas, tres tentativas em tres segundos, timeout,
  expiracao em 30 segundos e substituicao sem remover pendencias.
- [x] Configuracao de uma interface/IPv4 em RAM e resolucao IP para MAC sem
  bloquear o Shell.
- [x] Comandos `net arp`, estado no Network e invariantes somente-leitura no
  `regcheck full`.
- [x] Suite `net check qemu <id> <ip>` agrupa reply, cache hit e timeout sem
  impedir o polling cooperativo do processo de sistema.
- [x] Validada pelo usuario no QEMU padrao: request/reply, cache hit sem novo
  TX, tres tentativas, timeout, polling sem erros e invariantes concluiram em
  `OK`.
- [ ] Cobertura complementar: peer externo, ausencia de NIC, RTL8139,
  `device-scan`, sintaxe invalida e regressao visual Simple/Classic; esses
  cenarios nao bloqueiam a conclusao.

## S2.5 - IPv4 estatico e ICMP Echo (concluida e validada)

- [x] IPv4 minimo com cabecalho fixo, MTU 1500, checksum, DF, TTL 64,
  roteamento direto/gateway e despacho fixo por protocolo.
- [x] Configuracao estatica unica em RAM, coordenada atomicamente com ARP e
  cancelamento de ICMP nas mudancas.
- [x] ICMP Echo Request/Reply, resposta automatica, sessao unica de ping,
  timeout por tentativa e RTT por ticks do PIT.
- [x] Comandos individuais `net ipv4 config`, `net ipv4 status` e `ping`,
  preservando o diagnostico agrupado `net check`.
- [x] Suite `net check qemu <id> <ip>` ampliada com IPv4, checksum, Echo Reply,
  RTT, polling e invariantes.
- [x] `regcheck full` ampliado com invariantes e vetores puros de checksum sem
  transmitir ou alterar configuracao/cache.
- [x] Validada pelo usuario no QEMU padrao: ARP reply, cache hit, timeout,
  IPv4 RX/TX, checksum, ICMP Echo, RTT, polling e invariantes concluiram em
  `OK`; o `ping` individual recebeu quatro de quatro replies, sem perdas.
- [ ] Cobertura complementar: ausencia de NIC, RTL8139, peer externo,
  entradas IPv4 malformadas e regressao visual Simple/Classic; esses
  cenarios nao bloqueiam a conclusao.

## S2.6 - UDP, DHCP e DNS (concluida e validada)

- [x] UDP com 16 endpoints fixos, pseudo-checksum, callbacks sincronas e
  broadcast limitado reservado ao bootstrap DHCP.
- [x] DHCP Discover/Offer/Request/ACK, retentativas, T1/T2, NAK, expiracao,
  renovacao e release, preservando IPv4 estatico ate um ACK valido.
- [x] DNS A/IN com uma consulta ativa, nomes comprimidos, CNAME limitado,
  cache de 16 entradas e expiracao por TTL.
- [x] Comandos individuais de UDP, DHCP e DNS, `nslookup`, `ping` por nome e
  suite agrupada `net check qemu dhcp <id> <dominio>`.
- [x] Invariantes e vetores puros integrados ao `regcheck full`.
- [x] Validada pelo usuario no QEMU padrao: UDP RX/TX e checksum, ciclo
  Discover/Offer/Request/ACK, lease, gateway, DNS A, cache sem novo TX,
  IPv4/ICMP, polling e invariantes concluiram em `OK`.
- [ ] Cobertura complementar: renovacao/liberacao individual, expiracao de
  cache, ausencia de NIC, RTL8139, peer externo e regressao visual
  Simple/Classic; esses cenarios nao bloqueiam a conclusao.

## S2.7 - TCP cliente, sockets do kernel e HTTP GET (concluida)

- [x] TCP cliente com 16 conexoes, handles geracionais, handshake, dados,
  FIN/RST, checksum, MSS 536, janela RX e descarte fora de ordem.
- [x] RTO adaptativo com SRTT/RTTVAR, Karn, backoff e tres retransmissoes;
  espera por ARP nao inicia o temporizador TCP.
- [x] Sockets `STREAM` nativos com 16 entradas, fila TX de 2 KiB e ring RX
  de 4 KiB, sem ABI de userspace.
- [x] HTTP GET para `http://`, DNS/IP literal, headers limitados e corpo de
  16 KiB por `Content-Length` ou EOF.
- [x] Comandos `net tcp`, `net socket`, `http` e suite agrupada
  `net check qemu tcp <id> <dominio>`.
- [x] Invariantes e vetores puros de TCP/HTTP integrados ao `regcheck full`.
- [x] Suite QEMU principal validada pelo usuario: DHCP, DNS, handshake,
  dados, checksum, sockets, HTTP, fechamento, polling e invariantes em `OK`.
- [x] Entrada Shift e integridade da pilha/heap revalidadas pelo usuario:
  `echo A:B?` funcionou e `regcheck full` permaneceu em `OK` antes e depois
  do GET e da suite agrupada.
- [x] GET individual revalidado com `neverssl.com`: HTTP 302, headers e
  corpo recebidos, seguido por `regcheck full` em `OK`.
- [x] Suite agrupada revalidada com repeticao controlada: o primeiro
  handshake expirou, a tentativa 2/3 concluiu HTTP 200 e todos os itens,
  inclusive o `regcheck full` posterior, ficaram em `OK`.
- [x] Fallback sem NIC validado: interface inexistente foi recusada sem
  trafego e o `regcheck full` permaneceu em `OK`.
- [ ] Cobertura complementar: peer controlado, perda/retransmissao, janela
  zero, RST, tabela cheia, RTL8139 e Simple/Classic.

## Continuacao da S2 - Rede e atualizacoes

- [x] S2.8: Multi-NIC/RTL8139 concluida, com registro de quatro interfaces,
  E1000 multi-instancia, IRQ compartilhada, uma unica NIC L3 e suite
  `net check qemu multi`; Q3, build e matriz QEMU foram aprovados pelo usuario.
- [x] U1: politica de integridade e contrato ZUPD v1 concluidos, com layout
  autenticado Ed25519/SHA-256 e vetores para sucesso, corrupcao, chave
  desconhecida e versao incompativel.
- [x] U2 concluida: ferramenta host, raiz publica de release, parser
  somente-leitura, SHA-2/Ed25519, `update verify`, `health` e sete fixtures
  validados. Build limpo, motivos esperados, memoria estavel, imagem sem
  alteracao e `regcheck full` em `OK` foram confirmados pelo usuario.
- [x] U3 concluida: copy-on-write FAT12, estado/journal redundantes,
  aplicacao, recuperacao no boot, rollback, failpoint, `APPLY.ZUP` e auditor
  offline foram validados. Os cenarios de sucesso, rollback e interrupcao
  recuperavel passaram no QEMU; `regcheck full`, memoria e `audit-image`
  confirmaram versao `0.1.0`, journal limpo e rollback consumido.
- [x] U4 concluida: `update status`, `update history`, controles `ZUH1`,
  auditoria host e System Updater Simple/Classic foram validados. Aplicacao,
  rollback e failpoint passaram no QEMU; a recuperacao preservou `0.1.0`,
  registrou quatro eventos, deixou o journal limpo e terminou com
  `regcheck full` e `audit-image` em `OK`.
- [x] U5 concluida: manifesto assinado `ZUM1`, HTTP streaming, cache FAT12
  redundante, comandos e aba Remoto foram validados no Shell e no System
  Updater Classic. DHCP automatico, opt-in por sessao, consulta sem escrita,
  alternancia `ZUR0/1.ZUP`, autenticacao, preservacao do cache, timeout/retry,
  cancelamento, aplicacao local, rollback, `regcheck full` e `audit-image`
  passaram no QEMU. O Simple permanece como fallback implementado, com
  regressao complementar que nao bloqueia a fase.

## Roadmaps por etapa

O roadmap principal mantém a visão geral. Os próximos trabalhos executáveis foram
separados por dependência para não misturar estabilização, plataforma de apps,
kernel, interface e novos serviços:

| Ordem | Documento | Objetivo |
|-------|-----------|----------|
| 1 | [`docs/roadmaps/01-estabilizacao-e-qualidade.md`](docs/roadmaps/01-estabilizacao-e-qualidade.md) | Regressão, diagnóstico e fallbacks antes de novas migrações. |
| 2 | [`docs/roadmaps/02-plataforma-de-aplicativos.md`](docs/roadmaps/02-plataforma-de-aplicativos.md) | Fase 7, pacotes locais, ZAPP e distribuicao. |
| 3 | [`docs/roadmaps/03-kernel-e-desempenho.md`](docs/roadmaps/03-kernel-e-desempenho.md) | Métricas, scheduler, memória e otimização baseada em evidências. |
| 4 | [`docs/roadmaps/04-interface-e-experiencia.md`](docs/roadmaps/04-interface-e-experiencia.md) | Taskbar, Window Manager, ícones e interação gráfica. |
| 5 | [`docs/roadmaps/05-sistema-e-ecossistema.md`](docs/roadmaps/05-sistema-e-ecossistema.md) | Dispositivos, energia, rede, atualizações e ecossistema. |
| 6 | [`docs/roadmaps/06-app-store.md`](docs/roadmaps/06-app-store.md) | Catalogo local, ciclo de vida, App Store Modern e distribuicao futura. |
| 7 | [`docs/roadmaps/07-modernizacao-visual.md`](docs/roadmaps/07-modernizacao-visual.md) | Escala acessivel, visual flat/dark e desempenho VESA mensuravel; MV4 funcionalmente validado, comparacao historica N/D. |
| 8 | [`docs/roadmaps/08-evolucao-da-plataforma.md`](docs/roadmaps/08-evolucao-da-plataforma.md) | EP1, EP2 e EP3 validadas. |
| 9 | [`docs/roadmaps/09-funcionalidades-aplicaveis.md`](docs/roadmaps/09-funcionalidades-aplicaveis.md) | Logs, timers, espera, work queue, dispositivos, I/O, cache e métricas do scheduler. |

Os numeros 06 e 07 identificam os documentos, nao uma barreira de conclusao
integral. A ordem executavel compartilhada e **AS1-AS2 -> MV0-MV3 -> AS3 ->
MV4 -> AS4-AS5**. Dessa forma, catalogo e ciclo de vida sao estabilizados
primeiro, a fundacao visual e definida em seguida e a App Store Modern ja
nasce no novo padrao, sem uma segunda refatoracao imediata.

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
- **API de Aplicativos e Syscalls** (`api de aplicativos e syscalls.md`) - Fases 1 a 7 validadas; pacotes locais `ZPKG` v1 disponiveis
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
- **Explorer Classic** (`explorer moderno.md`)
- **Painel de Configurações** (`configurações.md`)
- **Gerenciador de Processos** (`gerenciador de processos.md`)

### Novos Módulos e Aplicativos (App Store / Opcionais)
- **Gerenciador de Aplicativos (App Store)** (`gerenciador de aplicativos.md`) - AS1-AS5 concluidos e validados
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
