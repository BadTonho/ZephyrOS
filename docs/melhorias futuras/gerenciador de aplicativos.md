# Gerenciador de Aplicativos — ZephyrOS v0.1

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Framework de Aplicativos | 42 | 0 | 0 | 42 |
| 2. Formato de Pacote e Instalador | 38 | 0 | 0 | 38 |
| 3. Loja de Aplicativos (TUI) | 52 | 0 | 0 | 52 |
| 4. Backend da Loja (Repositório) | 34 | 0 | 0 | 34 |
| 5. Integração com Sistema | 36 | 0 | 0 | 36 |
| 6. Atualizações e Dependências | 28 | 0 | 0 | 28 |
| 7. SDK para Desenvolvedores | 24 | 0 | 0 | 24 |
| **TOTAL** | **254** | **0** | **0** | **254** |

**Progresso geral: 0%** (0/254 itens completos)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F2 | Abrir Loja de Aplicativos |
| Shift+F2 | Gerenciar Aplicativos Instalados |
| F6 | Verificar atualizações |
| Esc | Fechar janela |
| Tab | Alternar entre seções |
| Setas | Navegar na lista |
| Enter | Selecionar/Instalar aplicativo |
| Delete | Desinstalar aplicativo |
| Space | Ver detalhes do aplicativo |
| I | Instalar aplicativo selecionado |
| U | Desinstalar aplicativo selecionado |

---

## Aplicativos Nativos (pré-instalados)

Estes aplicativos fazem parte do sistema e não precisam ser instalados:

### Apps completos

| Aplicativo | Arquivo | Descrição |
|-----------|---------|-----------|
| Shell | `src/shell/shell.c` | Terminal interativo com 22+ comandos |
| Editor | `src/shell/editor.c` | Editor de texto com syntax highlight |
| Media Player | `src/shell/mediaplayer.c` | Reprodutor de áudio WAV e visualizador BMP |
| Task Manager | `src/shell/taskmanager.c` | Monitor de processos, memória e threads |
| File Manager | `src/filemanager/filemanager.c` | Explorador de arquivos com CRUD completo |
| Settings | `src/settings/settings.c` | Painel de configurações com 7 categorias |
| Desktop | `src/desktop/desktop.c` | Ambiente desktop com ícones |
| Window Manager | `src/wm/wm.c` | Gerenciador de janelas com foco e Z-order |
| Taskbar | `src/taskbar/taskbar.c` | Barra de tarefas com Menu Iniciar |
| System Updater | `src/updater/updater.c` | Sistema de atualização do OS |

### Módulos com funções CLI nativas (TUI é opcional)

Estes módulos têm suas funções core compiladas no kernel e acessíveis via comandos do shell. A interface visual (TUI) é um app opcional distribuído pela Loja:

| Módulo | Funções nativas (shell) | App visual (loja) |
|--------|------------------------|-------------------|
| Device Manager | `devices`, `device-info`, `device-scan` | `gerenciador de dispositivos.md` |
| Game Manager | `games`, `games list/play/info` | `gerenciador de jogos.md` |
| Media Manager | `play`, `stop`, `pause`, `volume`, `eq` | `gerenciador de midia.md` |
| Network Manager | `net`, `ifconfig`, `ping`, `wget`, `ftp` | `gerenciador de rede.md` |

---

## Aplicativos Opcionais (instalávels via Loja)

Estes aplicativos são distribuídos através da Loja de Aplicativos. Apenas a interface visual (TUI) — as funções já existem no sistema:

| Aplicativo | Roadmap | O que o app adiciona | Progresso |
|-----------|---------|---------------------|-----------|
| Device Manager | `gerenciador de dispositivos.md` | Visualização em árvore, gerenciamento visual | 14% |
| Game Manager | `gerenciador de jogos.md` | Biblioteca visual, overlay, screenshots | 2% |
| Media Manager | `gerenciador de midia.md` | Player com equalizer, playlists, galeria | 14% |
| Network Manager | `gerenciador de rede.md` | Painel visual de rede, firewall TUI | 0% |
| Anti-Virus | `anti virus.md` | Scanner completo (função + TUI) | 5% |
| PCSista | `pcsista.md` | Monitor visual com overlay e relatórios | 12% |
| Developer Tools | `programadores.md` | IDE, terminal avançado, build system | 0% |

