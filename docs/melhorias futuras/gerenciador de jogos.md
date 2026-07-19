# Gerenciador de Jogos — ZephyrOS v0.1

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Infraestrutura de jogos | 24 | 6 | 0 | 18 |
| 2. Game Launcher (Biblioteca) | 52 | 0 | 0 | 52 |
| 3. Game Overlay (HUD) | 48 | 0 | 0 | 48 |
| 4. Performance Monitoring | 44 | 0 | 0 | 44 |
| 5. Game Recording & Screenshots | 38 | 0 | 0 | 38 |
| 6. Controller Support | 36 | 0 | 0 | 36 |
| 7. Interface TUI do Gerenciador | 56 | 0 | 0 | 56 |
| **TOTAL** | **298** | **6** | **0** | **292** |

**Progresso geral: 2%** (6/298 itens completos)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F8 | Abrir/fechar Game Overlay |
| F12 | Capturar screenshot |
| Ctrl+F12 | Gravar vídeo (toggle) |
| Shift+F12 | Abrir biblioteca de jogos |
| Setas | Navegar na interface |
| Enter | Selecionar/iniciar jogo |
| Esc | Fechar overlay/janela |
| Tab | Alternar entre seções |
| +/- | Ajustar opacidade do overlay |

---

## Fase 1: Infraestrutura de Jogos ✅

### 1.1 Timer e Performance

- [x] PIT Timer a 50 Hz para medição de tempo — `timer.c:10-17`
- [x] API `timer_get_ticks()` para contagem — `timer.c:24-26`
- [x] Cálculo de uptime — `shell.c:232-245`
- [ ] Criar função `game_get_fps()` para FPS (usando timer)
- [ ] Criar função `game_get_frame_time()` para tempo por frame
- [ ] Criar função `game_get_delta_time()` para deltaTime
- [ ] Criar array `fps_history[300]` para histórico (5 minutos)

### 1.2 Memória e Alloc

- [x] Heap allocator (first-fit) — `memory.c:138-201`
- [x] API `kmalloc()` / `kfree()` — `memory.c:174-202`
- [x] Page allocator — `memory.c:85-136`
- [ ] Criar pool allocator para structs de jogos (fixed-size blocks)
- [ ] Criar buffer ring para gravação de vídeo
- [ ] Criar função `game_alloc_aligned(size, alignment)` para DMA

### 1.3 Áudio

- [x] AC97 driver completo — `ac97.c:1-197`
- [x] Play/Stop/Volume — `ac97.c:126-179`
- [x] PC Speaker beep — `speaker.c:1-55`
- [ ] Criar混音器 (mixer) para múltiplos canais de áudio
- [ ] Criar função `game_play_sound(sound)` para efeitos sonoros
- [ ] Criar função `game_play_music(track)` para música de fundo
- [ ] Criar suporte a WAV (já parcialmente existe)

### 1.4 Video

- [x] VGA Text Mode — `video.c:1-170`
- [x] VESA Framebuffer — `vesa.c:1-392`
- [x] Primitivas gráficas — `vesa.c:234-392`
- [ ] Criar framebuffer duplo (double buffering) para jogos
- [ ] Criar função `game_clear_screen()` para limpar buffer
- [ ] Criar função `game_flip_buffers()` para trocar buffers
- [ ] Criar suporte a sprites (carregar de BMP)
- [ ] Criar suporte a animações (sprite sheets)

### 1.5 Input

- [x] Teclado PS/2 com callbacks — `keyboard.c:1-53`
- [x] Sistema de callbacks dinâmicos — `keyboard.c:49-53`
- [ ] Criar API de input para jogos (key states: pressed, released, held)
- [ ] Criar função `game_is_key_pressed(key)` para verificar tecla
- [ ] Criar função `game_is_key_just_pressed(key)` para tecla recém-pressionada
- [ ] Criar polling de input (não apenas interrupts)
- [ ] Criar suporte a gamepad via serial (futuro)

---

## Fase 2: Game Launcher (Biblioteca)

### 2.1 Sistema de Jogos

- [ ] Criar módulo `src/games/gamemanager.c` e `gamemanager.h`
- [ ] Criar struct `game_t`:
  ```
  - id              = uint32_t
  - name[64]        = "Super Mario"
  - executable[32]  = "Mario.exe"
  - icon_path[64]   = "/games/mario/icon.bmp"
  - category        = enum (PLATFORMER, PUZZLE, ACTION, ADVENTURE, RPG, UTILITY)
  - status          = enum (INSTALLED, NOT_INSTALLED, UPDATING)
  - install_path[64]= "/games/mario/"
  - version[16]     = "1.0.0"
  - size_kb         = uint32_t
  - last_played     = uint32_t (timestamp)
  - play_time       = uint32_t (seconds)
  - rating          = uint8_t (1-5 stars)
  - favorite        = bool
  ```
