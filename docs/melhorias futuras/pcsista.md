# PCSista — Monitor de Sistema e FPS (RivaTuner-like)

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Infraestrutura de monitoramento | 42 | 28 | 1 | 13 |
| 2. Contador de FPS | 38 | 0 | 0 | 38 |
| 3. Monitor de hardware (overlay) | 56 | 0 | 0 | 56 |
| 4. Interface TUI do PCSista | 64 | 0 | 0 | 64 |
| 5. Diagnósticos e relatórios | 44 | 0 | 0 | 44 |
| 6. Integração com其他 módulos | 32 | 4 | 0 | 28 |
| **TOTAL** | **276** | **32** | **1** | **243** |

**Progresso geral: 12%** (32/276 itens completos, 1 parcial)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F9 | Abrir/fechar overlay de monitoramento |
| F10 | Abrir janela do PCSista |
| F11 | Capturar screenshot |
| Ctrl+F9 | Ciclar modos de overlay (FPS, Hardware, Tudo, Off) |
| Setas | Navegar na interface |
| Esc | Fechar janela/overlay |
| Tab | Alternar entre abas |
| Enter | Selecionar opção |
| +/- | Aumentar/diminuir opacidade do overlay |

---

## Fase 1: Infraestrutura de Monitoramento ✅

### 1.1 Timer/Tick System

- [x] Driver PIT inicializado a 50 Hz (20ms por tick) — `timer.c:10-17`
- [x] Contador global de ticks — `timer.c:4`
- [x] Handler de interrupção incrementando ticks — `timer.c:19-22`
- [x] API `timer_get_ticks()` para leitura — `timer.c:24-26`
- [x] Inicialização no boot com `timer_init(50)` — `kernel.c:49-51`
- [x] Cálculo de uptime (ticks → horas:minutos:segundos) — `shell.c:232-245`
- [x] Relógio na taskbar (HH:MM) — `taskbar.c:193-231`
- [ ] Criar função `timer_get_elapsed_ms()` para medição precisa
- [ ] Criar função `timer_get_freq()` para obter frequência atual do PIT
- [ ] Detectar frequência do CPU via RDTSC ou método alternativo

### 1.2 Informações de Memória

- [x] API `memory_get_total()`, `memory_get_free()`, `memory_get_used()` — `memory.c:204-206`
- [x] Struct `memory_info_t` completa — `memory.h:23-33`
- [x] Display no Task Manager (aba Memória) — `taskmanager.c:254-300`
- [x] Comando shell `mem` — `shell.c:168-181`
- [x] Info no painel Settings — `settings.c:346-366`
- [x] Heap allocator (first-fit) — `memory.c:138-201`
- [x] Page allocator (bitmap) — `memory.c:85-136`
- [x] Paging system — `paging.c:1-94`
- [x] Boot memory detection (E820) — `boot.asm:67-94`
- [ ] Criar função `memory_get_usage_percent()` para percentual
- [ ] Criar função `memory_get_largest_free_block()` para memória contígua
- [ ] Adicionar tracking de alocações por processo

### 1.3 Informações de Processos

- [x] Struct `process_t` com `total_ticks` para CPU% — `process.h:37`
- [x] Cálculo de CPU% por processo no Task Manager — `taskmanager.c:217-223`
- [x] Color coding: >80% vermelho, >50% amarelo — `taskmanager.c:225-230`
- [x] Lista de processos (PID, nome, estado, CPU%, tempo) — `taskmanager.c:156-252`
- [x] Comando shell `procs` — `shell.c:183-206`
- [x] Kill process com Delete — `taskmanager.c:440`
- [x] Proteção do PID 1 — `taskmanager.c:440`
- [x] Lista de threads (TID, nome, estado, pai) — `taskmanager.c:302-365`
- [x] Comando shell `threads` — `shell.c:208-229`
- [ ] Criar função `process_get_cpu_usage()` para uso geral do CPU
- [ ] Criar função `process_get_running_count()` para processos ativos
- [ ] Criar função `process_get_uptime()` por processo

### 1.4 Task Manager (GUI)