---

## Fase 1: Framework de Aplicativos ⬜

### 1.1 Interface de Aplicativo

- [ ] Criar módulo `src/apps/app.h` e `src/apps/app.c`
- [ ] Definir struct `app_info_t`:
  - [ ] `char name[32]` — nome do aplicativo
  - [ ] `char version[16]` — versão (ex: "1.0.0")
  - [ ] `char description[128]` — descrição curta
  - [ ] `char author[64]` — autor
  - [ ] `uint32_t size` — tamanho em bytes
  - [ ] `uint8_t type` — NATIVO ou OPCIONAL
  - [ ] `uint8_t status` — INSTALADO, DISPONIVEL, ATUALIZAVEL
- [ ] Definir interface de aplicativo:
  - [ ] `int (*init)(void)` — inicialização
  - [ ] `int (*run)(void)` — execução principal
  - [ ] `int (*close)(void)` — finalização
  - [ ] `const char* (*get_name)(void)` — retorna nome
  - [ ] `const char* (*get_version)(void)` — retorna versão
- [ ] Criar struct `app_t` que combina info + interface
- [ ] Criar função `app_create(info, interface)` para criar aplicativo
- [ ] Criar função `app_destroy(app)` para destruir aplicativo

### 1.2 Registrador de Aplicativos

- [ ] Criar módulo `src/apps/registry.h` e `src/apps/registry.c`
- [ ] Criar struct `app_registry_t`:
  - [ ] `app_t* apps[MAX_APPS]` — array de aplicativos (máx 64)
  - [ ] `int count` — número de aplicativos registrados
  - [ ] `char registry_file[64]` — caminho do arquivo de registro
- [ ] Implementar funções:
  - [ ] `registry_init()` — inicializa registrador
  - [ ] `registry_add(app)` — registra aplicativo
  - [ ] `registry_remove(name)` — remove aplicativo
  - [ ] `registry_find(name)` — busca por nome
  - [ ] `registry_get_all()` — retorna todos os aplicativos
  - [ ] `registry_get_installed()` — retorna apenas instalados
  - [ ] `registry_get_available()` — retorna disponíveis para instalação
  - [ ] `registry_save()` — salva registrador em arquivo
  - [ ] `registry_load()` — carrega registrador de arquivo
- [ ] Formato do arquivo de registro (`APPS.REG`):
  - [ ] Header: magic "ZAPP", versão, contagem
  - [ ] Entradas: nome, versão, tamanho, status, caminho

### 1.3 Carregador de Aplicativos

- [ ] Criar módulo `src/apps/loader.h` e `src/apps/loader.c`
- [ ] Implementar carregamento de binário do filesystem:
  - [ ] `loader_load(path)` — carrega binário do arquivo
  - [ ] `loader_unload(app)` — descarrega binário da memória
  - [ ] `loader_execute(app)` — executa aplicativo carregado
- [ ] Gerenciamento de memória para aplicativos:
  - [ ] Alocação de memória para binário (kmalloc)
  - [ ] Cópia do arquivo para memória
  - [ ] Limpeza ao descarregar (kfree)
- [ ] Tratamento de erros:
  - [ ] Arquivo não encontrado
  - [ ] Formato inválido
  - [ ] Memória insuficiente
  - [ ] Erro de execução

### 1.4 API do Kernel para Aplicativos

- [ ] Criar header `src/include/app_api.h`
- [ ] Disponibilizar funções do kernel para aplicativos:
  - [ ] `app_api_print(text, color)` — impressão na tela
  - [ ] `app_api_clear()` — limpar tela
  - [ ] `app_api_input(buffer, size)` — ler input do teclado
  - [ ] `app_api_file_read(path, buffer, size)` — ler arquivo
  - [ ] `app_api_file_write(path, buffer, size)` — escrever arquivo
  - [ ] `app_api_file_list(path)` — listar arquivos
  - [ ] `app_api_mem_alloc(size)` — alocar memória
  - [ ] `app_api_mem_free(ptr)` — liberar memória
  - [ ] `app_api_process_create(name, entry)` — criar processo
  - [ ] `app_api_process_kill(pid)` — matar processo
  - [ ] `app_api_sound_play(freq, dur)` — tocar som