- [ ] Criar array `games[MAX_GAMES]` (máximo 32 jogos)
- [ ] Criar função `gamemanager_init()` para carregar lista
- [ ] Criar função `gamemanager_add(game)` para adicionar jogo
- [ ] Criar função `gamemanager_remove(id)` para remover jogo
- [ ] Criar função `gamemanager_find(name)` para buscar por nome
- [ ] Criar função `gamemanager_list()` para listar todos
- [ ] Criar função `gamemanager_get_recent()` para jogos recentes
- [ ] Criar função `gamemanager_get_favorites()` para favoritos

### 2.2 Formato de Jogo

- [ ] Criar formato `.zephyrosgame` para pacotes de jogos
- [ ] Criar struct `game_package_t`:
  ```
  - magic[4]        = "MGME"
  - name[64]        = "Super Mario"
  - version[16]     = "1.0.0"
  - description[256]= "Descrição do jogo"
  - author[32]      = "Nome do autor"
  - category        = uint8_t
  - icon_data[]     = BMP icon (optional)
  - executable[]    = ELF binary
  - assets[]        = Sprites, sounds, music
  ```
- [ ] Criar função `game_package_open(path)` para ler pacote
- [ ] Criar função `game_package_extract(package, dest_dir)` para extrair
- [ ] Criar função `game_package_create(files, output)` para empacotar (host tool)

### 2.3 Game Library UI

- [ ] Criar janela "Biblioteca de Jogos"
- [ ] Mostrar jogos em grid com ícones (BMP 32×32)
- [ ] Mostrar nome, categoria, rating, último jogo
- [ ] Filtrar por: Todos, Instalados, Favoritos, Recentes
- [ ] Ordenar por: Nome, Data, Rating, Tamanho
- [ ] Busca por nome (digitação incremental)
- [ ] Detalhes ao selecionar (descrição, screenshots, stats)
- [ ] Botão "Jogar" para iniciar jogo
- [ ] Botão "Instalar" para jogos não instalados
- [ ] Botão "Remover" para desinstalar
- [ ] Botão "Favoritar" para marcar como favorito

### 2.4 Game Details Screen

- [ ] Criar tela de detalhes do jogo
- [ ] Mostrar: Nome, Descrição, Autor, Versão, Tamanho
- [ ] Mostrar: Categoria, Rating, Último jogo, Tempo total
- [ ] Mostrar: Screenshots (se disponíveis)
- [ ] Mostrar: Requisitos (mínimo: RAM, CPU)
- [ ] Botão "Jogar" (iniciar)
- [ ] Botão "Configurar" (opções do jogo)
- [ ] Botão "Desinstalar"
- [ ] Botão "Voltar" para biblioteca

### 2.5 Integração com Shell

- [ ] Comando `games` — abrir biblioteca
- [ ] Comando `games list` — listar jogos instalados
- [ ] Comando `games add <path>` — instalar jogo
- [ ] Comando `games remove <name>` — desinstalar jogo
- [ ] Comando `games play <name>` — iniciar jogo
- [ ] Comando `games info <name>` — mostrar detalhes
- [ ] Comando `games search <query>` — buscar jogos

---

## Fase 3: Game Overlay (HUD)

### 3.1 Overlay System

- [ ] Criar módulo `src/games/gameoverlay.c` e `gameoverlay.h`
- [ ] Criar overlay independente do WM (desenha sobre tudo)
- [ ] Criar buffer de overlay (VESA: 320×100 pixels ou VGA: 40×6 chars)
- [ ] Criar função `overlay_show()` para ativar
- [ ] Criar função `overlay_hide()` para desativar
- [ ] Criar função `overlay_toggle()` para ligar/desligar
- [ ] Criar função `overlay_update()` para redesenhar
- [ ] Integrar com F8 para toggle

### 3.2 HUD Elements

- [ ] Criar elemento "FPS Counter" (canto superior esquerdo)
  - Formato: "FPS: XX"
  - Cor: verde (>60), amarelo (30-60), vermelho (<30)
  - Posição configurável
- [ ] Criar elemento "CPU Usage" (abaixo do FPS)
  - Formato: "CPU: XX%"
  - Barra de progresso mini
- [ ] Criar elemento "RAM Usage" (abaixo do CPU)
  - Formato: "RAM: XX/XX MB"
  - Barra de progresso mini
