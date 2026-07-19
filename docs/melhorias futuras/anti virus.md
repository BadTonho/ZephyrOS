# Anti-Virus — ZephyrOS v0.1

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Infraestrutura base (hash/checksum) | 32 | 0 | 0 | 32 |
| 2. Motor de scanning (assinar pattern) | 48 | 0 | 0 | 48 |
| 3. Monitor de processos | 44 | 6 | 0 | 38 |
| 4. Proteção de arquivos | 42 | 0 | 0 | 42 |
| 5. Quarantena e remoção | 38 | 0 | 0 | 38 |
| 6. Interface TUI do Anti-Vírus | 56 | 0 | 0 | 56 |
| 7. Integração com sistema | 34 | 8 | 0 | 26 |
| **TOTAL** | **294** | **14** | **0** | **280** |

**Progresso geral: 5%** (14/294 itens completos)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F7 | Abrir janela do Anti-Vírus |
| F4 | Iniciar scan rápido |
| Shift+F4 | Iniciar scan completo |
| Esc | Fechar janela |
| Tab | Alternar entre seções |
| Setas | Navegar na lista |
| Enter | Selecionar opção |
| Delete | Remover arquivo infectado |
| Space | Pausar/resumir scan |

---

## Fase 1: Infraestrutura Base (Hash/Checksum) ⬜

### 1.1 Funções de Hash

- [ ] Criar módulo `src/security/hash.c` e `hash.h`
- [ ] Implementar CRC32 (para verificação rápida de integridade)
  - [ ] Tabela CRC32 (256 entries)
  - [ ] Função `crc32(data, size)` retorna uint32_t
  - [ ] Função `crc32_file(path)` retorna uint32_t
- [ ] Implementar Adler32 (checksum simples)
  - [ ] Função `adler32(data, size)` retorna uint32_t
- [ ] Implementar FNV-1a (hash rápido para strings)
  - [ ] Função `fnv1a_hash(data, size)` retorna uint32_t
  - [ ] Função `fnv1a_string(str)` retorna uint32_t
- [ ] Implementar MD5 (para assinaturas de vírus)
  - [ ] Struct `md5_context_t` (state, count, buffer)
  - [ ] Função `md5_init(ctx)` para inicializar
  - [ ] Função `md5_update(ctx, data, size)` para processar dados
  - [ ] Função `md5_final(ctx, digest)` para finalizar (16 bytes)
  - [ ] Função `md5(data, size, digest)` atalho (init+update+final)
  - [ ] Função `md5_file(path, digest)` hash de arquivo
  - [ ] Função `md5_to_string(digest, str)` para conversão hex

### 1.2 Assinaturas de Vírus

- [ ] Criar módulo `src/security/signatures.c` e `signatures.h`
- [ ] Criar formato de database de assinaturas:
  ```
  // Formato: hash_md5 = tamanho_mínimo = nome
  d41d8cd98f00b204e9800998ecf8427e = 0 = EmptyFile
  7c4a8d09ca3762af61e59520943dc264 = 4 = TestVirus
  ...
  ```
- [ ] Criar struct `signature_t`:
  ```
  - hash[32]        = "d41d8cd98f00b204..."
  - min_size         = uint32_t
  - name[64]         = "TestVirus.A"
  - severity         = enum (CLEAN, SUSPICIOUS, MALICIOUS, CRITICAL)
  - category         = enum (TROJAN, WORM, RANSOM, ADWARE, SPYWARE, ROOTKIT, UNKNOWN)
  - description[128] = "Descrição da ameaça"
  ```
- [ ] Criar array `signatures[MAX_SIGNATURES]` (máximo 1024 assinaturas)
- [ ] Criar função `sig_load(path)` para carregar database
- [ ] Criar função `sig_save(path)` para salvar database
- [ ] Criar função `sig_add(hash, name, severity)` para adicionar assinatura
- [ ] Criar função `sig_remove(hash)` para remover assinatura
- [ ] Criar função `sig_find(hash)` para buscar assinatura (retorna signature_t*)
- [ ] Criar função `sig_count()` para contar assinaturas carregadas
- [ ] Criar função `sig_export(path)` para exportar database
- [ ] Criar database inicial com assinaturas conhecidas (placeholder)

