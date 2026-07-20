# Gerenciador de Dispositivos — ZephyrOS v0.1

> **Arquitetura:** As funções core (comandos shell: `devices`, `device-info`, `device-scan`) são nativas do sistema. A interface visual (TUI) é um app opcional distribuído pelo [Gerenciador de Aplicativos](gerenciador%20de%20aplicativos.md).

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Drivers de hardware (baixo nível) | 68 | 48 | 1 | 19 |
| 2. Interface TUI do Gerenciador | 72 | 0 | 0 | 72 |
| 3. Gerenciamento de dispositivos | 58 | 0 | 0 | 58 |
| 4. Plug and Play e Hotplug | 42 | 0 | 0 | 42 |
| 5. Drivers dinâmicos e Driver Store | 54 | 0 | 0 | 54 |
| 6. Atualização e segurança | 46 | 0 | 0 | 46 |
| **TOTAL** | **340** | **48** | **1** | **291** |

**Progresso geral: 14%** (48/340 itens completos, 1 parcial)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F5 | Atualizar lista de dispositivos |
| Enter | Abrir propriedades do dispositivo selecionado |
| Delete | Desativar dispositivo selecionado |
| Shift+Delete | Desinstalar dispositivo |
| Tab | Alternar entre categorias |
| Setas | Navegar na lista |
| F1 | Ajuda |
| Esc | Fechar janela |

---

## Fase 1: Drivers de Hardware (Baixo Nível) ✅

### 1.1 PCI Bus Enumeration

- [x] Definição de ports PCI_CONFIG_ADDRESS (0xCF8) e PCI_CONFIG_DATA (0xCFC) — `pci.h:6-7`
- [x] Constantes de registradores PCI (VENDOR_ID até INTERRUPT_PIN) — `pci.h:9-27`
- [x] Struct `pci_device_t` completa (vendor, device, class, subclass, prog_if, revision, irq, bus/dev/func, BAR0-BAR5, present) — `pci.h:29-47`
- [x] Leitura de config space via `pci_read()` (bus<<16 | dev<<11 | func<<8 | offset | 0x80000000) — `pci.c:37-41`
- [x] Escrita de config space via `pci_write()` — `pci.c:43-47`
- [x] Escaneamento por device com `pci_scan_device()` (8 functions por device) — `pci.c:49-89`
- [x] Enumeração completa com `pci_init()` (256 buses × 32 devices, multi-function) — `pci.c:91-115`
- [x] Busca por class/subclass com `pci_get_device(class, subclass)` — `pci.c:117-126`
- [x] Busca por vendor/device ID com `pci_get_device_by_id(vendor_id, device_id)` — `pci.c:128-137`
- [x] Bus mastering enable com `pci_enable_bus_mastering()` — `pci.c:139-144`
- [ ] Exportar lista de dispositivos para interface TUI
- [ ] Traduzir class codes para nomes legíveis (ex: 0x01 = "Controlador de disco")
- [ ] Traduzir vendor IDs para nomes de fabricantes
- [ ] Mostrar dispositivos por categoria (áudio, vídeo, rede, etc.)

### 1.2 ATA/IDE Disk Driver

- [x] Definições de ports (ATA_PRIMARY_IO=0x1F0, ATA_PRIMARY_CTRL=0x3F6, etc.) — `ata.h:6-9`
- [x] Registradores ATA (DATA, ERROR, SECCOUNT, LBA_LOW/MID/HIGH, DRIVE, STATUS/COMMAND) — `ata.h:11-19`
- [x] Flags de status (BSY, DRDY, DRQ, ERR) — `ata.h:21-24`
- [x] Comandos ATA (READ=0x20, WRITE=0x30, IDENTIFY=0xEC) — `ata.h:26-28`
- [x] Struct `ata_device_t` completa (base_port, ctrl_port, slave, signature, capabilities, sectors, model, present) — `ata.h:30-39`
- [x] Detecção com `ata_detect()` (soft reset, IDENTIFY, leitura de 256 words) — `ata.c:61-109`
- [x] Inicialização com `ata_init()` (master + slave no barramento primário) — `ata.c:111-118`
- [x] Seleção de drive com `ata_select_drive()` — `ata.c:49-52`
- [x] Soft reset com `ata_soft_reset()` — `ata.c:54-59`
- [x] Espera DRDY com `ata_wait_ready()` — `ata.c:28-37`
- [x] Espera DRQ com `ata_wait_drq()` — `ata.c:39-47`
- [x] Leitura de setores LBA28 com `ata_read_sectors()` — `ata.c:127-159`
- [x] Escrita de setores LBA28 com `ata_write_sectors()` — `ata.c:161-196`
- [x] Recuperação de device com `ata_get_device()` — `ata.c:120-125`
- [ ] Mostrar informações do disco na interface (modelo, capacidade, setores)
- [ ] Mostrar status do disco (presente/ausente)
- [ ] Listar todos os discos detectados