- [ ] Criar elemento "Frame Time" (canto superior direito)
  - Formato: "XX.X ms"
  - Gráfico de barras últimos 60 frames
- [ ] Criar elemento "Timer" (centro superior)
  - Formato: "HH:MM:SS" (tempo de jogo)
- [ ] Criar elemento "Notifications" (canto inferior)
  - Toast notifications para achievements, alerts

### 3.3 Game Bar

- [ ] Criar barra de ferramentas no topo (aparece com F8)
- [ ] Botões: Screenshot, Gravar, Configurações, Biblioteca, Sair
- [ ] Indicadores: FPS, CPU%, RAM%, Tempo
- [ ] Volume slider (se suportado)
- [ ] Botão de pause (para jogos single-thread)
- [ ] Acesso rápido a configurações do jogo

### 3.4 Achievements System

- [ ] Criar sistema de conquistas (achievements)
- [ ] Criar struct `achievement_t`:
  ```
  - id              = uint32_t
  - name[64]        = "First Blood"
  - description[128]= "Derrote o primeiro inimigo"
  - icon_path[64]   = "/achievements/first_blood.bmp"
  - unlocked        = bool
  - unlock_time     = uint32_t (timestamp)
  ```
- [ ] Criar função `achievement_unlock(id)` para desbloquear
- [ ] Criar função `achievement_check(id, condition)` para verificar
- [ ] Criar notificação toast ao desbloquear
- [ ] Criar tela "Conquistas" no gerenciador
- [ ] Salvar conquistas em arquivo `/achievements.dat`

---

## Fase 4: Performance Monitoring

### 4.1 FPS Tracking

- [ ] Criar array `fps_history[300]` para 5 minutos de dados
- [ ] Criar função `fps_record(frame_time)` para registrar frame
- [ ] Criar função `fps_get_avg(seconds)` para média
- [ ] Criar função `fps_get_min(seconds)` para mínimo
- [ ] Criar função `fps_get_max(seconds)` para máximo
- [ ] Criar função `fps_get_percentile(p)` para percentil (90%, 99%)
- [ ] Criar gráfico de FPS no overlay (últimos 60 segundos)
- [ ] Criar gráfico de frame time no overlay

### 4.2 CPU Monitoring

- [ ] Criar função `perf_get_cpu_usage()` (já parcialmente existe)
- [ ] Criar tracking de CPU por processo do jogo
- [ ] Criar função `perf_get_cpu_time(game_id)` para tempo de CPU
- [ ] Criar alerta se CPU > 90% por mais de 5 segundos
- [ ] Mostrar CPU% no overlay durante jogo

### 4.3 Memory Monitoring

- [ ] Criar função `perf_get_ram_usage()` (já existe)
- [ ] Criar tracking de alocações por jogo
- [ ] Criar função `perf_get_ram_used(game_id)` para RAM do jogo
- [ ] Criar alerta se RAM > 80% do total
- [ ] Mostrar RAM% no overlay durante jogo

### 4.4 Disk I/O Monitoring

- [ ] Criar tracking de leituras/escritas por jogo
- [ ] Criar função `perf_get_disk_reads(game_id)` 
- [ ] Criar função `perf_get_disk_writes(game_id)`
- [ ] Criar função `perf_get_disk_activity()` retorna se disco ativo
- [ ] Mostrar atividade do disco no overlay

### 4.5 Performance Report

- [ ] Criar função `perf_save_report(game_id, filepath)` para salvar relatório
- [ ] Incluir: FPS avg/min/max, CPU avg, RAM peak, play time
- [ ] Criar tela "Relatório de Performance" no gerenciador
- [ ] Criar comando shell `game-perf <name>` para mostrar stats
- [ ] Criar gráfico de barras comparativo entre jogos

---

## Fase 5: Game Recording & Screenshots

### 5.1 Screenshot System

- [ ] Criar módulo `src/games/screenshot.c` e `screenshot.h`
- [ ] Criar função `screenshot_capture()` para capturar tela atual
- [ ] Criar função `screenshot_save(path, format)` para salvar
- [ ] Suportar formatos: BMP (sem compressão), TGA (simple)
- [ ] Criar diretório `/screenshots/` para armazenar
- [ ] Nomear automaticamente: `screenshot_YYYY-MM-DD_HH-MM-SS.bmp`
- [ ] Criar atalho F12 para capturar
- [ ] Criar notificação "Screenshot salvo" no overlay
- [ ] Criar comando shell `screenshot` para capturar

### 5.2 Video Recording

