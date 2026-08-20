# Roadmap 10 - VFS e Abstracao de I/O

## Objetivo

Implementar uma camada unificada de Sistema de Arquivos Virtual (VFS - *Virtual File System*) no ZephyrOS, baseada no conceito arquitetural de abstração universal de I/O (*"everything is a file"*), permitindo que arquivos em disco, dispositivos de hardware, pipes de comunicação e pseudo-arquivos sejam manipulados de forma homogênea através de descritores de arquivos (`fd`) e uma tabela de operações unificada (`file_operations_t`).

Este roadmap estabelece uma arquitetura própria, modular, segura e limpa para o ZephyrOS em 32-bit x86.

## Resumo de progresso

- [ ] VFS1 - Tabela de descritores de arquivos (`file_desc_t` / `fd`) e contratos de I/O.
- [ ] VFS2 - Tabela de montagem de volumes e resolução uniforme de caminhos.
- [ ] VFS3 - Abstração de nós de dispositivos (`/dev/`) de caractere e bloco.
- [ ] VFS4 - Pipes anônimos e redirecionamento de streams no Shell.

## Atalhos

- [Roadmap 08 - Evolucao da Plataforma](08-evolucao-da-plataforma.md)
- [Roadmap 09 - Funcionalidades aplicáveis](09-funcionalidades-aplicaveis.md)
- [Roadmap 13 - Armazenamento e Buffer Cache](13-armazenamento-e-buffer-cache.md)
- [Roadmap 15 - Introspeccao e Pseudo-Filesystems](15-introspeccao-e-pseudo-fs.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Drivers ATA PIO e leitura/escrita em setores de disco.
- Implementações nativas de FAT12 e FAT32 com listagem e leitura/escrita de arquivos.
- Índice de arquivos em RAM com pesquisa rápida no Explorer.
- Suporte a processos ring 3 e tabela básica de syscalls.

## Princípios de engenharia

- **Abstração Uniforme:** Processos e comandos do Shell operam via `open()`, `read()`, `write()`, `close()`, `lseek()` e `ioctl()`, sem conhecer detalhes de drivers de hardware ou formatos de filesystem.
- **Tabela de Operações (`file_operations_t`):** Cada nó (arquivo, diretório, driver de caractere, pipe) registra uma estrutura de ponteiros de funções para leitura, escrita e controle.
- **Isolamento por Processo:** Cada processo possui sua própria tabela de descritores (`fd_table`), mapeando inteiros pequenos (`0 = stdin`, `1 = stdout`, `2 = stderr`, `3...`) para estruturas abertas do kernel (`file_t`).
- **Resiliência e Fallbacks:** Falha na montagem de um volume ou erro em dispositivo não afeta o console de emergência Simple nem derruba o sistema.

## Ordem de dependência

1. VFS1 - Descritores de arquivos, estrutura `vnode_t`/`inode_t` e chamadas de I/O.
2. VFS2 - Tabela de montagem (`mount_table`) conectando FAT12 e FAT32 ao VFS.
3. VFS3 - Nós de dispositivos `/dev/` para terminais, nulo e áudio.
4. VFS4 - Pipes anônimos e redirecionamento para o Shell.

---

## VFS1 - Tabela de descritores e operações de I/O

### Implementação

- [ ] Definir a estrutura `file_operations_t` contendo ponteiros:
  `int (*open)(vnode_t*, file_t*);`
  `int (*read)(file_t*, void*, uint32_t);`
  `int (*write)(file_t*, const void*, uint32_t);`
  `int (*close)(file_t*);`
  `int (*lseek)(file_t*, int32_t, int);`
  `int (*ioctl)(file_t*, uint32_t, void*);`
- [ ] Criar a estrutura central `vnode_t` (nó virtual em memória) representando arquivos, diretórios e dispositivos.
- [ ] Implementar a tabela de arquivos abertos do processo (`fd_table`) com capacidade inicial de 16 a 32 descritores por processo.
- [ ] Implementar as syscalls unificadas: `sys_open`, `sys_read`, `sys_write`, `sys_close`, `sys_lseek`.
- [ ] Mapear `stdin` (fd 0) para o buffer de teclado e `stdout`/`stderr` (fd 1 e 2) para a saída de vídeo ativa (Simple/Classic).

### Critério de saída

Processos em ring 3 conseguem abrir, ler, escrever e fechar descritores de arquivo padronizados sem invocar diretamente funções do FAT ou do driver de vídeo.

### Comandos Shell / Diagnóstico

- `vfs status`: exibe descritores abertos no processo atual e arquivos globais em uso.
- `vfs test`: executa suite de testes de abertura, escrita, leitura e fechamento de descritores.

---

## VFS2 - Tabela de montagem e caminhos universais

### Implementação

- [ ] Criar a tabela de montagem global (`mount_table_t`) com suporte a múltiplos pontos de montagem (`/`, `/mnt/fat12`, `/mnt/fat32`, etc.).
- [ ] Implementar o algoritmo de resolução de caminhos absolutos e relativos (`vfs_lookup`), resolvendo diretórios `.` e `..` de forma determinística.
- [ ] Integrar os drivers existentes de FAT12 e FAT32 como provedores do contrato `file_operations_t`.
- [ ] Adicionar suporte a diretório de trabalho atual (`cwd`) por processo.

### Critério de saída

O sistema resolve caminhos completos no formato `/mnt/c/arquivo.txt` de forma transparente, direcionando as requisições para o driver de filesystem correspondente.

### Comandos Shell / Diagnóstico

- `mount`: lista todos os volumes e pontos de montagem ativos com suas capacidades e tipo de sistema de arquivos.
- `pwd`: exibe o diretório de trabalho atual do processo.
- `cd <caminho>`: navega pela árvore de diretórios unificada.

---

## VFS3 - Nós de dispositivos (`/dev/`)

### Implementação

- [ ] Criar o provedor de pseudo-filesystem `devfs` montado em `/dev`.
- [ ] Registrar dispositivos padrão de caractere:
  - `/dev/null`: descarta escritas e retorna EOF na leitura.
  - `/dev/zero`: retorna bytes zero continuamente na leitura.
  - `/dev/tty`: entrada e saída direta do console ativo.
  - `/dev/speaker`: controle do PC Speaker via `write` ou `ioctl`.
- [ ] Registrar nós de blocos brutos para diagnóstico:
  - `/dev/hda`: acesso direto aos setores do disco primário ATA.

### Critério de saída

Comandos como `cat /dev/zero` ou redirecionamento para `/dev/null` funcionam através das mesmas chamadas de leitura e escrita do VFS.

### Comandos Shell / Diagnóstico

- `ls /dev`: lista todos os dispositivos de caractere e bloco registrados.
- `devcheck`: valida as operações de leitura/escrita nos nós padrão de `/dev`.

---

## VFS4 - Pipes anônimos e redirecionamento

### Implementação

- [ ] Implementar a estrutura de buffer circular `pipe_t` com semântica de leitor/escritor.
- [ ] Implementar a syscall `sys_pipe(int fds[2])`, retornando um descritor de leitura (`fds[0]`) e um de escrita (`fds[1]`).
- [ ] Implementar bloqueio suave da leitura quando o pipe estiver vazio e bloqueio de escrita quando estiver cheio (integrando com as Wait Queues do Roadmap 12).
- [ ] Adaptar o parser do Shell para suportar pipes (`|`) e redirecionamento de saída (`>` e `>>`).

### Critério de saída

Comandos encadeados no Shell como `procs | grep shell` ou `ls > lista.txt` executam com sucesso utilizando pipes e descritores redirecionados.

### Comandos Shell / Diagnóstico

- `pipetest`: validação automatizada de comunicação unidirecional entre duas threads via pipe.