### 1.3 Base de Dados Local

- [ ] Criar arquivo `/security/signatures.db` no filesystem
- [ ] Criar função `sigdb_init()` para carregar no boot
- [ ] Criar função `sigdb_reload()` para recarregar
- [ ] Criar função `sigdb_update(path)` para importar novas assinaturas
- [ ] Criar função `sigdb_get_version()` para versão do database
- [ ] Criar função `sigdb_get_count()` para total de assinaturas
- [ ] Criar comando shell `sigdb load`, `sigdb reload`, `sigdb info`

---

## Fase 2: Motor de Scanning (Pattern Matching) ⬜

### 2.1 Scanner de Arquivos

- [ ] Criar módulo `src/security/scanner.c` e `scanner.h`
- [ ] Criar enum `scan_result_t`:
  ```
  SCAN_CLEAN = 0
  SCAN_SUSPICIOUS = 1
  SCAN_MALICIOUS = 2
  SCAN_ERROR = 3
  SCAN_TIMEOUT = 4
  ```
- [ ] Criar struct `scan_result_t`:
  ```
  - file_path[64]    = "/programs/game.exe"
  - file_size        = uint32_t
  - file_hash[33]    = "d41d8cd98f00b204..."
  - result           = enum scan_result_t
  - signature_name[64]= "TestVirus.A"
  - severity         = enum severity
  - category         = enum category
  - action_taken     = enum (NONE, QUARANTINED, DELETED, CLEANED)
  ```
- [ ] Criar função `scan_file(path)` para scan de arquivo individual
  - [ ] Ler arquivo para buffer (usando `fs_read_file()`)
  - [ ] Calcular MD5 do conteúdo
  - [ ] Verificar tamanho mínimo
  - [ ] Buscar hash no database de assinaturas
  - [ ] Retornar resultado (CLEAN/SUSPICIOUS/MALICIOUS)
- [ ] Criar função `scan_buffer(data, size)` para scan de buffer em memória
- [ ] Criar função `scan_directory(path)` para scan recursivo de diretório
  - [ ] Listar arquivos com `fs_list_dir()` ou `fs_get_file_count()`
  - [ ] Para cada arquivo, chamar `scan_file()`
  - [ ] Para cada subdiretório, chamar recursivamente
  - [ ] Retornar lista de resultados
- [ ] Criar função `scan_full()` para scan completo do disco
  - [ ] Começar do root "/"
  - [ ] Escanear todos os arquivos
  - [ ] Mostrar progresso (arquivo atual, count, percentual)
  - [ ] Retornar lista de resultados
- [ ] Criar função `scan_quick()` para scan rápido
  - [ ] Escanear apenas diretórios críticos: /boot, /programs, /system
  - [ ] Escanear apenas arquivos executáveis (.bin, .exe, .com)
  - [ ] Pular arquivos grandes (> 1MB)
- [ ] Criar função `scan_custom(path_list[])` para scan de lista específica

### 2.2 Detecção Heurística

- [ ] Criar módulo `src/security/heuristic.c` e `heuristic.h`
- [ ] Criar regras heurísticas:
  - [ ] Arquivo com tamanho 0 bytes (suspeito)
  - [ ] Arquivo com extensão executável mas conteúdo não-ELF
  - [ ] Arquivo com nome duplicado em diretórios diferentes
  - [ ] Arquivo modificado recentemente (últimas 24h)
  - [ ] Arquivo com permissões incorretas
  - [ ] Arquivo muito grande (> 10MB para executável)
  - [ ] Arquivo com strings suspeitas (payload, shellcode, etc.)
  - [ ] Arquivo com assinatura conocida mas hash diferente (variante)
