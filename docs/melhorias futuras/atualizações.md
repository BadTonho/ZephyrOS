# Atualizações do Sistema — ZephyrOS v0.1

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Infraestrutura base | 38 | 18 | 0 | 20 |
| 2. Sistema de versionamento | 28 | 0 | 0 | 28 |
| 3. Formato de pacote de atualização | 42 | 0 | 0 | 42 |
| 4. Instalador de atualizações | 56 | 0 | 0 | 56 |
| 5. ISO de instalação | 34 | 0 | 0 | 34 |
| 6. Interface TUI de atualizações | 48 | 0 | 0 | 48 |
| 7. Integração com GitHub | 36 | 0 | 0 | 36 |
| **TOTAL** | **282** | **18** | **0** | **264** |

**Progresso geral: 6%** (18/282 itens completos)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F6 | Abrir janela de Atualizações |
| F2 | Verificar atualizações |
| F5 | Instalar atualização selecionada |
| Setas | Navegar na lista |
| Enter | Selecionar/instalar |
| Esc | Fechar janela |
| Tab | Alternar entre seções |
| Delete | Remover atualização instalada |

---

## Fase 1: Infraestrutura Base ✅

### 1.1 Sistema de Arquivos

- [x] Driver ATA PIO completo (leitura/escrita de setores) — `ata.c:127-196`
- [x] FAT12 driver completo (read/write/delete/list) — `fat12.c:1-1145`
- [x] FAT32 driver completo (read/write/delete/list) — `fat32.c:1-976`
- [x] API unificada de filesystem — `fs.c:1-194`
- [x] `fs_read_file()` para leitura — `fs.c:27-34`
- [x] `fs_write_file()` para escrita — `fs.c:36-43`
- [x] `fs_delete_file()` para exclusão — `fs.c:45-52`
- [x] `fs_list_dir()` para listagem — `fs.c:54-61`
- [x] `fs_create_dir_entry()` para criar diretórios — `fs.c:163-172`
- [x] `fs_write_file_in_dir()` para escrita em subdiretório — `fs.c:174-183`
- [x] `fs_delete_file_in_dir()` para exclusão em subdiretório — `fs.c:185-194`
- [x] Suporte a subdiretórios (resolução de path) — `fs.c:117-130`
- [ ] Criar função `fs_copy_file(src, dst)` para cópia de arquivos
- [ ] Criar função `fs_move_file(src, dst)` para movimentação
- [ ] Criar função `fs_file_exists(path)` para verificação
- [ ] Criar função `fs_get_file_size(path)` para obter tamanho
- [ ] Criar função `fs_mkdir(path)` para criar diretório

### 1.2 Memória

- [x] Gerenciador de memória com bitmap — `memory.c:85-136`
- [x] Alocador de heap (first-fit) — `memory.c:138-201`
- [x] API `kmalloc()` / `kfree()` — `memory.c:174-202`
- [x] Paging system — `paging.c:1-94`
- [x] Deteção E820 — `boot.asm:67-94`
- [ ] Criar função `memory_get_largest_block()` para blocos grandes
- [ ] Criar buffer ring para streaming de dados

### 1.3 Compressão

- [x] Compressor LZSS implementado — `compress.c:1-220`
- [x] API `compress_data()` / `decompress_data()` — `compress.c`
- [x] Stats de compressão — `compress.c:169-220`
- [x] Comando shell `stats` — `shell.c:517-518`
- [ ] Criar função `compress_file(src_path, dst_path)` para comprimir arquivo
- [ ] Criar função `decompress_file(src_path, dst_path)` para descomprimir

### 1.4 PCI e Hardware

- [x] PCI enumeration completa — `pci.c:91-115`
- [x] Leitura/Escrita de config space — `pci.c:37-47`
- [x] Busca por class/vendor — `pci.c:117-137`
- [x] ATA disk detection — `ata.c:61-118`
- [x] Teclado PS/2 — `keyboard.c:1-53`
- [x] VGA Text Mode — `video.c:1-170`
- [x] VESA Framebuffer — `vesa.c:1-392`
- [ ] Detectar placa de rede via PCI (class 0x02, subclass 0x00)
- [ ] Criar estrutura `nic_device_t` para NIC detection

### 1.5 Shell e Comandos

- [x] Shell funcional com parsing de comandos — `shell.c:1-518`
- [x] Comando `reboot` — `shell.c:280-285`
- [x] Comando `shutdown` — `shell.c:287-292`
- [x] Comando `mem` — `shell.c:168-181`
- [x] Comando `procs` — `shell.c:183-206`
- [x] Comando `help` — `shell.c:80-104`
- [ ] Criar comando `update` — gerenciar atualizações
- [ ] Criar comando `version` — mostrar versão atual
- [ ] Criar comando `changelog` — mostrar changelog