- [ ] Criar thunk layer para chamadas de sistema via interrupção

---

## Fase 2: Formato de Pacote e Instalador ⬜

### 2.1 Formato de Pacote (.zephyrosapp)

- [ ] Definir estrutura do pacote:
  - [ ] Header (64 bytes):
    - [ ] `char magic[4]` — "ZAPP"
    - [ ] `uint32_t version` — versão do formato
    - [ ] `char app_name[32]` — nome do aplicativo
    - [ ] `char app_version[16]` — versão do aplicativo
    - [ ] `uint32_t binary_size` — tamanho do binário
    - [ ] `uint32_t metadata_size` — tamanho dos metadados
    - [ ] `uint32_t checksum` — CRC32 do conteúdo
  - [ ] Binário (tamanho variável):
    - [ ] Código nativo x86 (32-bit)
    - [ ] Compilado com -ffreestanding -nostdlib
  - [ ] Metadados (tamanho variável):
    - [ ] `char description[256]` — descrição detalhada
    - [ ] `char author[64]` — autor
    - [ ] `char license[32]` — licença
    - [ ] `char dependencies[256]` — dependências (separadas por vírgula)
    - [ ] `uint32_t min_os_version` — versão mínima do OS

### 2.2 Empacotador de Aplicativos

- [ ] Criar ferramenta `tools/packager.c` (roda no host)
- [ ] Função `packager_create(app_dir, output_path)`:
  - [ ] Lê binário compilado
  - [ ] Lê metadados de `app.json`
  - [ ] Calcula CRC32
  - [ ] Escreve pacote .zephyrosapp
- [ ] Formato do `app.json`:
  ```json
  {
    "name": "Anti-Virus",
    "version": "1.0.0",
    "description": "Scanner de vírus para ZephyrOS",
    "author": "ZephyrOS Team",
    "license": "MIT",
    "dependencies": [],
    "min_os_version": "0.2.0"
  }
  ```

### 2.3 Instalador de Aplicativos

- [ ] Criar módulo `src/apps/installer.h` e `src/apps/installer.c`
- [ ] Implementar instalação:
  - [ ] `installer_install(package_path)` — instala pacote
  - [ ] Validação do pacote (magic, checksum, versão)
  - [ ] Cópia do binário para `APPS/<nome>/`
  - [ ] Extração de metadados
  - [ ] Atualização do registrador
  - [ ] Criação de atalho no desktop (opcional)
- [ ] Implementar desinstalação:
  - [ ] `installer_uninstall(app_name)` — desinstala aplicativo
  - [ ] Remoção do binário e metadados
  - [ ] Atualização do registrador
  - [ ] Remoção de atalhos
- [ ] Implementar verificação:
  - [ ] `installer_verify(package_path)` — verifica integridade
  - [ ] `installer_check_deps(app_name)` — verifica dependências
  - [ ] `installer_resolve_deps(app_name)` — resolve dependências

### 2.4 Sistema de Backup

- [ ] Backup antes de instalar:
  - [ ] `installer_backup(app_name)` — cria backup do estado atual
  - [ ] Salva binário anterior em `APPS/<nome>/backup/`
  - [ ] Salva configurações em `APPS/<nome>/config/`
- [ ] Restauração:
  - [ ] `installer_restore(app_name)` — restaura backup
  - [ ] Rollback em caso de falha na instalação

---

## Fase 3: Loja de Aplicativos (TUI) ⬜

### 3.1 Interface Principal da Loja

