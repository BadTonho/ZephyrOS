# Roadmap 15 - Introspeccao e Pseudo-Filesystems

## Objetivo

Implementar a arquitetura de introspecção do kernel inspirada nos pseudo-filesystems do Linux (`/proc` e `/sys`), permitindo que métricas de CPU, memória, processos, drivers e barramentos de hardware sejam expostos como nós de arquivos de texto virtuais gerados dinamicamente em memória, facilitando o consumo por aplicativos (como o Task Manager e Device Manager) e comandos do Shell sem a necessidade de centenas de syscalls proprietárias.

## Resumo de progresso

- [ ] PROC1 - Infraestrutura de geração dinâmica de pseudo-arquivos em RAM.
- [ ] PROC2 - Mapeamento de `/proc` para métricas globais e processos por PID.
- [ ] PROC3 - Mapeamento de `/sys` para árvore de dispositivos, barramentos e drivers.
- [ ] PROC4 - Migração do Task Manager, Device Manager e utilitários Shell para `/proc`.

## Atalhos

- [Roadmap 03 - Kernel e Desempenho](03-kernel-e-desempenho.md)
- [Roadmap 10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md)
- [Roadmap 11 - Gerenciamento Avancado de Memoria](11-gerenciamento-avancado-de-memoria.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Diagnóstico `memcheck`, `procs`, `threads`, `uptime` e `health`.
- Inventário PCI e descoberta de hardware ACPI.
- Tabela de métricas `kmetrics`.
- Integração de janelas gráficas no Task Manager e Device Manager.

## Princípios de engenharia

- **Zero Armazenamento em Disco:** Nenhum arquivo de `/proc` ou `/sys` ocupa espaço em disco; seu conteúdo é sintetizado por funções de callback apenas no momento em que o arquivo é lido (`read`).
- **Formato Texto Simples:** Todas as informações são expressas em pares chave-valor de texto legível (ex: `MemTotal: 65536 kB\nMemFree: 45120 kB\n`), permitindo visualização com ferramentas padrão como `cat` ou `grep`.
- **Desacoplamento Kernel-Usuário:** Aplicativos de monitoramento não dependem de ponteiros ou layouts binários de structs do kernel, aumentando a estabilidade da ABI.
- **Leveza de Memória:** Leituras utilizam buffers temporários pequenos com paginação simples.

## Ordem de dependência

1. PROC1 - Driver de filesystem virtual `procfs` integrado ao VFS.
2. PROC2 - Nós globais e subdiretórios de processos em `/proc`.
3. PROC3 - Nós de hardware e drivers em `/sys`.
4. PROC4 - Atualização de ferramentas do usuário para leitura dos pseudo-arquivos.

---

## PROC1 - Infraestrutura de Pseudo-Arquivos

### Implementação

- [ ] Criar o driver de filesystem `procfs` que implementa a interface `file_operations_t` do VFS.
- [ ] Implementar a estrutura `proc_entry_t` contendo:
  - `const char* name;`
  - `uint32_t mode;`
  - `int (*read_proc)(char* buffer, uint32_t max_len, void* data);`
  - `int (*write_proc)(const char* buffer, uint32_t len, void* data);`
- [ ] Montar o pseudo-filesystem automaticamente no ponto de montagem `/proc` durante o boot.

### Critério de saída

O comando `ls /proc` lista os nós virtuais disponíveis e a leitura de nós simples retorna texto formatado dinamicamente.

### Comandos Shell / Diagnóstico

- `mount | grep proc`: confirma a montagem correta do `procfs`.

---

## PROC2 - Mapeamento de /proc para Sistema e Processos

### Implementação

- [ ] Implementar os nós de sistema em `/proc`:
  - `/proc/meminfo`: memória total, livre, usada, SLAB e buffers de disco.
  - `/proc/cpuinfo`: identificador do processador, flags e frequência estimada.
  - `/proc/uptime`: segundos de atividade e tempo gasto em modo idle.
  - `/proc/version`: versão do ZephyrOS, data de compilação e compilador utilizado.
  - `/proc/cmdline`: argumentos passados pelo bootloader ao kernel.
- [ ] Implementar diretórios dinâmicos para cada processo ativo (`/proc/<pid>/`):
  - `/proc/<pid>/status`: nome, estado, PPID, threads e uso de memória.
  - `/proc/<pid>/cmdline`: comando e argumentos que iniciaram o processo.
  - `/proc/<pid>/maps`: lista de VMAs e regiões de memória mapeadas.

### Critério de saída

Comandos como `cat /proc/meminfo` e `cat /proc/1/status` exibem dados precisos e atualizados em tempo real.

### Comandos Shell / Diagnóstico

- `cat /proc/meminfo`: inspeciona o estado global de memória.
- `cat /proc/<pid>/status`: inspeciona os atributos de um processo específico.

---

## PROC3 - Mapeamento de /sys para Hardware

### Implementação

- [ ] Criar o ponto de montagem `/sys` com o driver `sysfs`.
- [ ] Mapear a árvore de barramentos e dispositivos:
  - `/sys/bus/pci/devices/`: dispositivos PCI descobertos com Vendor ID, Device ID e classe.
  - `/sys/class/net/`: interfaces de rede registradas (`eth0`, `eth1`) e seus endereços MAC.
  - `/sys/class/block/`: discos e mídias de armazenamento (`hda`, `sda`).
  - `/sys/power/state`: controle e leitura dos estados de energia disponíveis.

### Critério de saída

A árvore `/sys` permite navegar pela topologia de hardware do computador como se fosse uma hierarquia de diretórios.

### Comandos Shell / Diagnóstico

- `ls /sys/bus/pci/devices`: lista todos os dispositivos conectados ao barramento PCI.

---

## PROC4 - Integração de Ferramentas Nativas

### Implementação

- [ ] Atualizar o Task Manager Classic/Modern para obter a lista de processos lendo `/proc/<pid>/status`.
- [ ] Atualizar o Device Manager para preencher a interface gráfica a partir dos nós em `/sys`.
- [ ] Permitir alteração de parâmetros do kernel em tempo de execução via escrita em nós de `/proc/sys/` (ex: `echo 1 > /proc/sys/net/ipv4/ip_forward`).

### Critério de saída

As ferramentas gráficas e de terminal funcionam de forma totalmente desacoplada das estruturas internas de dados do kernel.

### Comandos Shell / Diagnóstico

- `proccheck`: suite de validação de leitura e formatação de todos os nós de `/proc` e `/sys`.