---

## Fase 2: Sistema de Versionamento

### 2.1 Header de Versão

- [ ] Criar arquivo `src/include/version.h`
- [ ] Definir `#define ZEPHYROS_VERSION "0.2.0"` (ou próxima versão)
- [ ] Definir `#define ZEPHYROS_VERSION_MAJOR 0`
- [ ] Definir `#define ZEPHYROS_VERSION_MINOR 2`
- [ ] Definir `#define ZEPHYROS_VERSION_PATCH 0`
- [ ] Definir `#define ZEPHYROS_BUILD_DATE __DATE__ __TIME__`
- [ ] Definir `#define ZEPHYROS_BUILD_NUMBER` (auto-increment ou timestamp)
- [ ] Criar função `version_get_string()` retorna "ZephyrOS vX.Y.Z"
- [ ] Criar função `version_compare(v1, v2)` retorna -1, 0, 1
- [ ] Criar função `version_is_newer(current, available)` retorna bool
- [ ] Integrar com `kernel.c` para mostrar versão no boot
- [ ] Integrar com `settings.c` para mostrar versão no About

### 2.2 Metadados de Build

- [ ] Criar arquivo `build-info.txt` gerado durante build
- [ ] Incluir: versão, data de build, hash do git (se disponível)
- [ ] Incluir: compilador usado, flags de otimização
- [ ] Incluir: hardware alvo (i686)
- [ ] Criar Makefile target `build-info` para gerar arquivo
- [ ] Integrar com `version.h` via `#include` ou `#define`

### 2.3 Changelog

- [ ] Criar arquivo `CHANGELOG.md` no repositório
- [ ] Formato: versão, data, lista de mudanças
- [ ] Criar função `changelog_get_entries()` para ler do disco
- [ ] Criar função `changelog_add_entry(version, changes)` para adicionar
- [ ] Criar comando shell `changelog` para mostrar
- [ ] Integrar com janela de atualizações

---

## Fase 3: Formato de Pacote de Atualização

### 3.1 Design do Formato

- [ ] Criar módulo `src/updater/package.c` e `package.h`
- [ ] Definir extensão: `.zephyros` ou `.mup` (ZephyrOS Update Package)
- [ ] Criar struct `package_header_t`:
  ```
  - magic[4]        = "MUPK"
  - version[16]     = "0.2.0"
  - prev_version[16]= "0.1.0"
  - description[64] = "Descrição da atualização"
  - author[32]      = "Nome do autor"
  - timestamp       = uint32_t (epoch)
  - file_count      = uint32_t
  - total_size      = uint32_t
  - checksum        = uint32_t (CRC32 ou adler32)
  - flags           = uint32_t (bit 0=reboot_required, bit 1=backup_required)
  ```
- [ ] Criar struct `package_file_entry_t`:
  ```
  - filename[32]    = "KERNEL.BIN"
  - offset          = uint32_t (posição no pacote)
  - size            = uint32_t (tamanho original)
  - compressed_size = uint32_t (tamanho comprimido)
  - checksum        = uint32_t
  - flags           = uint32_t (bit 0=overwrite, bit 1=optional)
  ```
- [ ] Criar função `package_create(header, files, output_path)` para empacotar
- [ ] Criar função `package_open(path)` para ler pacote
- [ ] Criar função `package_validate(path)` para verificar integridade
- [ ] Criar função `package_extract_file(package, index, output_path)` para extrair

### 3.2 Empacotador (Host-side Tool)

- [ ] Criar script `tools/make-package.sh` (Linux/Mac) ou `tools/make-package.bat` (ZephyrOS)
- [ ] Ler lista de arquivos de manifesto `update-manifest.txt`
- [ ] Empacotar arquivos com header + entries + dados
- [ ] Opcionalmente comprimir cada arquivo com LZSS
- [ ] Calcular checksums e preencher header
- [ ] Gerar pacote `.zephyros` pronto para upload

### 3.3 Manifesto de Atualização

- [ ] Criar formato `update-manifest.txt`:
  ```
  # ZephyrOS Update Manifest
  version=0.2.0
  previous=0.1.0
  description=Correções de bugs e novas funcionalidades
  author=ZephyrOS Team
  reboot_required=yes
  # Arquivos incluídos:
  file:kernel.bin
  file:shell.bin
  file:docs/changelog.txt
  ```