### 1.3 Keyboard PS/2 Driver

- [x] Tabela scancode→ASCII completa (128 entries) — `keyboard.c:13-23`
- [x] Conversão com `scancode_to_ascii()` — `keyboard.c:25-30`
- [x] Inicialização com `keyboard_init()` (registra handler IRQ1/int 33) — `keyboard.c:32-34`
- [x] Handler de interrupção `keyboard_handler()` (leitura porta 0x60) — `keyboard.c:36-47`
- [x] Sistema de callbacks dinâmicos com `keyboard_set_callback()` — `keyboard.c:49-53`
- [ ] Mostrar teclado como dispositivo na interface
- [ ] Exibir status (ativo/inativo)

### 1.4 Audio AC97 Driver

- [x] Constantes PCI (AC97_PCI_CLASS=0x04, SUBCLASS=0x01) — `ac97.h:6-7`
- [x] 36+ registradores AC97 (RESET, MASTER_VOL, PCM_OUT_VOL, etc.) — `ac97.h:9-35`
- [x] Flags de output (SR, FIFOE, LVBCI, DMA_EN) — `ac97.h:43-48`
- [x] Registradores PO (STATUS, LPIB, CIV, LVI, etc.) — `ac97.h:50-62`
- [x] Struct `ac97_device_t` (io_base, ctrl_base, irq, slot, codec_type, sample_rate, etc.) — `ac97.h:67-78`
- [x] Inicialização com `ac97_init()` (busca PCI, leitura BARs, bus mastering, reset, configuração) — `ac97.c:77-124`
- [x] Leitura/Escrita de registrador — `ac97.c:47-53`
- [x] Reset do codec — `ac97.c:55-59`
- [x] Power down — `ac97.c:61-66`
- [x] Configuração de sample rate — `ac97.c:68-75`
- [x] Play de áudio com `ac97_play()` — `ac97.c:126-154`
- [x] Stop de áudio com `ac97_stop()` — `ac97.c:156-170`
- [x] Controle de volume com `ac97_set_volume()` — `ac97.c:172-179`
- [x] Handler de interrupção — `ac97.c:185-197`
- [ ] Mostrar controlador de áudio na interface
- [ ] Exibir informações do codec (tipo, sample rate, canais)

### 1.5 PC Speaker Driver

- [x] Inicialização com `speaker_init()` — `speaker.c:8-10`
- [x] Beep com `speaker_beep(freq, duration_ms)` — `speaker.c:12-36`
- [x] Desligar com `speaker_off()` — `speaker.c:38-41`
- [x] Melodia com `speaker_play_melody()` — `speaker.c:43-55`
- [ ] Mostrar PC Speaker como dispositivo de áudio

### 1.6 VGA Text Mode Driver

- [x] Constantes VGA (VIDEO_MEMORY=0xB8000, WIDTH=80, HEIGHT=25) — `video.h:6-8`
- [x] 16 cores VGA definidas — `video.h:10-25`
- [x] Inicialização com `video_init()` — `video.c:40-44`
- [x] Limpar tela com `video_clear()` — `video.c:46-53`
- [x] Print de caractere com `video_put_char()` (trata \n, \r, \t, \b, scroll) — `video.c:55-78`
- [x] Print de string com `video_print()` — `video.c:80-84`
- [x] Atualização do cursor via ports 0x3D4/0x3D5 — `video.c:16-26`
- [x] Scroll automático — `video.c:28-38`
- [x] Print em posição com `video_print_at()` — `video.c:118-135`
- [x] Preencher retângulo com `video_fill_rect()` — `video.c:137-143`
- [x] Desenhar box com `video_draw_box()` (box-drawing characters) — `video.c:157-170`
- [ ] Mostrar adaptador de vídeo como dispositivo
- [ ] Exibir modo atual (80×25, text mode)