- [ ] Criar função `heuristic_analyze(file_info)` retorna score de risco (0-100)
- [ ] Criar função `heuristic_check_strings(data, size)` para procurar padrões
- [ ] Criar função `heuristic_check_entropy(data, size)` para entropia (detectar packing)
- [ ] Criar limiares: 0-30=limpo, 31-60=suspeito, 61-100=malicioso

### 2.3 Scanner em Tempo Real

- [ ] Criar módulo `src/security/realtime.c` e `realtime.h`
- [ ] Criar função `realtime_init()` para inicializar
- [ ] Criar função `realtime_enable()` para ativar proteção
- [ ] Criar função `realtime_disable()` para desativar
- [ ] Implementar hook no filesystem:
  - [ ] Hook `fs_write_file()` para scanar antes de escrever
  - [ ] Hook `fs_create_dir_entry()` para scanar novos arquivos
  - [ ] Bloquear escrita se resultado for MALICIOUS
- [ ] Implementar hook no processo:
  - [ ] Hook `process_create()` para scanar executável antes de criar
  - [ ] Bloquear criação se executável for infectado
- [ ] Criar função `realtime_scan_on_access(path)` para scan sob demanda
- [ ] Criar log de eventos em tempo real (`/security/realtime.log`)
- [ ] Criar notificações no sistema (barra de status ou overlay)

### 2.4 Filtros de Scanning

- [ ] Criar struct `scan_options_t`:
  ```
  - include_hidden     = bool (default: true)
  - include_system     = bool (default: true)
  - include_readonly   = bool (default: true)
  - max_file_size      = uint32_t (default: 10MB)
  - extensions[]       = {".bin", ".exe", ".com", ".sys", ".dll"}
  - skip_extensions[]  = {".txt", ".bmp", ".wav"}
  - follow_symlinks    = bool (default: false)
  - scan_archives      = bool (default: false)
  ```
- [ ] Criar função `scan_set_options(options)` para configurar
- [ ] Criar função `scan_get_options()` para obter opções atuais
- [ ] Criar opção de exclusão por path (skip list)

---

## Fase 3: Monitor de Processos ⬜

### 3.1 Monitor Básico (já existe parcialmente)

- [x] Process table com 64 slots — `process.c:7-10`
- [x] States: UNUSED, READY, RUNNING, BLOCKED, ZOMBIE — `process.h:11-17`
- [x] CPU% por processo no Task Manager — `taskmanager.c:217-223`
- [x] Kill process com Delete — `taskmanager.c:440`
- [x] Proteção do PID 1 — `taskmanager.c:440`
- [x] Lista de processos no shell (`procs`) — `shell.c:183-206`
- [ ] Criar log de criação de processos
- [ ] Criar log de destruição de processos
- [ ] Criar log de mudanças de estado

### 3.2 Detecção de Processos Suspeitos

- [ ] Criar módulo `src/security/protection.c` e `protection.h`
- [ ] Criar regras de detecção:
  - [ ] Processo usando CPU > 90% por mais de 30 segundos
  - [ ] Processo usando memória > 50% do total
  - [ ] Processo criando muitos filhos (> 10)
  - [ ] Processo acessando áreas de memória proibidas
  - [ ] Processo com nome suspeito (lista known-bad)
  - [ ] Processo modificando arquivos do sistema
  - [ ] Processo se renomeando repetidamente
- [ ] Criar função `protection_check_process(pid)` retorna score de risco
- [ ] Criar função `protection_monitor_process(pid)` para monitorar continuamente
- [ ] Criar função `protection_alert(process, rule)` para notificar
- [ ] Criar log de alertas em `/security/process-alerts.log`

### 3.3 Proteção de Processos Críticos