- [ ] Criar parser para ler manifesto
- [ ] Criar função `manifest_validate(manifest)` para verificar campos obrigatórios
- [ ] Integrar com empacotador

### 3.4 Verificação de Integridade

- [ ] Implementar CRC32 ou Adler32 para checksum
- [ ] Criar função `checksum_data(data, size)` retorna uint32_t
- [ ] Criar função `checksum_file(path)` retorna uint32_t
- [ ] Verificar checksum ao abrir pacote
- [ ] Verificar checksum de cada arquivo extraído
- [ ] Mostrar erro se checksum falhar

---

## Fase 4: Instalador de Atualizações

### 4.1 Módulo de Atualização

- [ ] Criar módulo `src/updater/updater.c` e `updater.h`
- [ ] Criar enum `update_status_t` (IDLE, DOWNLOADING, EXTRACTING, APPLYING, REBOOTING, ERROR)
- [ ] Criar struct `update_context_t` (status, progress, current_file, errors)
- [ ] Criar função `updater_init()` para inicializar
- [ ] Criar função `updater_check(package_path)` para verificar pacote
- [ ] Criar função `updater_apply(package_path)` para aplicar atualização
- [ ] Criar função `updater_get_status()` retorna status atual
- [ ] Criar função `updater_get_progress()` retorna percentual (0-100)

### 4.2 Processo de Instalação

- [ ] Fase 1: Validação do pacote (magic, version, checksum)
- [ ] Fase 2: Verificar compatibilidade (previous version match)
- [ ] Fase 3: Criar backup dos arquivos atuais
  - [ ] Criar diretório `/backup/YYYY-MM-DD/`
  - [ ] Copiar arquivos que serão substituídos
  - [ ] Criar arquivo `backup-manifest.txt` com lista
- [ ] Fase 4: Extrair e aplicar arquivos
  - [ ] Para cada arquivo no pacote:
    - [ ] Extrair dados (descomprimir se necessário)
    - [ ] Verificar checksum
    - [ ] Copiar para destino (kernel.bin → /boot/, etc.)
- [ ] Fase 5: Atualizar metadados
  - [ ] Atualizar `version.h` com nova versão
  - [ ] Atualizar `CHANGELOG.md`
  - [ ] Criar arquivo `update-log.txt` com resultado
- [ ] Fase 6: Pós-atualização
  - [ ] Se reboot_required, mostrar mensagem e aguardar confirmação
  - [ ] Oferecer opção de reiniciar imediatamente ou depois
  - [ ] Criar flag `/update-pending` para verificar no próximo boot

### 4.3 Sistema de Backup

- [ ] Criar função `backup_create(files[])` para criar backup
- [ ] Criar função `backup_list()` para listar backups disponíveis
- [ ] Criar função `backup_restore(backup_id)` para restaurar
- [ ] Criar função `backup_delete(backup_id)` para remover
- [ ] Limitar a 3 backups mais recentes (auto-delete mais antigo)
- [ ] Criar comando shell `update backup`, `update restore`, `update backups`

### 4.4 Rollback

- [ ] Criar função `updater_rollback()` para desfazer última atualização
- [ ] Restaurar arquivos do backup mais recente
- [ ] Restaurar versão anterior
- [ ] Criar comando shell `update rollback`
- [ ] Oferecer rollback automático se atualização falhar

### 4.5 Log de Atualizações

- [ ] Criar arquivo `/update-history.log`
- [ ] Registrar: data, versão antiga → nova, status, erros
- [ ] Criar função `updater_get_history()` para consultar
- [ ] Criar comando shell `update history`
- [ ] Integrar com janela de atualizações

---

## Fase 5: ISO de Instalação

### 5.1 Formato da ISO

- [ ] Criar script `tools/make-iso.sh` para gerar ISO
- [ ] Usar `mkisofs` ou `genisoimage` para criar ISO9660
- [ ] Incluir: boot sector, kernel, filesystem
- [ ] Configurar para boot via BIOS (El Torito)
- [ ] Incluir README de instalação
- [ ] Gerar ISO de ~1.44MB (tamanho do floppy image)

### 5.2 Processo de Instalação

- [ ] Criar módulo `src/installer/installer.c` e `installer.h`
- [ ] Criar interface TUI de instalação (texto)
- [ ] Passo 1: Boas-vindas e seleção de idioma
- [ ] Passo 2: Selecionar disco de destino
- [ ] Passo 3: Criar partição (usar disco inteiro)
- [ ] Passo 4: Format FAT12/FAT32
- [ ] Passo 5: Copiar arquivos do ISO para disco
- [ ] Passo 6: Instalar boot loader
- [ ] Passo 7: Configurações iniciais (nome do PC, etc.)
- [ ] Passo 8: Reiniciar