### 1.7 VESA Framebuffer Driver

- [x] Constantes de resolução (640×480 até 1920×1080) — `vesa.h:6-15`
- [x] Struct `vesa_mode_t` (width, height, bpp, pitch, framebuffer, initialized) — `vesa.h:21-28`
- [x] Struct `vesa_color_t` (BGRA pixel + raw uint32) — `vesa.h:30-40`
- [x] Escaneamento de modos com `vesa_scan_modes()` (BIOS int 0x10, func 0x4F00/0x4F01) — `vesa.c:49-154`
- [x] Teste individual com `try_mode()` — `vesa.c:25-47`
- [x] Inicialização com `vesa_init()` — `vesa.c:156-184`
- [x] Mudança de modo com `vesa_set_mode()` — `vesa.c:186-213`
- [x] Operações de pixel (put/get) — `vesa.c:215-232`
- [x] Primitivas gráficas (clear, fill_rect, lines, circles, bitmap, text) — `vesa.c:234-392`
- [ ] Mostrar framebuffer como dispositivo de vídeo
- [ ] Exibir resolução e modo atual

### 1.8 Timer PIT

- [x] Inicialização com `timer_init(50)` (IRQ0, canal 0, divisor 1193180/freq) — `timer.c:10-17`
- [x] Handler `timer_handler()` — `timer.c:19-22`
- [x] Get ticks com `timer_get_ticks()` — `timer.c:24-26`
- [ ] Mostrar timer como dispositivo de sistema

### 1.9 IDT/ISR/IRQ Infrastructure

- [x] Struct IDT entry — `idt.h:6-12`
- [x] Struct registers — `idt.h:19-24`
- [x] 32 ISRs (exceções CPU) — `isr.asm:21-52`
- [x] 16 IRQs (hardware) — `irq.asm:13-28`
- [x] ISR common stub — `isr.asm:54-78`
- [x] IRQ common stub — `irq.asm:30-54`
- [x] IDT init com `idt_init()` (256 entradas, PIC remap, sti) — `idt.c:99-162`
- [x] PIC remapping (master 0x20→32, slave 0xA0→40) — `idt.c:67-87`
- [x] Handler registration — `idt.c:164-166`
- [x] Exception messages (22 mensagens) — `idt.c:168-193`
- [x] ISR handler (kernel panic) — `idt.c:195-245`
- [x] IRQ handler (chamada + EOI) — `idt.c:247-256`
- [ ] Mostrar controlador de interrupções na interface

### 1.10 TSS (Task State Segment)

- [x] Struct TSS completa — `tss.h:6-34`
- [x] Inicialização com `tss_init()` — `tss.c:8-41`
- [x] Set kernel stack com `tss_set_kernel_stack()` — `tss.c:43-45`
- [ ] Mostrar TSS como componente de sistema

### 1.11 Boot / Deteção de Memória

- [x] BPB FAT12 — `boot.asm:10-28`
- [x] Deteção E820 com BIOS int 0x15 — `boot.asm:67-94`
- [x] Carregamento do kernel com `disk_load()` — `boot.asm:111-133`
- [x] GDT setup — `boot.asm:150-175`
- [x] Protected mode switch — `boot.asm:136-148`
- [x] Context switch — `switch.asm:4-41`
- [ ] Mostrar mapa de memória E820 na interface
- [ ] Exibir quantas entradas de memória foram detectadas

### 1.12 Deteção de Hardware no Boot (kernel.c)