- [x] Janela com 3 abas (Processos, Memória, Threads) — `taskmanager.c:1-456`
- [x] Navegação por teclado (Tab, setas, Delete) — `taskmanager.c`
- [x] Scroll na lista de processos — `taskmanager.c`
- [x] Barras de uso de memória com cores — `taskmanager.c:269-283`
- [x] Stats de compressão RAM — `compress.c:169-220`
- [ ] Adicionar aba "CPU" com uso total e por core
- [ ] Adicionar aba "Disco" com I/O e throughput
- [ ] Adicionar aba "Rede" (placeholder para futuro)
- [ ] Mostrar FPS atual no título da janela

### 1.5 Infraestrutura de Display

- [x] VGA Text Mode 80×25 — `video.c:1-170`
- [x] VESA Framebuffer (até 1920×1080) — `vesa.c:1-392`
- [x] Fonte bitmap 8×16 — `font.c:1-118`
- [x] Primitivas gráficas (rect, line, circle, text) — `vesa.c:234-392`
- [x] Window Manager com z-order — `wm.c:1-488`
- [x] Taskbar com relógio — `taskbar.c:1-493`
- [ ] Criar sistema de overlay (desenhar sobre outras janelas)
- [ ] Criar função `overlay_print()` para texto na tela
- [ ] Criar função `overlay_draw_rect()` para fundo semi-transparente
- [ ] Criar buffer de overlay separado do framebuffer principal

---

## Fase 2: Contador de FPS

### 2.1 Cálculo de FPS

- [ ] Criar módulo `src/pcsista/fps.c` e `fps.h`
- [ ] Criar variável estática `frame_count` para contagem de frames
- [ ] Criar variável estática `last_fps_time` para último cálculo
- [ ] Criar variável estática `current_fps` para FPS atual
- [ ] Criar função `fps_init()` para inicializar variáveis
- [ ] Criar função `fps_tick()` chamada a cada frame (incrementa frame_count)
- [ ] Criar função `fps_update()` que calcula FPS a cada segundo:
  ```
  if (current_time - last_fps_time >= 50) {  // 50 ticks = 1 segundo
      current_fps = frame_count;
      frame_count = 0;
      last_fps_time = current_time;
  }
  ```
- [ ] Criar função `fps_get()` que retorna current_fps
- [ ] Criar função `fps_get_avg()` para FPS médio (últimos N segundos)
- [ ] Criar função `fps_get_min()` para FPS mínimo
- [ ] Criar função `fps_get_max()` para FPS máximo
- [ ] Integrar `fps_tick()` no loop principal do kernel (`kernel.c:199-203`)
- [ ] Integrar `fps_tick()` no handler do VESA (após cada draw)
- [ ] Criar array `fps_history[60]` para histórico de FPS (últimos 60 segundos)
- [ ] Criar função `fps_get_history()` para gráfico de FPS

### 2.2 Display de FPS

- [ ] Criar função `fps_draw_overlay()` para mostrar FPS na tela
- [ ] Formato: "FPS: XX" ou "FPS: XX (AVG: XX, MIN: XX)"
- [ ] Posição configurável (canto superior esquerdo/direito, inferior)
- [ ] Cor baseada no valor: >60 verde, 30-60 amarelo, <30 vermelho
- [ ] Opção de fundo transparente ou semi-transparente
- [ ] Opção de texto com sombra (legível sobre qualquer fundo)
- [ ] Integrar com F9 para ligar/desligar overlay
- [ ] Integrar com Ctrl+F9 para ciclar modos

### 2.3 FPS em Janelas

- [ ] Criar opção "Mostrar FPS" no menu de qualquer janela
- [ ] Mostrar FPS no canto superior direito da janela
- [ ] Atualizar a cada segundo (não a cada frame para evitar flicker)
- [ ] Integrar com window manager (campo `show_fps` no struct window)
- [ ] Criar comando shell `fps` para mostrar/esconder overlay global

### 2.4 FPS por Processo

- [ ] Criar tracking de frames por processo (se aplicável)
- [ ] Mostrar FPS do processo focado no título da janela
- [ ] Integrar com campo `cpu_ticks` existente no window manager
- [ ] Criar função `process_get_fps()` por processo

---

## Fase 3: Monitor de Hardware (Overlay)