- [ ] Criar lista de processos protegidos:
  - [ ] PID 1 (init) — não pode ser terminado
  - [ ] Shell — não pode ser terminado por processos não-admin
  - [ ] Task Manager — não pode ser terminado
  - [ ] Window Manager — não pode ser terminado
  - [ ] Anti-Virus — não pode ser terminado
- [ ] Criar função `protection_is_protected(pid)` retorna bool
- [ ] Criar função `protection_block_kill(attacker_pid, target_pid)` para bloquear
- [ ] Integrar com `process_destroy()` para verificar antes de destruir
- [ ] Integrar com Task Manager para mostrar "PROTEGIDO" em processos críticos

### 3.4 Sandbox Básico

- [ ] Criar módulo `src/security/sandbox.c` e `sandbox.h`
- [ ] Criar flags de sandbox por processo:
  ```
  - CAN_READ_FILES     = bit 0
  - CAN_WRITE_FILES    = bit 1
  - CAN_CREATE_PROCESS = bit 2
  - CAN_ACCESS_NETWORK = bit 3 (futuro)
  - CAN_ACCESS_DEVICE  = bit 4
  ```
- [ ] Criar função `sandbox_create(rules)` para criar sandbox
- [ ] Criar função `sandbox_check(process_id, action)` para verificar permissão
- [ ] Criar função `sandbox_violate(process_id, action)` para registrar violação
- [ ] Criar modo "restrito" para processos não-confiáveis
- [ ] Integrar com filesystem para verificar permissões de escrita
- [ ] Integrar com process manager para verificar permissões de criação

---

## Fase 4: Proteção de Arquivos ⬜

### 4.1 Integridade de Arquivos

- [ ] Criar módulo `src/security/integrity.c` e `integrity.h`
- [ ] Criar struct `file_integrity_t`:
  ```
  - path[64]          = "/boot/kernel.bin"
  - crc32             = uint32_t
  - md5[33]           = "d41d8cd98f00b204..."
  - size              = uint32_t
  - timestamp         = uint32_t
  - is_critical       = bool (arquivos essenciais do sistema)
  ```
- [ ] Criar database de integridade (`/security/integrity.db`)
- [ ] Criar função `integrity_baseline(path)` para criar baseline
  - [ ] Calcular hash de todos os arquivos críticos
  - [ ] Salvar no database
- [ ] Criar função `integrity_verify(path)` para verificar
  - [ ] Recalcular hash atual
  - [ ] Comparar com baseline
  - [ ] Retornar CLEAN se igual, TAMPERED se diferente
- [ ] Criar função `integrity_verify_all()` para verificar todos
- [ ] Criar função `integrity_update(path)` para atualizar baseline
- [ ] Criar comando shell `integrity check`, `integrity update`, `integrity status`

### 4.2 Proteção de Arquivos Críticos

- [ ] Criar lista de arquivos protegidos:
  - [ ] `/boot/kernel.bin` — kernel do sistema
  - [ ] `/boot/boot.bin` — boot sector
  - [ ] `/security/signatures.db` — database de assinaturas
  - [ ] `/security/integrity.db` — database de integridade
  - [ ] `/system/*` — arquivos do sistema
- [ ] Criar função `protect_file(path)` para marcar como protegido
- [ ] Criar função `is_protected(path)` para verificar
- [ ] Criar função `unprotect_file(path)` para desproteger (admin only)
- [ ] Bloquear escrita em arquivos protegidos (exceto via update)
- [ ] Bloquear exclusão de arquivos protegidos
- [ ] Alertar quando arquivo protegido é modificado

### 4.3 Monitor de Mudanças

- [ ] Criar módulo `src/security/watcher.c` e `watcher.h`
- [ ] Criar struct `watch_entry_t`:
  ```
  - path[64]       = "/programs/"
  - events[]       = {CREATE, MODIFY, DELETE, RENAME}
  - callback       = void (*handler)(event, path)
  - enabled        = bool
  ```