- [ ] Criar módulo `src/games/record.c` e `record.h`
- [ ] Criar buffer ring para frames (usando VESA framebuffer)
- [ ] Criar função `record_start()` para iniciar gravação
- [ ] Criar função `record_stop()` para parar e salvar
- [ ] Criar função `record_toggle()` para ligar/desligar
- [ ] Salvar como sequência de BMPs ou formato simples
- [ ] Limitar a 30 segundos (ou memória disponível)
- [ ] Criar atalho Ctrl+F12 para toggle gravação
- [ ] Criar indicador "REC" no overlay (vermelho piscando)
- [ ] Criar comando shell `record start/stop` para controle

### 5.3 Replay System

- [ ] Criar sistema de replay (gravar input + estado)
- [ ] Criar função `replay_start()` para iniciar
- [ ] Criar função `replay_stop()` para parar
- [ ] Criar função `replay_play()` para reproduzir
- [ ] Salvar em arquivo `.replay` (input commands + timestamps)
- [ ] Limitar a 60 segundos de replay
- [ ] Criar atalho Shift+F12 para replay

---

## Fase 6: Controller Support

### 6.1 Gamepad Detection

- [ ] Detectar gamepad via portas legacy (0x200-0x207)
- [ ] Criar função `gamepad_detect()` para verificar presença
- [ ] Criar struct `gamepad_state_t`:
  ```
  - connected       = bool
  - buttons[16]     = bool (A, B, X, Y, LB, RB, LT, RT, Back, Start, L3, R3, DPad)
  - left_x, left_y  = int8_t (-128 to 127)
  - right_x, right_y= int8_t (-128 to 127)
  ```
- [ ] Criar função `gamepad_read(state)` para ler estado
- [ ] Criar polling a 60 Hz (via timer ou custom IRQ)

### 6.2 Input Mapping

- [ ] Criar sistema de mapeamento de botões
- [ ] Criar struct `game_input_t`:
  ```
  - move_up, move_down, move_left, move_right = bool
  - action_a, action_b, action_x, action_y = bool
  - shoulder_l, shoulder_r = bool
  - trigger_l, trigger_r = bool
  - start, select = bool
  ```
- [ ] Criar função `game_map_button(physical, logical)` para mapear
- [ ] Criar perfil de mapeamento padrão (Xbox-style)
- [ ] Salvar/perfil customizado em arquivo
- [ ] Criar tela "Configurar Controles" no gerenciador

### 6.3 Gamepad API

- [ ] Criar função `gamepad_is_connected()` retorna bool
- [ ] Criar função `gamepad_get_state()` retorna estado atual
- [ ] Criar função `gamepad_is_button_pressed(button)` retorna bool
- [ ] Criar função `gamepad_get_stick(stick)` retorna (x, y)
- [ ] Criar função `gamepad_set_vibration(left, right)` para force feedback (se suportado)
- [ ] Integrar com game input system (key states)

### 6.4 Controller UI

- [ ] Criar tela "Configurar Controles" no gerenciador
- [ ] Mostrar diagrama do controle (ASCII art)
- [ ] Permitir remapear cada botão
- [ ] Mostrar calibração dos sticks
- [ ] Testar botões (pressionar para verificar)
- [ ] Salvar configurações em `/gamepad-config.txt`

---

## Fase 7: Interface TUI do Gerenciador

### 7.1 Janela Principal

- [ ] Criar módulo `src/games/gameui.c` e `gameui.h`
- [ ] Criar janela "Gerenciador de Jogos"
- [ ] Adicionar barra de menu (Jogos, Exibir, Configurações, Ajuda)
- [ ] Adicionar barra de status (jogos instalados, espaço usado)
- [ ] Integrar com window manager
- [ ] Registrar na taskbar com ícone de controle
- [ ] Registrar no menu Start
- [ ] Atalho Shift+F8 para abrir

### 7.2 Abas do Gerenciador

- [ ] Criar aba "Biblioteca" — grid de jogos com ícones
- [ ] Criar aba "Recentes" — jogos jogados recentemente
- [ ] Criar aba "Favoritos" — jogos marcados como favoritos
- [ ] Criar aba "Performance" — stats de todos os jogos
- [ ] Criar aba "Conquistas" — todas as conquistas
- [ ] Criar aba "Configurações" — opções do gerenciador

### 7.3 Tela de Jogo

- [ ] Criar tela de lançamento do jogo
- [ ] Mostrar: Nome, Ícone, Descrição
- [ ] Botão "Jogar" (inicia o jogo)
- [ ] Botão "Configurar" (opções antes de jogar)
- [ ] Opções de resolução (se aplicável)
- [ ] Opções de áudio (volume, música)
- [ ] Opções de controles (teclado/gamepad)
- [ ] Barra de carregamento (se jogo precisa de init)