### 3.1 CPU Monitoring

- [ ] Criar módulo `src/pcsista/hwmon.c` e `hwmon.h`
- [ ] Detectar frequência do CPU via RDTSC (se disponível) ou estimar
- [ ] Criar função `hwmon_get_cpu_freq()` retorna MHz
- [ ] Criar função `hwmon_get_cpu_usage()` retorna percentual (já parcialmente existe em taskmanager)
- [ ] Criar função `hwmon_get_cpu_temp()` (placeholder — sem sensor real)
- [ ] Criar função `hwmon_get_cpu_count()` retorna número de cores (1 no modo real)
- [ ] Criar função `hwmon_get_cpu_model()` retorna string do modelo
- [ ] Integrar com CPUID para obter informações do processador
- [ ] Detectar fabricante (Intel, AMD, VIA, etc.)
- [ ] Detectar família/modelo/stepping

### 3.2 Memory Monitoring

- [ ] Criar função `hwmon_get_ram_total()` (já existe `memory_get_total()`)
- [ ] Criar função `hwmon_get_ram_used()` (já existe `memory_get_used()`)
- [ ] Criar função `hwmon_get_ram_free()` (já existe `memory_get_free()`)
- [ ] Criar função `hwmon_get_ram_percent()` retorna uso percentual
- [ ] Criar função `hwmon_get_swap_used()` (placeholder — sem swap)
- [ ] Criar tracking de alocações por processo
- [ ] Criar função `hwmon_get_page_faults()` retorna contagem

### 3.3 Disk Monitoring

- [ ] Criar função `hwmon_get_disk_model()` (já existe em ata_device_t)
- [ ] Criar função `hwmon_get_disk_size()` retorna tamanho em MB/GB
- [ ] Criar função `hwmon_get_disk_sectors()` retorna total de setores
- [ ] Criar tracking de I/O (leituras/escritas)
- [ ] Criar função `hwmon_get_disk_reads()` retorna total de leituras
- [ ] Criar função `hwmon_get_disk_writes()` retorna total de escritas
- [ ] Criar função `hwmon_get_disk_activity()` retorna se disco está ativo
- [ ] Integrar com ATA driver para contar operações

### 3.4 GPU/Video Monitoring

- [ ] Criar função `hwmon_get_gpu_model()` (placeholder — sem detecção GPU)
- [ ] Criar função `hwmon_get_vram_total()` (placeholder — sem VRAM)
- [ ] Criar função `hwmon_get_video_mode()` retorna modo atual (80×25 ou VESA)
- [ ] Criar função `hwmon_get_resolution()` retorna largura×altura
- [ ] Criar função `hwmon_get_bpp()` retorna bits por pixel
- [ ] Criar função `hwmon_get_framebuffer_addr()` retorna endereço
- [ ] Criar função `hwmon_get_refresh_rate()` (estimado, sem VSync)
- [ ] Detectar se VESA está ativo

### 3.5 Audio Monitoring

- [ ] Criar função `hwmon_get_audio_model()` (placeholder — sem modelo)
- [ ] Criar função `hwmon_get_audio_sample_rate()` (já existe em ac97_device_t)
- [ ] Criar função `hwmon_get_audio_volume()` (já existe `ac97_get_volume()`)
- [ ] Criar função `hwmon_get_audio_playing()` retorna se áudio está tocando
- [ ] Integrar com AC97 driver para status de playback

### 3.6 System Info

- [ ] Criar função `hwmon_get_uptime()` (já existe `cmd_uptime()`)
- [ ] Criar função `hwmon_get_process_count()` retorna processos ativos
- [ ] Criar função `hwmon_get_thread_count()` retorna threads ativas
- [ ] Criar função `hwmon_get_irq_count()` retorna interrupções processadas
- [ ] Criar função `hwmon_get_timer_freq()` retorna frequência do PIT (50 Hz)
- [ ] Criar função `hwmon_get_kernel_version()` retorna "ZephyrOS v0.1"
- [ ] Criar função `hwmon_get_boot_time()` retorna timestamp do boot

---

## Fase 4: Interface TUI do PCSista

### 4.1 Janela Principal