- [ ] Criar módulo `src/apps/store.h` e `src/apps/store.c`
- [ ] Layout da tela:
  ```
  ┌─────────────────────────────────────────────────────────────┐
  │                  ZephyrOS App Store                         │
  ├──────────┬──────────────────────────────────────────────────┤
  │ Categorias│  ┌────────────────────────────────────────────┐ │
  │          │  │ Anti-Virus v1.0.0                          │ │
  │ [Todos]  │  │ Scanner de vírus com MD5 e assinaturas    │ │
  │ [Sistema]│  │ Tamanho: 45KB  Autor: ZephyrOS Team       │ │
  │ [Jogos]  │  │ Status: Disponível  [I]nstalar             │ │
  │ [Mídia]  │  ├────────────────────────────────────────────┤ │
  │ [Rede]   │  │ Game Manager v1.0.0                        │ │
  │ [Ferram] │  │ Biblioteca de jogos estilo Steam           │ │
  │          │  │ Tamanho: 62KB  Autor: ZephyrOS Team       │ │
  │          │  │ Status: Disponível  [I]nstalar             │ │
  │          │  └────────────────────────────────────────────┘ │
  ├──────────┴──────────────────────────────────────────────────┤
  │ F2=Loja Shift+F2=Instalados F6=Atualizar Esc=Sair        │
  └─────────────────────────────────────────────────────────────┘
  ```

- [ ] Implementar painel de categorias:
  - [ ] Lista lateral com categorias
  - [ ] Filtro por categoria
  - [ ] Contador de aplicativos por categoria
- [ ] Implementar lista de aplicativos:
  - [ ] Nome, versão, descrição resumida
  - [ ] Tamanho, autor, status
  - [ ] Ícone do aplicativo (opcional)
- [ ] Implementar detalhes do aplicativo:
  - [ ] Descrição completa
  - [ ] Histórico de versões (changelog)
  - [ ] Capturas de tela (opcional)
  - [ ] Avaliações (opcional)

### 3.2 Tela de Aplicativos Instalados

- [ ] Criar视图 "Meus Aplicativos":
  - [ ] Lista de aplicativos instalados
  - [ ] Versão instalada vs disponível
  - [ ] Botão de atualização (se disponível)
  - [ ] Botão de desinstalação
- [ ] Implementar gerenciamento:
  - [ ] Ativar/desativar aplicativo
  - [ ] Verificar atualizações
  - [ ] Limpar cache/dados

### 3.3 Sistema de Busca

- [ ] Implementar busca por nome:
  - [ ] Input de texto para busca
  - [ ] Busca em tempo real (filtrar lista)
  - [ ] Busca por descrição
- [ ] Implementar filtros:
  - [ ] Por categoria
  - [ ] Por status (disponível/instalado/atualizável)
  - [ ] Por tamanho
  - [ ] Por autor

### 3.4 Sistema de Notificações

- [ ] Notificação de atualização disponível
- [ ] Notificação de instalação concluída
- [ ] Notificação de erro
- [ ] Barra de progresso durante instalação

---

## Fase 4: Backend da Loja (Repositório) ⬜

### 4.1 Repositório Local

- [ ] Criar estrutura de diretórios:
  ```
  APPS/
  ├── REGISTRY/          → Registros de aplicativos
  │   └── APPS.REG       → Arquivo de registro
  ├── INSTALLED/         → Aplicativos instalados
  │   ├── anti-virus/
  │   │   ├── app.bin    → Binário do aplicativo
  │   │   ├── app.json   → Metadados
  │   │   └── config/    → Configurações
  │   └── game-manager/
  ├── REPOSITORY/        → Repositório de pacotes
  │   ├── anti-virus.zephyrosapp
  │   ├── game-manager.zephyrosapp
  │   └── ...
  └── BACKUP/            → Backups de atualização
      └── <app-name>/
  ```

### 4.2 Gerenciamento do Repositório

- [ ] Implementar funções:
  - [ ] `repo_scan()` — escaneia repositório e atualiza registry
  - [ ] `repo_add_package(path)` — adiciona pacote ao repositório
  - [ ] `repo_remove_package(name)` — remove pacote do repositório
  - [ ] `repo_list_packages()` — lista pacotes disponíveis
  - [ ] `repo_get_package_info(name)` — retorna info do pacote