- [ ] Criar função `watcher_add(path, events, callback)` para monitorar
- [ ] Criar função `watcher_remove(path)` para parar monitoramento
- [ ] Criar função `watcher_enable(path)` / `watcher_disable(path)`
- [ ] Integrar com filesystem para detectar mudanças
- [ ] Criar log de mudanças em `/security/file-changes.log`
- [ ] Criar comando shell `watch list`, `watch add <path>`, `watch remove <path>`

### 4.4 Backup de Arquivos Críticos

- [ ] Criar função `backup_critical_files()` para backup automático
- [ ] Criar diretório `/security/backups/` para armazenar
- [ ] Criar manifest de backup (`backup-manifest.txt`)
- [ ] Criar função `restore_critical_files()` para restaurar
- [ ] Criar função `backup_list()` para listar backups
- [ ] Criar função `backup_cleanup(keep_count)` para remover backups antigos
- [ ] Limitar a 5 backups (auto-delete mais antigo)

---

## Fase 5: Quarantena e Remoção ⬜

### 5.1 Sistema de Quarantena

- [ ] Criar módulo `src/security/quarantine.c` e `quarantine.h`
- [ ] Criar diretório `/quarantine/` no filesystem
- [ ] Criar struct `quarantine_entry_t`:
  ```
  - original_path[64]  = "/programs/game.exe"
  - quarantine_path[64]= "/quarantine/game.exe.2024-01-15"
  - file_hash[33]       = "abc123..."
  - signature_name[64]  = "Trojan.Generic"
  - timestamp           = uint32_t
  - reason              = enum (VIRUS, SUSPICIOUS, USER_ACTION)
  - restored            = bool
  ```
- [ ] Criar função `quarantine_file(path, reason)` para mover para quarantena
  - [ ] Renomear arquivo (adicionar timestamp e extensão .quar)
  - [ ] Mover para `/quarantine/`
  - [ ] Registrar no manifest de quarantena
  - [ ] Registrar no log de eventos
- [ ] Criar função `restore_file(quarantine_id)` para restaurar
  - [ ] Verificar hash original
  - [ ] Mover de volta ao path original
  - [ ] Atualizar manifest
- [ ] Criar função `delete_quarantined(quarantine_id)` para deletar permanentemente
- [ ] Criar função `quarantine_list()` para listar arquivos em quarantena
- [ ] Criar função `quarantine_info(id)` para detalhes de um arquivo
- [ ] Criar função `quarantine_clean()` para limpar todos (mais de 30 dias)

### 5.2 Remoção de Malware

- [ ] Criar função `remove_malware(path, result)` para remover
  - [ ] Se arquivo é infectado mas legítimo: limpar (remover payload)
  - [ ] Se arquivo é puramente malicioso: deletar
  - [ ] Se arquivo é suspeito: mover para quarantena
- [ ] Criar função `clean_file(path)` para limpar infecção
  - [ ] Identificar payload malicioso
  - [ ] Remover payload do arquivo
  - [ ] Verificar se arquivo continua funcional
  - [ ] Recalcular hash
- [ ] Criar função `delete_malware(path)` para deletar permanentemente
  - [ ] Confirmar com usuário
  - [ ] Deletar arquivo
  - [ ] Limpar entradas FAT
  - [ ] Sobrescrever setores (opcional, para segurança)

### 5.3 Log de Eventos

- [ ] Criar arquivo `/security/events.log`
- [ ] Criar struct `security_event_t`:
  ```
  - timestamp    = uint32_t
  - event_type   = enum (SCAN_COMPLETE, THREAT_FOUND, QUARANTINED, RESTORED, DELETED, etc.)
  - severity     = enum (INFO, WARNING, ERROR, CRITICAL)
  - details[128] = "Trojan.Generic encontrado em /programs/game.exe"
  - action_taken = enum (NONE, QUARANTINED, DELETED, CLEANED, BLOCKED)
  ```