- [ ] Criar módulo `src/pcsista/pcsista.c` e `pcsista.h`
- [ ] Criar estrutura `pcsista_t` (janela, estado, configurações)
- [ ] Criar janela principal com bordas e título "PCSista - Monitor de Sistema"
- [ ] Adicionar barra de menu (Arquivo, Exibir, Configurações, Ajuda)
- [ ] Adicionar barra de status (FPS atual, CPU%, RAM%, disco)
- [ ] Integrar com window manager (`wm_create_window`)
- [ ] Registrar na taskbar com ícone
- [ ] Registrar no menu Start do taskbar
- [ ] Registrar no launcher de desktop
- [ ] Atalho F10 para abrir/fechar

### 4.2 Abas de Monitoramento

- [ ] Criar aba "Visão Geral" com resumo de tudo
  - FPS atual com barra
  - CPU% com barra
  - RAM% com barra
  - Disco ativo (sim/não)
  - Uptime
- [ ] Criar aba "CPU" com detalhes
  - Modelo do processador
  - Frequência (MHz)
  - Uso percentual com gráfico
  - Top 5 processos por CPU%
- [ ] Criar aba "Memória" com detalhes
  - Total/Usado/Livre
  - Gráfico de uso
  - Top 5 processos por memória
  - Estatísticas de paginação
- [ ] Criar aba "Disco" com detalhes
  - Modelo do disco
  - Tamanho total
  - Setores lidos/escritos
  - Atividade atual
- [ ] Criar aba "Sistema" com info geral
  - Versão do SO
  - Uptime
  - Processos/Threads ativos
  - IRQs processadas
  - Timer frequency

### 4.3 Overlay System

- [ ] Criar sistema de overlay independente do WM
- [ ] Criar buffer de overlay (256×64 pixels ou 32×8 caracteres)
- [ ] Criar função `overlay_show()` para ativar
- [ ] Criar função `overlay_hide()` para desativar
- [ ] Criar função `overlay_update()` para redesenhar
- [ ] Criar função `overlay_set_position(x, y)` para reposicionar
- [ ] Criar função `overlay_set_opacity(level)` para transparência
- [ ] Criar função `overlay_set_mode(mode)` para ciclar modos
- [ ] Integrar com F9 para toggle
- [ ] Integrar com Ctrl+F9 para ciclar modos
- [ ] Integrar com +/- para opacidade
- [ ] Criar 4 modos de overlay:
  1. FPS Only: "FPS: XX"
  2. Hardware: "CPU: XX% | RAM: XX% | FPS: XX"
  3. Full: "CPU: XX% | RAM: XX/XX MB | FPS: XX | Uptime: HH:MM:SS"
  4. Off: overlay desativado

### 4.4 Configurações

- [ ] Criar painel de configurações do PCSista
- [ ] Opção: Mostrar/esconder overlay por padrão
- [ ] Opção: Posição do overlay (4 cantos da tela)
- [ ] Opção: Tamanho do overlay (pequeno/médio/grande)
- [ ] Opção: Modo padrão do overlay (FPS/Hardware/Full/Off)
- [ ] Opção: Atualização do overlay (1s, 2s, 5s)
- [ ] Opção: Mostrar FPS no título das janelas
- [ ] Opção: Cor do overlay (verde, branco, amarelo)
- [ ] Opção: Fundo do overlay (transparente, preto, cinza)
- [ ] Salvar configurações em arquivo (se filesystem suportar)

### 4.5 Integração com Shell

- [ ] Comando `pcsista` — abrir janela do PCSista
- [ ] Comando `fps` — mostrar/esconder overlay de FPS
- [ ] Comando `fps on` — ligar overlay
- [ ] Comando `fps off` — desligar overlay
- [ ] Comando `hwmon` — mostrar informações de hardware
- [ ] Comando `hwmon -v` — modo verboso
- [ ] Comando `cpu-info` — mostrar detalhes do CPU
- [ ] Comando `mem-info` — mostrar detalhes da memória
- [ ] Comando `disk-info` — mostrar detalhes do disco
- [ ] Comando `sysinfo` — mostrar resumo do sistema

---

## Fase 5: Diagnósticos e Relatórios