### 5.3 Boot Loader de Instalação

- [ ] Criar `boot-installer.asm` que carrega instalador
- [ ] Detectar disco de destino
- [ ] Perguntar confirmação antes de formatar
- [ ] Copiar setor a setor do ISO para disco
- [ ] Instalar boot sector no disco destino

### 5.4 Script de Build da ISO

- [ ] Criar `Makefile` target `iso`
- [ ] Compilar kernel + boot loader
- [ ] Criar filesystem FAT12
- [ ] Montar ISO com mkisofs
- [ ] Gerar `zephyros-vX.Y.Z.iso`
- [ ] Criar checksum SHA256 da ISO
- [ ] Criar `zephyros-vX.Y.Z.iso.sha256`

---

## Fase 6: Interface TUI de Atualizações

### 6.1 Janela Principal

- [ ] Criar módulo `src/updater/updater_ui.c` e `updater_ui.h`
- [ ] Criar janela "Atualizações do Sistema"
- [ ] Mostrar versão atual e última disponível
- [ ] Mostrar status: "Sistema atualizado" ou "Atualização disponível"
- [ ] Botão: "Verificar atualizações"
- [ ] Botão: "Instalar atualização"
- [ ] Botão: "Ver histórico"
- [ ] Botão: "Configurações"
- [ ] Integrar com window manager
- [ ] Registrar na taskbar e menu Start

### 6.2 Tela de Verificação

- [ ] Criar tela "Verificando atualizações..."
- [ ] Mostrar progresso (barra de progresso)
- [ ] Verificar pacote local (se fornecido)
- [ ] Verificar versão contra manifest
- [ ] Mostrar resultado: "Atualização disponível" ou "Sistema atualizado"
- [ ] Se disponível, mostrar detalhes (versão, descrição, tamanho)

### 6.3 Tela de Instalação

- [ ] Criar tela de progresso da instalação
- [ ] Mostrar fase atual (Validando → Backup → Extraindo → Instalando → Concluído)
- [ ] Barra de progresso geral (0-100%)
- [ ] Barra de progresso do arquivo atual
- [ ] Nome do arquivo sendo processado
- [ ] Botão "Cancelar" (habilitado apenas em fases seguras)
- [ ] Mensagem de conclusão: "Reinicie para aplicar atualizações"

### 6.4 Tela de Histórico

- [ ] Criar tela "Histórico de Atualizações"
- [ ] Lista de atualizações instaladas (data, versão, status)
- [ ] Detalhes ao selecionar (lista de arquivos, erros)
- [ ] Botão "Restaurar" para rollback
- [ ] Botão "Limpar histórico"

### 6.5 Tela de Configurações

- [ ] Opção: "Verificar automaticamente" (ligar/desligar)
- [ ] Opção: "Instalar automaticamente" (ligar/desligar)
- [ ] Opção: "Criar backup antes de instalar" (ligar/desligar)
- [ ] Opção: "Reiniciar automaticamente" (ligar/desligar)
- [ ] Opção: "Pasta de downloads" (caminho)
- [ ] Salvar configurações em `/update-config.txt`

### 6.6 Integração com Shell

- [ ] Comando `update check` — verificar atualizações
- [ ] Comando `update install <file>` — instalar pacote
- [ ] Comando `update list` — listar atualizações disponíveis
- [ ] Comando `update history` — mostrar histórico
- [ ] Comando `update rollback` — desfazer última atualização
- [ ] Comando `update backup` — criar backup manual
- [ ] Comando `update restore` — restaurar backup
- [ ] Comando `update backups` — listar backups
- [ ] Comando `version` — mostrar versão atual

---

## Fase 7: Integração com GitHub

### 7.1 Estrutura no GitHub

- [ ] Criar repositório no GitHub para releases
- [ ] Organizar releases por versão (v0.1.0, v0.2.0, etc.)
- [ ] Incluir em cada release:
  - [ ] `zephyros-vX.Y.Z.iso` — ISO de instalação
  - [ ] `zephyros-vX.Y.Z.zephyros` — Pacote de atualização
  - [ ] `zephyros-vX.Y.Z.iso.sha256` — Checksum da ISO
  - [ ] `zephyros-vX.Y.Z.zephyros.sha256` — Checksum do pacote
  - [ ] `CHANGELOG.md` — Lista de mudanças
  - [ ] `README.md` — Instruções de instalação/atualização

### 7.2 Download Manual (Sem Rede)