- [ ] Criar função `event_log_add(event)` para registrar
- [ ] Criar função `event_log_read(count)` para ler últimos N eventos
- [ ] Criar função `event_log_clear()` para limpar log
- [ ] Criar função `event_log_export(path)` para exportar
- [ ] Criar comando shell `security log`, `security log clear`

### 5.4 Notificações

- [ ] Criar sistema de notificações do anti-vírus
- [ ] Criar notificação na barra de tarefas (ícone de escudo)
- [ ] Criar popup de alerta quando ameaça encontrada
- [ ] Criar som de alerta (PC Speaker beep)
- [ ] Criar opção de silenciar notificações
- [ ] Criar histórico de notificações

---

## Fase 6: Interface TUI do Anti-Vírus ⬜

### 6.1 Janela Principal

- [ ] Criar módulo `src/security/antivirus_ui.c` e `antivirus_ui.h`
- [ ] Criar janela "ZephyrOS Anti-Virus"
- [ ] Mostrar status atual: "Protegido" ou "Em risco"
- [ ] Mostrar: Último scan, Ameaças encontradas, Arquivos escaneados
- [ ] Botão: "Scan Rápido" (F4)
- [ ] Botão: "Scan Completo" (Shift+F4)
- [ ] Botão: "Quarantena"
- [ ] Botão: "Configurações"
- [ ] Botão: "Atualizar Assinaturas"
- [ ] Integrar com window manager
- [ ] Registrar na taskbar com ícone de escudo
- [ ] Registrar no menu Start

### 6.2 Tela de Scan

- [ ] Criar tela de progresso do scan
- [ ] Mostrar: Arquivo atual sendo escaneado
- [ ] Mostrar: Progresso (X de Y arquivos, %)
- [ ] Mostrar: Tempo decorrido e estimado
- [ ] Mostrar: Ameaças encontradas até agora
- [ ] Barra de progresso com cores (verde/vermelho)
- [ ] Botão "Pausar" / "Continuar"
- [ ] Botão "Cancelar"
- [ ] Mostrar estatísticas em tempo real (arquivos/s, MB/s)

### 6.3 Tela de Resultados

- [ ] Criar tela de resultados do scan
- [ ] Mostrar resumo: Total, Limpos, Suspeitos, Maliciosos, Erros
- [ ] Lista de ameaças encontradas com detalhes
- [ ] Para cada ameaça:
  - [ ] Nome do arquivo
  - [ ] Tipo de ameaça (Trojan, Worm, etc.)
  - [ ] Severidade (cor: verde/amarelo/vermelho)
  - [ ] Ação recomendada (Quarantena, Deletar, Limpar)
- [ ] Botão "Ação em Lote" (aplicar mesma ação a todas)
- [ ] Botão "Detalhes" para ver informações completas
- [ ] Botão "Exportar Relatório"

### 6.4 Tela de Quarantena

- [ ] Criar tela "Quarantena"
- [ ] Lista de arquivos em quarantena
- [ ] Para cada arquivo:
  - [ ] Nome original
  - [ ] Data de quarentena
  - [ ] Tipo de ameaça
  - [ ] Tamanho
- [ ] Botão "Restaurar" (mover de volta)
- [ ] Botão "Deletar Permanentemente"
- [ ] Botão "Ver Detalhes"
- [ ] Botão "Limpar Antigos" (> 30 dias)

### 6.5 Tela de Configurações

- [ ] Criar painel de configurações do anti-vírus
- [ ] Opção: "Proteção em tempo real" (ligar/desligar)
- [ ] Opção: "Scan automático ao acessar arquivo" (ligar/desligar)
- [ ] Opção: "Scan automático ao iniciar" (ligar/desligar)
- [ ] Opção: "Notificações" (ligar/desligar)
- [ ] Opção: "Som de alerta" (ligar/desligar)
- [ ] Opção: "Ação padrão" (Quarantena/Deletar/Perguntar)
- [ ] Opção: "Excluir arquivos maiores que" (tamanho)
- [ ] Opção: "Incluir arquivos de sistema" (ligar/desligar)
- [ ] Opção: "Incluir arquivos ocultos" (ligar/desligar)
- [ ] Salvar em `/security/config.txt`

