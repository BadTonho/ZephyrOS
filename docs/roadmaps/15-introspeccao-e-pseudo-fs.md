# Roadmap 15 - Introspeccao e Pseudo-Filesystems

## Objetivo

Implementar a arquitetura de introspecção do kernel através de pseudo-filesystems
(`/proc` e `/sys`), com leitura dinâmica, snapshots coerentes e lifecycle seguro.
`/proc` expõe processos e métricas operacionais; `/sys` expõe objetos,
dispositivos e relações do kernel. As ferramentas poderão consumir esses
contratos textuais sem depender de layouts binários ou de centenas de syscalls
proprietárias.

## Resumo de progresso

- [ ] PROC0 - Contrato de leitura, snapshot, lifetime e ABI textual.
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

- **Zero Armazenamento em Disco:** Nenhum nó ocupa espaço em disco; seu
  conteúdo é sintetizado ou obtido de um snapshot no momento da leitura.
- **Contratos separados:** `/proc` organiza processos e métricas; `/sys`
  organiza objetos e dispositivos. Um nó não mistura os dois modelos.
- **ABI textual estável:** Interfaces públicas documentam nomes, unidades,
  permissões e formato. `/sys` prefere um atributo por arquivo; relatórios
  longos usam um iterador estilo `seq_file` com offset e EOF.
- **Desacoplamento Kernel-Usuário:** Aplicativos não dependem de ponteiros ou
  layouts binários de structs do kernel.
- **Leveza e lifetime:** Leituras usam buffers temporários limitados e mantêm
  referência ao objeto/snapshot até o fim da operação.
- **Escrita excepcional:** A primeira entrega é somente leitura. Escrita fica
  reservada a controles explicitamente definidos, validados e documentados.

## Ordem de dependência

1. PROC0 - Contratos de leitura e ABI textual.
2. PROC1 - Driver de filesystem virtual `procfs` integrado ao VFS.
3. PROC2 - Nós globais e subdiretórios de processos em `/proc`.
4. PROC3 - Nós de hardware e drivers em `/sys`.
5. PROC4 - Atualização gradual de ferramentas para leitura dos pseudo-arquivos.

---

## PROC0 - Contrato de introspecção

### Implementação

- [ ] Definir ownership, lifetime, permissões, snapshot e cursor por abertura
  para cada entrada dinâmica.
- [ ] Definir formato, unidades, limites, erro, offset, EOF e comportamento
  diante de processo/dispositivo removido durante a leitura.
- [ ] Separar desde o contrato os namespaces `/proc` e `/sys`, evitando uma
  API genérica que esconda diferenças de ciclo de vida.
- [ ] Definir que nós de controle graváveis são opt-in e não fazem parte da
  primeira entrega somente leitura.

### Critério de saída

Uma leitura repetida do mesmo nó produz um snapshot válido e libera todas as
referências e buffers mesmo em erro, cancelamento ou remoção do objeto.

---

## PROC1 - Infraestrutura de Pseudo-Arquivos

### Implementação

- [ ] Criar o driver de filesystem `procfs` que implementa a interface `file_operations_t` do VFS.
- [ ] Implementar a estrutura `proc_entry_t` contendo:
  - `const char* name;`
  - `uint32_t mode;`
  - `int (*read_proc)(char* buffer, uint32_t max_len, void* data);`
  - `int (*write_proc)(const char* buffer, uint32_t len, void* data);` (opcional, somente para nós de controle autorizados)
- [ ] Montar o pseudo-filesystem automaticamente no ponto de montagem `/proc` durante o boot.
- [ ] Implementar leitura com offset, EOF, limite de buffer e referência ao
  entry/snapshot durante toda a operação.

### Critério de saída

O comando `ls /proc` lista os nós virtuais disponíveis e a leitura de nós
simples retorna texto formatado dinamicamente, com EOF e liberação corretos.

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
- [ ] Definir permissões mínimas para informações de outros processos e
  impedir que um PID reutilizado seja confundido com o processo anterior.

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
- [ ] Associar cada diretório a um objeto proprietário e cada atributo a um
  valor textual pequeno, com remoção segura quando o dispositivo desaparecer.
- [ ] Manter os atributos de `/sys` separados dos relatórios compostos de
  `/proc`; controles de energia só serão graváveis após contrato próprio.

### Critério de saída

A árvore `/sys` permite navegar pela topologia de hardware do computador como se fosse uma hierarquia de diretórios.

### Comandos Shell / Diagnóstico

- `ls /sys/bus/pci/devices`: lista todos os dispositivos conectados ao barramento PCI.

---

## PROC4 - Integração de Ferramentas Nativas

### Implementação

- [ ] Atualizar o Task Manager Classic para obter a lista de processos lendo
  `/proc/<pid>/status`, preservando o fallback Simple sem forçar uma nova
  matriz visual.
- [ ] Atualizar o Device Manager para preencher a interface gráfica a partir dos nós em `/sys`.
- [ ] Reservar alteração de parâmetros do kernel via escrita em nós de
  `/proc/sys/` para uma etapa posterior, com permissões, validação e ABI
  próprios; não habilitar escrita genérica em PROC4.

### Critério de saída

As ferramentas gráficas e de terminal funcionam de forma desacoplada das
estruturas internas, consumindo snapshots textuais estáveis e liberando
referências mesmo quando processos ou dispositivos mudam durante a leitura.

### Comandos Shell / Diagnóstico

- `proccheck`: suite de validação de leitura e formatação de todos os nós de `/proc` e `/sys`.