- [x] Ordem de inicialização documentada (12 etapas) — `kernel.c:25-203`
- [x] VGA Text Mode init — `kernel.c:26`
- [x] IDT/PIC init — `kernel.c:42-43`
- [x] Keyboard PS/2 init — `kernel.c:45-47`
- [x] Timer PIT init — `kernel.c:49-51`
- [x] Memory E820 — `kernel.c:53-86`
- [x] Paging — `kernel.c:88-90`
- [x] TSS + Processes + Threads — `kernel.c:92-99`
- [x] ATA Disk — `kernel.c:101-110`
- [x] FAT12/FAT32 — `kernel.c:112-118`
- [x] PC Speaker — `kernel.c:120-122`
- [x] VESA + Font — `kernel.c:124-155`
- [x] AC97 Audio — `kernel.c:157-164`
- [ ] Armazenar log de inicialização para exibição na interface
- [ ] Registrar timestamp de cada etapa
- [ ] Armazenar status de sucesso/erro de cada driver

---

## Fase 2: Interface TUI do Gerenciador

### 2.1 Janela Principal

- [ ] Criar módulo `src/devicemanager/devicemanager.c` e `devicemanager.h`
- [ ] Criar estrutura `device_manager_t` (lista de dispositivos, janela, estado)
- [ ] Criar janela principal com bordas e título "Gerenciador de Dispositivos"
- [ ] Adicionar barra de menu (Arquivo, Exibir, Ação, Ajuda)
- [ ] Adicionar barra de ferramentas (Atualizar, Propriedades, Desativar)
- [ ] Adicionar barra de status (total de dispositivos, dispositivo selecionado)
- [ ] Integrar com window manager (`wm_create_window`, callbacks)
- [ ] Registrar na taskbar com ícone
- [ ] Registrar no menu Start do taskbar
- [ ] Registrar no launcher de desktop

### 2.2 Árvore de Categorias

- [ ] Criar painel esquerdo com árvore de categorias
- [ ] Adicionar categorias: Adaptadores de vídeo, Controladores de áudio, Controladores de sistema, Dispositivos de interface humana, Unidades de disco, Outros dispositivos
- [ ] Implementar expand/collapse com setas (→/↓)
- [ ] Mostrar contagem de dispositivos por categoria entre parênteses
- [ ] Destacar categoria selecionada com cor diferente
- [ ] Navegação com setas (cima/baixo) e Enter
- [ ] Filtrar lista da direita ao selecionar categoria
- [ ] Adicionar opção "Mostrar todos os dispositivos" no topo

### 2.3 Lista de Dispositivos (Painel Direito)

- [ ] Criar lista detalhada com colunas: Nome, Tipo, Status, Local
- [ ] Mostrar ícone por tipo (🖥️ vídeo, 🔊 áudio, 💾 disco, ⌨️ teclado, etc.)
- [ ] Indicadores visuais de estado: ✅ funcionando, ⚠️ problema, ⬇️ desativado
- [ ] Ordenação por nome (alfabética)
- [ ] Seleção com highlighting
- [ ] Duplo-clique (Enter) para abrir propriedades
- [ ] Botão direito (menu de contexto) com opções: Ativar, Desativar, Propriedades
- [ ] Scroll com Page Up/Down e setas
- [ ] Indicador de "Nenhum dispositivo encontrado" quando vazio

### 2.4 Diálogo de Propriedades

- [ ] Criar janela de propriedades modal (bloqueia janela pai)
- [ ] Aba "Geral": Nome, Tipo, Fabricante, Localização, Estado, Código de erro
- [ ] Aba "Driver": Fabricante, Data, Versão, Arquivos, Botões (Atualizar, Reverter, Desinstalar)
- [ ] Aba "Detalhes": IDs de hardware, Classe, GUID, Serviço, Barramento, IRQ, Portas I/O, Memória
- [ ] Aba "Eventos": Log de eventos do dispositivo (instalação, configuração, falha)
- [ ] Aba "Recursos": Intervalos de memória, Portas I/O, IRQ, DMA
- [ ] Botões: OK, Cancelar, Aplicar
- [ ] Navegação com Tab entre abas

### 2.5 Integração com Shell

- [ ] Comando `devices` — listar todos os dispositivos
- [ ] Comando `devices -v` — modo verboso (detalhes completos)
- [ ] Comando `devices -c <categoria>` — filtrar por categoria
- [ ] Comando `device-info <id>` — mostrar detalhes de um dispositivo
- [ ] Comando `device-scan` — re-escanear hardware
- [ ] Comando `device-disable <id>` — desativar dispositivo
- [ ] Comando `device-enable <id>` — ativar dispositivo