### 5.1 Testes de Hardware

- [ ] Criar função `pcsista_test_cpu()` — testar performance do CPU
- [ ] Criar função `pcsista_test_ram()` — testar memória (leitura/escrita)
- [ ] Criar função `pcsista_test_disk()` — testar velocidade do disco
- [ ] Criar função `pcsista_test_video()` — testar modo de vídeo
- [ ] Criar função `pcsista_test_audio()` — testar áudio (beep)
- [ ] Criar função `pcsista_run_all()` — executar todos os testes
- [ ] Criar relatório de resultados (pass/fail para cada teste)
- [ ] Integrar com PC Speaker para notificações sonoras

### 5.2 Relatórios

- [ ] Criar função `pcsista_generate_report()` — gerar relatório completo
- [ ] Formato de relatório: texto simples (compatível com VGA)
- [ ] Incluir: CPU info, RAM info, Disk info, processos, uptime
- [ ] Incluir: estatísticas de uso, alertas, erros
- [ ] Criar comando shell `pcsista-report` — gerar e salvar relatório
- [ ] Criar opção de exportar para arquivo (se filesystem suportar)
- [ ] Criar opção de copiar para área de transferência (clipboard)

### 5.3 Alertas

- [ ] Criar sistema de alertas (thresholds configuráveis)
- [ ] Alerta: CPU > 90% por mais de 10 segundos
- [ ] Alerta: RAM > 85% usage
- [ ] Alerta: Disco cheio (< 10% livre)
- [ ] Alerta: FPS < 30 por mais de 5 segundos
- [ ] Mostrar alertas no overlay (cor vermelha)
- [ ] Tocar som (PC Speaker) para alertas críticos
- [ ] Criar log de alertas (últimos N alertas)
- [ ] Criar comando shell `pcsista-alerts` — mostrar alertas

### 5.4 Performance History

- [ ] Criar array `perf_history[3600]` para histórico de 1 hora (1 registro/segundo)
- [ ] Armazenar: timestamp, CPU%, RAM%, FPS, disco ativo
- [ ] Criar função `perf_get_history(start, end)` para consultar período
- [ ] Criar função `perf_get_avg(metric)` para média de uma métrica
- [ ] Criar função `perf_get_peak(metric)` para pico de uma métrica
- [ ] Criar gráfico de linha do tempo no TUI (barra ASCII)
- [ ] Criar comando shell `pcsista-perf` — mostrar gráfico

---

## Fase 6: Integração com Outros Módulos

### 6.1 Window Manager

- [x] Per-window CPU tracking implementado — `wm.c:291-292, 478-488`
- [x] Campo `cpu_ticks` no struct window — `wm.h:58-59`
- [x] Função `wm_update_cpu_stats()` chamada no loop principal — `kernel.c:201`
- [ ] Mostrar CPU% por janela no título (opcional)
- [ ] Mostrar FPS da janela focada
- [ ] Integrar com overlay para mostrar janela ativa
- [ ] Adicionar campo `show_fps` no struct window

### 6.2 Task Manager

- [x] Aba Processos com CPU% por processo — `taskmanager.c:156-252`
- [x] Aba Memória com uso de RAM — `taskmanager.c:254-300`
- [x] Aba Threads — `taskmanager.c:302-365`
- [x] Kill process com Delete — `taskmanager.c:440`
- [ ] Adicionar coluna "FPS" se processo tiver janela
- [ ] Adicionar coluna "Memória" com valor em KB
- [ ] Integrar com PCSista para dados detalhados
- [ ] Criar botão "Abrir PCSista" no Task Manager

### 6.3 Settings Panel

- [x] Info de memória no Settings — `settings.c:346-366`
- [x] Lista de processos no Settings — `settings.c:368-381`
- [ ] Adicionar categoria "Monitoramento" no Settings
- [ ] Adicionar opção "Configurar PCSista"
- [ ] Adicionar opção "Mostrar overlay"
- [ ] Adicionar opção "Relatório de sistema"

### 6.4 Shell