- [ ] Verificação de integridade:
  - [ ] CRC32 de cada pacote
  - [ ] Validação de formato
  - [ ] Detecção de corrupção

### 4.3 Sistema de Cache

- [ ] Cache de metadados:
  - [ ] Armazena informações de pacotes em memória
  - [ ] Atualiza quando repositório muda
  - [ ] Inválida cache quando necessário
- [ ] Cache de busca:
  - [ ] Armazena resultados de busca recentes
  - [ ] Atualiza em tempo real durante digitação

### 4.4 Estatísticas e Logs

- [ ] Log de instalações:
  - [ ] Data/hora da instalação
  - [ ] Versão instalada
  - [ ] Sucesso/erro
- [ ] Estatísticas:
  - [ ] Aplicativos mais instalados
  - [ ] Espaço usado
  - [ ] Última atualização

---

## Fase 5: Integração com Sistema ⬜

### 5.1 Integração com Taskbar

- [ ] Adicionar botão "App Store" no Menu Iniciar
- [ ] Ícone na taskbar quando loja está aberta
- [ ] Notificação de atualização disponível
- [ ] Contador de aplicativos instalados

### 5.2 Integração com Desktop

- [ ] Ícone "App Store" no desktop
- [ ] Atalho para loja (duplo clique)
- [ ] Ícones de aplicativos instalados no desktop (opcional)

### 5.3 Integração com Settings

- [ ] Categoria "Aplicativos" nas configurações
- [ ] Lista de aplicativos instalados
- [ ] Opções de gerenciamento (desinstalar, desativar)
- [ ] Preferências da loja (auto-atualizar, notificações)

### 5.4 Integração com Shell

- [ ] Comando `appstore` — abre a loja
- [ ] Comando `apps list` — lista aplicativos instalados
- [ ] Comando `apps install <nome>` — instala aplicativo
- [ ] Comando `apps uninstall <nome>` — desinstala aplicativo
- [ ] Comando `apps update` — verifica atualizações
- [ ] Comando `apps info <nome>` — mostra info do aplicativo

### 5.5 Integração com Window Manager

- [ ] Janela redimensionável para loja
- [ ] Suporte a múltiplas janelas (loja + detalhes)
- [ ] Título dinâmico com nome do aplicativo selecionado

---

## Fase 6: Atualizações e Dependências ⬜

### 6.1 Sistema de Atualização

- [ ] Verificação automática de atualizações:
  - [ ] Ao abrir a loja
  - [ ] Periódicamente (a cada 24h)
  - [ ] Manualmente (F6)
- [ ] Download de atualizações:
  - [ ] Comparação de versões
  - [ ] Download do pacote atualizado
  - [ ] Backup da versão anterior
  - [ ] Instalação da nova versão
- [ ] Rollback:
  - [ ] `apps rollback <nome>` — volta versão anterior
  - [ ] Lista de versões disponíveis

### 6.2 Sistema de Dependências

- [ ] Resolução de dependências:
  - [ ] Análise de dependências do pacote
  - [ ] Verificação se dependências estão instaladas
  - [ ] Instalação automática de dependências
- [ ] Conflitos:
  - [ ] Detecção de conflitos entre aplicativos
  - [ ] Aviso ao usuário
  - [ ] Resolução manual

### 6.3 Integração com System Updater

- [ ] Pacotes do sistema via `.zephyros` (atualizações.md)
- [ ] Pacotes de apps via `.zephyrosapp` (este roadmap)
- [ ] Separar atualizações do OS de atualizações de apps

---

## Fase 7: SDK para Desenvolvedores ⬜

### 7.1 Template de Aplicativo

- [ ] Criar template em `templates/app_template/`:
  - [ ] `main.c` — ponto de entrada
  - [ ] `app.json` — metadados
  - [ ] `Makefile` — compilação
  - [ ] `README.md` — instruções
- [ ] Exemplo de aplicativo:
  - [ ] `hello_world` — aplicativo mínimo
  - [ ] `calculator` — calculadora simples
  - [ ] `notepad` — bloco de notas

### 7.2 Ferramentas de Desenvolvimento

