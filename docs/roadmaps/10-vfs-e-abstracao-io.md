# Roadmap 10 - VFS e Abstracao de I/O

## Objetivo

Implementar uma camada unificada de Sistema de Arquivos Virtual (VFS - *Virtual File System*) no ZephyrOS, baseada no conceito arquitetural de abstração universal de I/O (*"everything is a file"*), permitindo que arquivos em disco, dispositivos de hardware, pipes de comunicação e pseudo-arquivos sejam manipulados de forma homogênea através de descritores de arquivos (`fd`) e uma tabela de operações unificada (`file_operations_t`).

Este roadmap estabelece uma arquitetura própria, modular, segura e limpa para o ZephyrOS em 32-bit x86.

## Resumo de progresso

- [x] VFS1 - Descritores e operacoes unificadas de I/O (concluida e validada).
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

- [x] Definir a estrutura `file_operations_t` contendo ponteiros:
  `int (*open)(vnode_t*, file_t*);`
  `int (*read)(file_t*, void*, uint32_t, uint32_t*);`
  `int (*write)(file_t*, const void*, uint32_t, uint32_t*);`
  `int (*close)(file_t*);`
  `int (*lseek)(file_t*, int32_t, uint32_t, uint32_t*);`
  `int (*ioctl)(file_t*, uint32_t, void*);`
- [x] Criar as estruturas centrais `vnode_t` e `file_t` para arquivos e streams padrao.
- [x] Implementar 32 descritores por processo e pool global de 32 arquivos regulares.
- [x] Preservar as syscalls 4-7 e acrescentar `file_lseek` como syscall 14.
- [x] Mapear `stdin` (fd 0) para scancodes via IPC e `stdout`/`stderr` (fd 1 e 2) para o console ativo.
- [x] Integrar criacao, descarte, encerramento e destruicao de processos.
- [x] Publicar a App API 0.5 e manter pacotes 0.3 e 0.4 compativeis.
- [x] Integrar `vfs status`, `vfs test`, `appcheck`, `regcheck full` e `health check`.

### Estado da entrega

A implementacao e a matriz executavel foram concluidas no QEMU padrao e no
perfil USB HID. VFS2, montagens, escrita parcial, `/dev`, pipes e
pseudo-filesystems nao fazem parte desta entrega.

Durante a matriz, Enter e Ctrl+C em `app inputtest` foram aprovados. O primeiro
teste com F12 revelou liberacao prematura do fd 0 bloqueado; a correcao passou
a concluir o cancelamento somente depois do retorno da espera. O novo teste
com F12 foi aprovado, sem descritores residuais. Os gates da versao corrigida
foram confirmados. Uma repeticao de Ctrl+C revelou contabilizacao espuria de
falha VFS; a leitura stdin foi ajustada para concluir a interrupcao por sinal
com zero bytes. O novo teste foi aprovado sem log de erro, sem falhas VFS e
sem descritores residuais. Enter, Ctrl+C e F12 estao aprovados.

### Critério de saída

Processos em ring 3 conseguem abrir, ler, escrever e fechar descritores de arquivo padronizados sem invocar diretamente funções do FAT ou do driver de vídeo.

### Comandos Shell / Diagnóstico

- `vfs status`: exibe descritores abertos no processo atual e arquivos globais em uso.
- `vfs test`: executa autoteste de stdio, ciclo de arquivo, permissoes, EOF,
  limites, isolamento, limpeza e invariantes.
- `vfs test foo`: valida a rejeicao de argumentos excedentes.

### Validacao concluida

Os gates de host, os diagnosticos VFS/App API e os tres fluxos de
`app inputtest` ja foram aprovados para esta versao. `usertest fault`,
`regcheck full`, `health check`, `memcheck` e `log check` tambem foram
aprovados no QEMU padrao. O usuario nao valida o fallback Simple; como a
entrega permanece no modo Classic e nao alterou a implementacao visual do
fallback, esse modo nao integra o criterio de saida. O `vfs status` final
confirmou ausencia de residuos e concluiu a matriz do QEMU padrao.

No perfil USB HID, teclado e mouse `READY`, `app inputtest`, Ctrl+C, retorno do
foco, limpeza VFS, `regcheck full` e `health check` foram aprovados depois da
correcao da invariante transitiva do QH Interrupt. VFS1 foi concluida sem
divida tecnica.

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