---

## Fase 3: Gerenciamento de Dispositivos

### 3.1 Estado do Dispositivo

- [ ] Criar enum `device_status_t` (OK, WARNING, ERROR, DISABLED, UNKNOWN)
- [ ] Adicionar campo `status` em structs de dispositivo (PCI, ATA, AC97, etc.)
- [ ] Mapear códigos de erro do ZephyrOS (Código 10, 22, 28, 31, 39, 43, 45)
- [ ] Atualizar status automaticamente durante operações
- [ ] Mostrar indicador visual na interface (cores/texturas)
- [ ] Registrar mudanças de status no log de eventos
- [ ] Alertar quando dispositivo entra em estado de erro

### 3.2 Informações do Dispositivo

- [ ] Criar função `device_get_info()` que retorna struct completa
- [ ] Extrair informações do PCI config space (vendor, device, class, IRQ, BARs)
- [ ] Extrair informações do ATA (modelo, capacidade, setores)
- [ ] Extrair informações do AC97 (codec type, sample rate, canais)
- [ ] Extrair informações do teclado (tipo, porta)
- [ ] Mostrar recursos alocados (memória, I/O, IRQ, DMA)
- [ ] Mostrar relações pai/filho (bus → device → function)
- [ ] Criar tela de informações detalhadas formatada

### 3.3 Deteção de Alterações de Hardware

- [ ] Criar função `device_rescan()` que re-executa pci_init(), ata_init(), ac97_init()
- [ ] Comparar lista anterior com nova lista (detectar adições/remoções)
- [ ] Notificar usuário sobre dispositivos adicionados/removidos
- [ ] Atualizar automaticamente a interface TUI
- [ ] Adicionar botão "Verificar alterações de hardware" na toolbar
- [ ] Integrar com F5 no teclado
- [ ] Log de mudanças (data/hora, dispositivo, ação)

### 3.4 Desativar/Ativar Dispositivos

- [ ] Criar funções `device_disable()` e `device_enable()`
- [ ] Implementar para dispositivos PCI (desabilitar BAR via PCI_COMMAND register)
- [ ] Implementar para AC97 (power down/up do codec)
- [ ] Implementar para ATA (reset do controlador)
- [ ] Marcar dispositivo como desativado na interface (seta para baixo)
- [ ] Impedir acesso a dispositivo desativado por outros módulos
- [ ] Reativar com `device_enable()` (restaurar configuração anterior)
- [ ] Proteger dispositivos essenciais (VGA, teclado) de desativação
- [ ] Confirmar antes de desativar dispositivo essencial

### 3.5 Desinstalar Dispositivo

- [ ] Criar função `device_uninstall()`
- [ ] Remover dispositivo da lista interna
- [ ] Liberar recursos alocados (IRQ, ports, memória)
- [ ] Chamar driver de shutdown se disponível
- [ ] Atualizar interface (dispositivo desaparece da lista)
- [ ] Permitir reinstalação via `device_rescan()`
- [ ] Confirmar antes de desinstalar
- [ ] Impedir desinstalação de dispositivos essenciais

### 3.6 Exibir Dispositivos Ocultos

- [ ] Adicionar opção "Mostrar dispositivos ocultos" no menu Exibir
- [ ] Marcar dispositivos como ocultos (desconectados, não presentes)
- [ ] Mostrar dispositivos ocultos com visualização esmaecida (cor diferente)
- [ ] Permitir mostrar/esconder por categoria
- [ ] Filtrar por padrão (não mostrar ocultos)
- [ ] Indicador visual de que estão ocultos

---

## Fase 4: Plug and Play e Hotplug

### 4.1 Deteção Automática de Hardware

- [ ] Criar sistema de eventos de hardware (hardware_added, hardware_removed)
- [ ] Implementar polling periódico de barramentos (PCI, ATA)
- [ ] Detectar novos dispositivos PCI (re-escaneamento)
- [ ] Detectar novos dispositivos ATA (re-escaneamento)
- [ ] Notificar interface sobre mudanças
- [ ] Tocar som (PC Speaker) ao detectar novo dispositivo
- [ ] Mostrar notificação na barra de tarefas