### 7.4 Configurações Globais

- [ ] Criar painel de configurações do gerenciador
- [ ] Opção: Mostrar overlay por padrão
- [ ] Opção: Posição do overlay
- [ ] Opção: Tamanho do overlay
- [ ] Opção: Modo padrão (FPS/Hardware/Full)
- [ ] Opção: Captura automática de screenshots
- [ ] Opção: Gravação automática de replays
- [ ] Opção: Limitar FPS (para economia de energia)
- [ ] Opção: Habilitar/desabilitar som
- [ ] Salvar em `/game-manager-config.txt`

### 7.5 Integração com Outros Módulos

- [ ] Integrar com PCSista para performance monitoring
- [ ] Integrar com Settings para configurações de áudio
- [ ] Integrar com Task Manager para processos de jogos
- [ ] Integrar com File Manager para navegar na pasta de jogos
- [ ] Integrar com Window Manager para fullscreen
- [ ] Integrar com Sound System para volume do jogo

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| Máximo de jogos | 32 | Array estático |
| Máximo de conquistas | 64 | Array estático |
| Tamanho do ícone | 32×32 | BMP format |
| Resolução de jogo | até 1920×1080 | VESA framebuffer |
| FPS máximo | 50 | Limitado por PIT (50 Hz) |
| Gravação de vídeo | 30 segundos | Limitado por RAM |
| Replay | 60 segundos | Limitado por RAM |
| Screenshots | ilimitados | Disco (FAT12: 224 arquivos max) |
| Gamepads | 1 | Porta legacy |
| Áudio | PCM only | AC97, sem mixing avançado |
| Mouse | Nenhum | Sem driver de mouse |
| Rede | Nenhum | Sem multiplayer online |
| GPU | Nenhum | Sem aceleração 3D |
| Fullscreen | Limitado | VESA mode switch lento |
| Perf history | 300 entries | 5 minutos (1/segundo) |
| Controller input | 60 Hz | Polling via timer |

---

## Notas de Implementação

1. **Sem mouse** — O ZephyrOS funciona apenas com teclado PS/2. Jogos devem ser navegáveis por teclado. Gamepad via serial seria uma extensão futura.

2. **FPS limitado a 50** — O PIT roda a 50 Hz, então FPS máximo é 50. Para jogos mais rápidos, seria necessário usar RDTSC ou aumentar frequência do PIT.

3. **Sem GPU** — Não há aceleração 3D. Todos os jogos seriam 2D usando VESA framebuffer. Sprite rendering é feito por software.

4. **Sem rede** — Não há multiplayer online. Jogos seriam single-player apenas.

5. **Áudio limitado** — AC97 suporta PCM playback. Não há mixing de múltiplos canais. Um jogo pode tocar apenas uma música/efeito por vez.

6. **Double buffering** — Para jogos smooth, seria necessário implementar double buffering (renderizar em buffer off-screen, depois copiar para tela). Isto requer memória extra (resolução × bpp × 2).

7. **Formato de jogo** — O `.zephyrosgame` é um formato proprietário simples (header + ELF binary + assets). Não é compatível com outros sistemas.

8. **Screenshots em BMP** — BMP é simples mas grande. Compressão RLE reduziria tamanho mas增加了复杂性。

9. **Gamepad via serial** — Ports 0x200-0x207 são para gameports legacy. Muitos emuladores (QEMU, VirtualBox) suportam gamepad via USB emulada, mas o ZephyrOS não tem driver USB.

10. **Integração existente** — O PCSista já fornece FPS counter e hardware monitoring. O Game Overlay estende isso com elementos específicos para jogos.

---

## Referências

- `src/drivers/timer.c` — PIT timer (26 linhas)
- `src/drivers/keyboard.c` — PS/2 keyboard (53 linhas)
- `src/drivers/ac97.c` — AC97 audio (197 linhas)
- `src/drivers/speaker.c` — PC Speaker (55 linhas)
- `src/drivers/vesa.c` — VESA framebuffer (392 linhas)
- `src/drivers/video.c` — VGA text mode (170 linhas)
- `src/memory/memory.c` — Memory manager (206 linhas)
- `src/memory/compress.c` — LZSS compression (220 linhas)
- `src/shell/shell.c` — Shell commands (518 linhas)
- `src/wm/wm.c` — Window manager (488 linhas)
- `src/pcsista/pcsista.c` — Performance monitoring (futuro)