- [ ] Criar instruções no README para download manual
- [ ] Usuário baixa `.zephyros` do GitHub no PC
- [ ] Copia para pendrive FAT12
- [ ] No ZephyrOS, monta pendrive e instala
- [ ] Criar comando `update install /mnt/pendrive/update.zephyros`

### 7.3 Future: Download Automático (Com Rede)

- [ ] Detector de NIC via PCI (class 0x02)
- [ ] Driver RTL8139 (placa de rede comum em VMs)
- [ ] Stack TCP/IP básico (IP, ARP, TCP, DNS)
- [ ] Cliente HTTP simples (GET request)
- [ ] Cliente GitHub API (listar releases, baixar assets)
- [ ] Comando `update fetch` — baixar do GitHub
- [ ] Comando `update download <version>` — baixar versão específica
- [ ] Barra de progresso de download
- [ ] Retry automático em caso de falha

### 7.4 Verificação de Versão

- [ ] Criar arquivo `version-check.json` no GitHub:
  ```json
  {
    "latest": "0.2.0",
    "url": "https://github.com/.../releases/download/v0.2.0/zephyros-v0.2.0.zephyros",
    "sha256": "abc123...",
    "size": 102400,
    "changelog": "Correções de bugs..."
  }
  ```
- [ ] Criar função `updater_check_online()` para verificar (future)
- [ ] Criar função `updater_parse_version_json()` para parse
- [ ] Comparar versão local com remota
- [ ] Mostrar notificação se disponível

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| Tamanho da ISO | ~1.44MB | Formato floppy (boot sector + kernel) |
| Tamanho do pacote | ~500KB | Limitado por RAM disponível para buffer |
| Máximo de arquivos/pacote | 32 | Limitado por memória |
| Máximo de backups | 3 | Auto-delete do mais antigo |
| Checksum | CRC32/Adler32 | Sem criptografia forte |
| Download | Manual | Sem rede (future: RTL8139 + TCP/IP) |
| Versão mínima | 0.1.0 | Primeira versão release |
| Rollback | Último backup | Sem versionamento completo |
| Formato de pacote | .zephyros | Proprietário (não compatível com outros SOs) |
| Instalação | Disco inteiro | Sem particionamento avançado |
| Boot | BIOS only | Sem suporte UEFI |
| Rede | Nenhum | Sem driver de NIC, sem TCP/IP |
| GitHub API | Nenhum | Sem cliente HTTP |
| Criptografia | Nenhum | Sem assinatura digital de pacotes |

---

## Notas de Implementação

1. **Sem rede** — O ZephyrOS não possui stack de rede. O download de atualizações deve ser manual (usuário baixa no PC e copia para pendrive). Para download automático, seria necessário implementar driver de NIC + TCP/IP.

2. **ISO vs Image** — O Makefile atual gera uma raw disk image (`zephyros.img`), não uma ISO. Para criar ISO, seria necessário usar `mkisofs` ou `genisoimage` com El Torito boot.

3. **Formato proprietário** — O pacote `.zephyros` é um formato proprietário simples (header + file entries + data). Não usa formats existentes como .deb, .rpm, ou .appx.

4. **Backup limitado** — O sistema mantém apenas 3 backups para economizar espaço em disco (FAT12 tem limite de 224 arquivos no root).

5. **Sem criptografia** — Pacotes não são assinados digitalmente. Qualquer pessoa pode criar um pacote. Isso é aceitável para uso interno.

6. **Persistência** — Configurações de atualização são salvas em arquivo (`/update-config.txt`). Se o filesystem não suportar escrita, configurações ficam em memória.

7. **Integração existente** — O filesystem já suporta operações completas (read/write/delete/list/create_dir). O updater precisa apenas usar essas APIs.

8. **Rollback** — O rollback restaura arquivos do backup mais recente. Não é versionamento completo (não permite voltar para qualquer versão, apenas a anterior).

---

## Referências

- `src/drivers/ata.c` — ATA PIO disk driver (196 linhas)
- `src/fs/fat12.c` — FAT12 filesystem (1145 linhas)
- `src/fs/fat32.c` — FAT32 filesystem (976 linhas)
- `src/fs/fs.c` — Unified FS API (194 linhas)
- `src/memory/memory.c` — Memory manager (206 linhas)
- `src/memory/compress.c` — LZSS compression (220 linhas)
- `src/shell/shell.c` — Shell commands (518 linhas)
- `src/kernel/kernel.c` — Kernel init (203 linhas)
- `src/settings/settings.c` — Settings panel (549 linhas)
- `Makefile` — Build system (318 linhas)
- `src/boot/boot.asm` — Bootloader (187 linhas)