- [x] Comando `mem` — `shell.c:168-181`
- [x] Comando `procs` — `shell.c:183-206`
- [x] Comando `threads` — `shell.c:208-229`
- [x] Comando `uptime` — `shell.c:232-245`
- [x] Comando `stats` — `shell.c:517-518`
- [ ] Adicionar comando `pcsista` — abrir janela
- [ ] Adicionar comando `fps` — toggle overlay
- [ ] Adicionar comando `hwmon` — info de hardware
- [ ] Adicionar comando `sysinfo` — resumo completo
- [ ] Adicionar comando `pcsista-report` — gerar relatório

### 6.5 Kernel Main Loop

- [x] Loop principal com `hlt` — `kernel.c:199-203`
- [x] `taskbar_update_clock()` chamado — `kernel.c:200`
- [x] `wm_update_cpu_stats()` chamado — `kernel.c:201`
- [ ] Adicionar `fps_tick()` no loop principal
- [ ] Adicionar `overlay_update()` no loop principal (se ativo)
- [ ] Adicionar `pcsista_check_alerts()` no loop (a cada 5 segundos)

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| FPS máximo mensurável | 50 | Limitado pela frequência do PIT (50 Hz) |
| Resolução VGA | 80×25 | Text mode, sem framebuffer nativo |
| Resolução VESA | até 1920×1080 | Framebuffer via BIOS |
| CPU cores | 1 | Modo real, sem SMP |
| Temperatura CPU | Nenhum | Sem sensor de temperatura |
| GPU monitoring | Nenhum | Sem detecção de GPU |
| VRAM | Nenhum | Sem acesso a memória de vídeo |
| Disk I/O stats | Nenhum | ATA driver sem contadores |
| Network | Nenhum | Sem stack de rede |
| Swap | Nenhum | Sem sistema de swap |
| Performance counters | Nenhum | Sem uso de PMC/PMU |
| Overlay transparency | Limitado | VGA text mode sem alpha blending |
| Máximo de processos | 64 | Array estático |
| Máximo de threads | 32 | Array estático |
| Histórico de performance | 3600 entries | 1 hora (1/segundo) |
| Alertas simultâneos | 16 | Array estático |

---

## Notas de Implementação

1. **FPS Limitado a 50** — O PIT roda a 50 Hz, então o FPS máximo mensurável é 50. Para FPS mais alto, seria necessário usar RDTSC ou aumentar a frequência do PIT.

2. **Sem hardware real** — O ZephyrOS roda em emuladores (QEMU, VirtualBox) que não expõem sensores de temperatura, fan speed, ou GPU stats. As funções de hardware monitoring são placeholders.

3. **Overlay em VGA** — O overlay usa caracteres VGA, não pixels. Transparência é limitada a fundo preto/transparente. VESA overlay seria mais flexível mas requer mais memória.

4. **Integração existente** — O Task Manager já fornece CPU% por processo e memória. O PCSista estende isso com FPS, hardware info e overlay.

5. **RDTSC** — Para medir frequência do CPU com precisão, seria necessário usar a instrução RDTSC (Time Stamp Counter). Isto requer detecção de suporte via CPUID.

6. **Sem multi-core** — O ZephyrOS roda em modo real (single-core). CPU monitoring é simplificado (sem per-core stats).

7. **Persistência** — Configurações do PCSista podem ser salvas em arquivo se o filesystem suportar. Caso contrário, ficam em memória (perdidas no reboot).

---

## Referências

- `src/drivers/timer.c` — PIT timer driver (26 linhas)
- `src/shell/taskmanager.c` — Task Manager com 3 abas (456 linhas)
- `src/shell/shell.c` — Comandos de diagnóstico (mem, procs, threads, uptime, stats)
- `src/memory/memory.c` — Memory manager com API de queries (206 linhas)
- `src/wm/wm.c` — Window Manager com CPU tracking (488 linhas)
- `src/drivers/vesa.c` — VESA framebuffer (392 linhas)
- `src/drivers/video.c` — VGA text mode (170 linhas)
- `src/drivers/ata.c` — ATA disk driver (196 linhas)
- `src/drivers/ac97.c` — AC97 audio driver (197 linhas)
- `src/settings/settings.c` — Settings panel (549 linhas)
- `src/kernel/kernel.c` — Main loop e init (203 linhas)