### 6.6 Tela de Status

- [ ] Criar tela "Status do Sistema"
- [ ] Mostrar: Versão do anti-vírus
- [ ] Mostrar: Versão do database de assinaturas
- [ ] Mostrar: Total de assinaturas carregadas
- [ ] Mostrar: Último update do database
- [ ] Mostrar: Total de scans realizados
- [ ] Mostrar: Total de ameaças encontradas (histórico)
- [ ] Mostrar: Arquivos em quarantena
- [ ] Mostrar: Integridade do sistema (OK/COMPROMISED)
- [ ] Botão "Atualizar Database"
- [ ] Botão "Verificar Integridade"

### 6.7 Integração com Shell

- [ ] Comando `av` — abrir janela do anti-vírus
- [ ] Comando `scan <path>` — scan rápido de arquivo/diretório
- [ ] Comando `scan-full` — scan completo do disco
- [ ] Comando `scan-quick` — scan rápido (apenas executáveis)
- [ ] Comando `quarantine list` — listar quarantena
- [ ] Comando `quarantine restore <id>` — restaurar arquivo
- [ ] Comando `quarantine delete <id>` — deletar permanentemente
- [ ] Comando `quarantine clean` — limpar quarantena antiga
- [ ] Comando `integrity check` — verificar integridade
- [ ] Comando `integrity update` — atualizar baseline
- [ ] Comando `security status` — mostrar status
- [ ] Comando `security log` — mostrar log de eventos
- [ ] Comando `sigdb info` — info do database

---

## Fase 7: Integração com Sistema ⬜

### 7.1 Boot Security

- [ ] Criar verificação de integridade do kernel no boot
- [ ] Calcular CRC32 do kernel.bin durante build
- [ ] Salvar hash no setor reservado do disco
- [ ] Verificar hash no boot (antes de carregar kernel)
- [ ] Se hash diferente: mostrar alerta e opção de recovery
- [ ] Criar modo "safe boot" para desabilitar drivers problemáticos

### 7.2 Integração com File Manager

- [ ] Adicionar opção "Scanar este arquivo" no menu de contexto
- [ ] Adicionar opção "Scanar este diretório"
- [ ] Mostrar ícone de status (escudo verde/vermelho) ao lado de cada arquivo
- [ ] Bloquear abertura de arquivo infectado (com popup de alerta)
- [ ] Criar opção "Ver propriedades de segurança"

### 7.3 Integração com Task Manager

- [ ] Adicionar aba "Segurança" no Task Manager
- [ ] Mostrar processos com score de risco
- [ ] Destacar processos suspeitos (cor vermelha)
- [ ] Botão "Quarentenar processo" para processos maliciosos
- [ ] Mostrar estatísticas de segurança (ameaças hoje, semana, total)

### 7.4 Integração com Settings

- [ ] Adicionar categoria "Segurança" no Settings
- [ ] Opção: "Anti-Vírus" (ligar/desabilitar proteção)
- [ ] Opção: "Proteção em tempo real" (ligar/desligar)
- [ ] Opção: "Atualizar assinaturas"
- [ ] Opção: "Verificar integridade do sistema"
- [ ] Opção: "Modo de segurança" (desabilitar drivers não-essenciais)

### 7.5 Integração com Window Manager

- [ ] Adicionar notificação de segurança na barra de tarefas
- [ ] Mostrar ícone de escudo (verde/vermelho/amarelo)
- [ ] Popup de alerta quando ameaça encontrada
- [ ] Bloquear janelas de processos maliciosos (se possível)

### 7.6 Integração com Power Manager