### 4.2 Configuração Automática

- [ ] Criar tabela de drivers por class code PCI
- [ ] Auto-detectar driver apropriado para novo dispositivo
- [ ] Atribuir IRQ automaticamente (PIC/IO-APIC)
- [ ] Configurar BARs automaticamente
- [ ] Inicializar driver apropriado
- [ ] Registrar dispositivo na lista
- [ ] Notificar usuário sobre sucesso/erro

### 4.3 Gerenciamento de Recursos

- [ ] Criar gerenciador de IRQ (alocação, liberação, conflitos)
- [ ] Criar gerenciador de ports I/O (alocação, liberação)
- [ ] Criar gerenciador de memória (alocação de buffers DMA)
- [ ] Detectar conflitos entre dispositivos
- [ ] Resolver conflitos automaticamente quando possível
- [ ] Mostrar conflitos na interface (aviso amarelo)
- [ ] Permitir resolução manual de conflitos

---

## Fase 5: Drivers Dinâmicos e Driver Store

### 5.1 Sistema de Módulos

- [ ] Criar formato de módulo (.mod) para drivers carregáveis
- [ ] Implementar loader de módulos (leitura do disco, reloc, init)
- [ ] Criar API de módulos (init, shutdown, get_info, get_status)
- [ ] Registrar módulos em tabela global
- [ ] Criar gerenciador de módulos (listar, carregar, descarregar)
- [ ] Integrar com filesystem (carregar de /drivers/)
- [ ] Criar comando shell `mod load`, `mod unload`, `mod list`

### 5.2 Driver Store

- [ ] Criar diretório `/driverstore/` no filesystem
- [ ] Armazenar pacotes de drivers (.inf, .sys)
- [ ] Indexar pacotes por class code e vendor/device ID
- [ ] Criar função `driverstore_add()` para adicionar pacote
- [ ] Criar função `driverstore_find()` para buscar driver compatível
- [ ] Criar função `driverstore_remove()` para remover pacote
- [ ] Criar comando shell `driverstore list`, `driverstore add`, `driverstore remove`
- [ ] Validar integridade dos pacotes (checksum)

### 5.3 Instalação Manual de Driver

- [ ] Criar função `driver_install(package_path)`
- [ ] Ler e validar arquivo .inf
- [ ] Copiar arquivos .sys para /drivers/
- [ ] Registrar driver no Driver Store
- [ ] Associar driver com dispositivo (matching by class/vendor/device)
- [ ] Carregar e inicializar driver
- [ ] Atualizar interface com novo dispositivo
- [ ] Criar comando shell `driver install <path>`

### 5.4 Reversão de Driver

- [ ] Manter versão anterior do driver no Driver Store
- [ ] Criar função `driver_rollback(device_id)`
- [ ] Restaurar versão anterior
- [ ] Reinicializar driver
- [ ] Atualizar interface
- [ ] Criar comando shell `driver rollback <device_id>`

### 5.5 Exportação de Drivers

- [ ] Criar função `driver_export(device_id, output_path)`
- [ ] Copiar pacote do Driver Store para pasta destino
- [ ] Criar arquivo de manifesto (.manifest)
- [ ] Criar comando shell `driver export <device_id> <path>`

---

## Fase 6: Atualização e Segurança

### 6.1 Atualização Automática de Drivers

- [ ] Criar sistema de versão de drivers (major.minor.patch)
- [ ] Comparar versão instalada com disponível
- [ ] Criar repositório de drivers (local ou rede)
- [ ] Buscar drivers mais recentes
- [ ] Perguntar antes de atualizar
- [ ] Criar backup antes de atualizar
- [ ] Rollback automático se atualização falhar
- [ ] Criar comando shell `driver update <device_id>`

### 6.2 Assinatura e Segurança

- [ ] Criar sistema de assinatura de drivers (hash + chave)
- [ ] Validar assinatura antes de carregar módulo
- [ ] Bloquear drivers não assinados (modo restrito)
- [ ] Criar lista de drivers confiáveis (whitelist)
- [ ] Criar lista de drivers bloqueados (blacklist)
- [ ] Log de tentativas de carregamento não autorizado
- [ ] Alertar usuário sobre drivers não confiáveis