- [ ] Compilador cross-compiler para apps:
  - [ ] Flags específicas para apps
  - [ ] Linker script para apps
  - [ ] Bibliotecas padrão para apps
- [ ] Empacotador (`tools/packager`):
  - [ ] Cria pacote .zephyrosapp
  - [ ] Valida pacote
  - [ ] Testa pacote
- [ ] Depurador:
  - [ ] `app-debug <nome>` — depura aplicativo
  - [ ] Logs de execução

### 7.3 Documentação

- [ ] Guia de desenvolvimento:
  - [ ] Como criar um aplicativo
  - [ ] API disponível
  - [ ] Boas práticas
- [ ] Referência da API:
  - [ ] Funções do kernel disponíveis
  - [ ] Estruturas de dados
  - [ ] Exemplos de código

---

## Limitações Técnicas

1. **Sem carregamento dinâmico** — O ZephyrOS não suporta ELF loading ou carregamento dinâmico de módulos. Aplicativos precisam ser compilados estaticamente ou usar um mecanismo simples de loading (copiar binário para endereço fixo e pular para ele).

2. **Sem processos isolados** — Aplicativos rodam no mesmo espaço de memória do kernel. Um bug em um aplicativo pode derrubar todo o sistema.

3. **Sem rede** — O ZephyrOS não possui stack de rede. A loja de aplicativos funciona apenas com repositório local (pacotes já no disco). Para download automático, seria necessário implementar driver de NIC + TCP/IP.

4. **Memória limitada** — Aplicativos devem ser pequenos (máx 64KB recomendado) devido à memória limitada do sistema.

5. **Single-tasking por aplicativo** — Cada aplicativo roda em modo blocking. Apenas um aplicativo pode estar ativo por vez (exceto WM que gerencia janelas).

6. **Formato proprietário** — O formato `.zephyrosapp` é proprietário. Não é compatível com outros sistemas ou formatos existentes (PE, ELF, etc.).

7. **Sem multi-usuário** — Não há conceito de permissões ou usuários. Qualquer aplicativo pode acessar qualquer recurso.

---

## Referências

- [OSDev Wiki - Userspace](https://wiki.osdev.org/Userspace) — Conceitos de processos de usuário
- [OSDev Wiki - ELF](https://wiki.osdev.org/ELF) — Formato ELF (para referência futura)
- [AppImage](https://appimage.org/) — Formato de pacote Linux (inspiração)
- [Flatpak](https://flatpak.org/) — Sistema de pacotes Linux (inspiração)
- [Windows Installer](https://learn.microsoft.com/en-us/windows/win32/msi/) — Instalador do Windows (inspiração)
- [Chocolatey](https://chocolatey.org/) — Gerenciador de pacotes Windows (inspiração)

---

## Notas de Implementação

1. **Prioridade** — O framework de aplicativos (Fase 1) é a base para tudo. Comece por ele.

2. **Simplicidade** — Dada a limitação de não ter carregamento dinâmico, a abordagem mais prática é:
   - Compilar todos os apps opcionais durante o build
   - Colocar seus binários no repositório
   - O instalador copia o binário para a pasta de apps instalados
   - Um registro controla quais apps estão "ativados"

3. **Compatibilidade** — O formato `.zephyrosapp` deve ser backwards-compatible. Versões antigas do instalador devem conseguir ler pacotes novos (ignorando campos desconhecidos).

4. **Integração** — O app store deve se integrar naturalmente com a taskbar (botão no Menu Iniciar), desktop (ícone) e shell (comando `appstore`).

5. **Ciclo de vida** — O fluxo completo de um aplicativo é:
   - Desenvolvedor cria app usando SDK
   - Empacota como `.zephyrosapp`
   - Adiciona ao repositório
   - Usuário instala via loja
   - App aparece no desktop/taskbar/shell
   - Usuário pode desinstalar a qualquer momento

6. **Diferença para outros roadmaps** — Os aplicativos listados nos outros arquivos (Anti-Virus, Game Manager, etc.) são distribuídos como pacotes `.zephyrosapp` através desta loja. Eles não são compilados no kernel padrão.