- [ ] Não permitir desligamento durante scan ativo
- [ ] Não permitir sleep durante scan
- [ ] Criar scan agendado (opção de agendar scans)
- [ ] Criar scan na inicialização (após boot)

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| Máximo de assinaturas | 1024 | Array estático em memória |
| Tamanho do database | ~64KB | Limitado por RAM |
| Máximo de arquivos em scan | 224 | Limitado por FAT12 root dir |
| Tamanho máximo de arquivo | 1MB | Buffer para leitura em scan |
| Máximo de processos monitorados | 16 | Limitado por RAM |
| Máximo de arquivos em quarantena | 32 | Diretório /quarantine/ |
| Máximo de eventos no log | 256 | Ring buffer em memória |
| Máximo de backups | 5 | Auto-delete mais antigo |
| Máximo de watch entries | 16 | Monitor de mudanças |
| Scan speed | ~100 arquivos/s | Limitado por I/O de disco |
| Hash de 128-bit | MD5 | Sem SHA-256 (muito lento para bare-metal) |
| Sem rede | Nenhum | Atualização manual de assinaturas |
| Sem criptografia | Nenhum | Sem cifra de arquivos em quarantena |
| Sem sandbox real | Limitado | Todos os processos em Ring 0 |
| Sem detecção de rootkit | Nenhum | Sem acesso a hardware profundo |
| Sem behavior monitoring | Básico | Apenas regras estáticas |
| Sem sandbox de rede | Nenhum | Sem stack de rede |

---

## Notas de Implementação

1. **Sem rede** — O ZephyrOS não possui stack de rede. Atualizações de assinaturas devem ser manuais (usuário baixa no PC e copia para pendrive).

2. **MD5 em vez de SHA-256** — SHA-256 é mais seguro mas requer mais computação. MD5 é suficiente para um anti-vírus educacional. Para uso real, SHA-256 seria necessário.

3. **Sem Ring 3** — Todos os processos rodam em Ring 0 (kernel mode). Não há separação real de privilégios. O sandbox é apenas lógico, não enforced pelo hardware.

4. **Scan por hash** — O scan atual compara MD5 do arquivo inteiro com assinaturas conhecidas. Não há scan por padrões dentro do arquivo (signature-based detection would require pattern matching).

5. **Heurísticas limitadas** — As heurísticas são baseadas em regras estáticas (tamanho, extensão, strings). Não há análise de comportamento em tempo real.

6. **Quarantena sem criptografia** — Arquivos em quarantena são apenas renomeados e movidos. Não são cifrados. Um atacante poderia restaurá-los manualmente.

7. **Integridade limitada** — A verificação de integridade usa CRC32 que é rápido mas não criptograficamente seguro. Para uso real, seria necessário HMAC ou assinatura digital.

8. **Sem boot secure** — O boot loader não verifica a integridade do kernel. Um atacante poderia modificar o kernel.bin sem detecção.

9. **Integração existente** — O filesystem já suporta operações completas (read/write/delete/list). O scanner precisa apenas usar essas APIs.

10. **Educacional** — Este anti-vírus é para fins educacionais. Não deve ser usado em produção sem melhorias significativas de segurança.

---

## Referências

- `src/fs/fs.c` — Unified FS API (194 linhas)
- `src/fs/fat12.c` — FAT12 filesystem (1145 linhas)
- `src/process/process.c` — Process manager (234 linhas)
- `src/shell/taskmanager.c` — Task Manager (456 linhas)
- `src/memory/memory.c` — Memory manager (206 linhas)
- `src/memory/compress.c` — LZSS compression (220 linhas)
- `src/drivers/ata.c` — ATA disk driver (196 linhas)
- `src/shell/shell.c` — Shell commands (518 linhas)
- `src/kernel/kernel.c` — Kernel init (203 linhas)
- `src/kernel/panic.c` — Kernel panic (26 linhas)
- `src/settings/settings.c` — Settings panel (549 linhas)