### 6.3 Diagnóstico e Relatórios

- [ ] Criar função `device_diagnose()` que testa todos os dispositivos
- [ ] Gerar relatório de status de todos os dispositivos
- [ ] Exportar relatório para arquivo (texto ou CSV)
- [ ] Criar comando shell `device-diag`, `device-report`
- [ ] Mostrar resumo de saúde do sistema na interface
- [ ] Detectar dispositivos com problemas e sugerir soluções

### 6.4 Integração com System Info

- [ ] Criar aba "Dispositivos" no painel System Info (já existente)
- [ ] Mostrar contagem de dispositivos por tipo
- [ ] Mostrar dispositivos com problemas
- [ ] Mostrar uso de recursos (IRQ, ports, memória)
- [ ] Criar gráfico de uso de recursos (barras)
- [ ] Atualizar automaticamente a cada 5 segundos

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| Dispositivos PCI | 256 × 32 × 8 | 256 buses, 32 devices, 8 functions |
| Dispositivos no array | 32 | Array estático `pci_device_t devices[32]` |
| Discos ATA | 2 | Master + slave no barramento primário |
| Resolução VGA | 80×25 | Text mode, sem framebuffer nativo |
| Resolução VESA | até 1920×1080 | Framebuffer via BIOS |
| IRQs disponíveis | 16 | Master PIC (8) + Slave PIC (8) |
| Drivers dinâmicos | Nenhum | Sistema monolítico, drivers compilados no kernel |
| Hotplug | Nenhum | Hardware detectado apenas no boot |
| Mouse | Nenhum | Driver de mouse não existe |
| Rede | Nenhum | Sem stack de rede |
| USB | Nenhum | Sem suporte USB |
| ACPI | Parcial | Campo E820 lido, mas ACPI não parseado |
| Máximo de categorias | 12 | Definido na interface TUI |
| Máximo de dispositivos por categoria | 16 | Limite de memória |

---

## Notas de Implementação

1. **Drivers existentes** — O ZephyrOS já possui drivers de baixo nível para PCI, ATA, Teclado, AC97, PC Speaker, VGA, VESA, Timer, IDT/PIC e TSS. Estes são compilados estaticamente no kernel.

2. **Sem drivers dinâmicos** — O sistema é monolítico. Não há mecanismo de carregar drivers em runtime. Para implementar gerenciamento real, seria necessário criar um sistema de módulos.

3. **Sem hotplug** — O hardware é detectado uma vez no boot. Não há suporte a conexão/desconexão de dispositivos em tempo de execução.

4. **Sem mouse** — O sistema funciona apenas com teclado PS/2. A interface TUI deve ser totalmente navegável por teclado.

5. **Sem rede** — Não há stack de rede. ZephyrOS Update, drivers online e exportação remota não são possíveis.

6. **Adaptação do conceito** — O documento original descreve o Device Manager do ZephyrOS. Para o ZephyrOS, o conceito é adaptado para um gerenciador de dispositivos que mostra o hardware detectado e permite operações básicas.

7. **Integração existente** — Os dados já estão disponíveis nos structs dos drivers (pci_device_t, ata_device_t, ac97_device_t). A interface TUI precisa apenas ler e exibir esses dados de forma organizada.

---

## Referências

- `src/drivers/pci.c` — PCI bus enumeration (137 linhas)
- `src/drivers/ata.c` — ATA PIO driver (196 linhas)
- `src/drivers/keyboard.c` — PS/2 keyboard driver (53 linhas)
- `src/drivers/ac97.c` — AC97 audio driver (197 linhas)
- `src/drivers/speaker.c` — PC Speaker driver (55 linhas)
- `src/drivers/video.c` — VGA text mode driver (170 linhas)
- `src/drivers/vesa.c` — VESA framebuffer driver (392 linhas)
- `src/drivers/timer.c` — PIT timer driver (26 linhas)
- `src/drivers/idt.c` — IDT/IRQ handler (256 linhas)
- `src/boot/boot.asm` — Boot and E820 memory detection
- `src/kernel/kernel.c` — Hardware initialization order
