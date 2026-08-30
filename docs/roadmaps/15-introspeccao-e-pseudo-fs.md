# Roadmap 15 - Introspeccao e Pseudo-Filesystems

## Objetivo

Implementar a arquitetura de introspecção do kernel através de pseudo-filesystems
(`/proc` e `/sys`), com leitura dinâmica, snapshots coerentes e lifecycle seguro.
`/proc` expõe processos e métricas operacionais; `/sys` expõe objetos,
dispositivos e relações do kernel. As ferramentas poderão consumir esses
contratos textuais sem depender de layouts binários ou de centenas de syscalls
proprietárias.

## Resumo de progresso

- [x] PROC0 - Contrato de leitura, snapshot, lifetime e ABI textual.
- [x] PROC1 - Infraestrutura de geração dinâmica de pseudo-arquivos em RAM.
- [x] PROC2 - Mapeamento de `/proc` para métricas globais e processos por PID.
- [x] PROC3 - Mapeamento de `/sys` para árvore de dispositivos, barramentos e drivers.
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

- [x] Definir ownership, lifetime, permissões, snapshot e cursor por abertura
  para cada entrada dinâmica.
- [x] Definir formato, unidades, limites, erro, offset, EOF e comportamento
  diante de processo/dispositivo removido durante a leitura.
- [x] Separar desde o contrato os namespaces `/proc` e `/sys`, evitando uma
  API genérica que esconda diferenças de ciclo de vida.
- [x] Definir que nós de controle graváveis são opt-in e não fazem parte da
  primeira entrega somente leitura.

### Contrato congelado

PROC0 é documental. Nenhum código, header, syscall, App API ou alteração de
Makefile é necessário nesta etapa. O PROC1 deverá implementar o contrato
usando a VFS existente, sem expor `vnode_t`, `file_t`, ponteiros de objetos ou
layouts binários aos consumidores.

Os namespaces têm responsabilidades independentes:

- `/proc` publica relatórios agregados do sistema e diretórios dinâmicos por
  processo. Os nós planejados são `meminfo`, `cpuinfo`, `uptime`, `version`,
  `cmdline`, `<pid>/status`, `<pid>/cmdline` e `<pid>/maps`.
- `/sys` publica a topologia de objetos e atributos pequenos, incluindo
  `bus/pci/devices`, `class/net`, `class/block` e `power/state`. Interfaces de
  permissões por usuário, UID/GID e controles de energia ficam fora do PROC0;
  `power/state` será somente leitura nesta etapa.

O conteúdo dos nós é uma sequência de linhas ASCII no formato
`<chave> <valor>\n`. As chaves usam minúsculas, dígitos, `_`, `-`, `/` e `.`;
uma linha possui exatamente um separador ASCII entre a chave e o valor. Os
valores não contêm `NUL`, `LF`, `CR`, sequências ANSI ou texto dependente de
locale; a serialização usa `LF` e nunca `CRLF`. Contadores usam decimal,
máscaras e identificadores de hardware usam hexadecimal, IPv4 usa notação
pontuada e estados usam tokens estáveis. Chaves repetidas representam
registros múltiplos e mantêm a ordem documentada pelo nó. Os formatos
específicos de cada nó serão detalhados em PROC2 e PROC3, sem alterar essa
gramática comum.

Cada abertura de nó captura um snapshot imutável com no máximo
`PROCFS_MAX_SNAPSHOT_SIZE`, definido contratualmente como 16 KiB. O snapshot
pertence ao `file_t` até `close()` e o `file_t.offset` é o cursor da leitura.
`read()` pode retornar blocos parciais, retorna `OK` com zero bytes no EOF e
`lseek()` aceita `SET`, `CUR` e `END` somente dentro de `[0, tamanho]`. Se a
serialização exceder 16 KiB, a abertura falha com `ERR_OVERFLOW`; não há
truncamento nem mistura de gerações. A listagem de diretório é um snapshot da
chamada `vfs_list_dir()` e não mantém ponteiros depois do retorno.

O snapshot é construído por cópia e não conserva referência direta a processo,
dispositivo, montagem ou tabela interna. A identidade capturada combina
`pid` com `process_event_generation` para processos e identificador estável
com geração para dispositivos. Se o objeto desaparecer depois da abertura,
o descritor continua válido até o fim do snapshot; uma nova abertura retorna
`ERR_NOT_FOUND`, evitando confundir um PID reutilizado com o processo anterior.

A enumeração é determinística: os nós fixos de `/proc` seguem a ordem do
contrato, os PIDs são crescentes, os nós de cada processo seguem ordem fixa e
`/sys` usa a hierarquia documentada com identificadores estáveis. Todos os
nós iniciais são públicos e somente leitura; abertura com escrita e operação
não suportada retornam `ERR_UNAVAILABLE`. Os demais erros estáveis são
`ERR_INVALID` para caminho ou cursor inválido, `ERR_NOT_FOUND` para nó ausente,
`ERR_OVERFLOW` para snapshot acima do limite e `ERR_MEM` para falta de memória.

### Critério de saída

Uma leitura repetida do mesmo nó produz um snapshot válido e libera todas as
referências e buffers mesmo em erro, cancelamento ou remoção do objeto.

---

## PROC1 - Infraestrutura de Pseudo-Arquivos

### Implementação

- [x] Criar `procfs.c` e `procfs.h`, com `proc_entry_t`, callback de leitura
  com `out_len`, callback de escrita reservado e entry `uptime` somente leitura.
- [x] Montar automaticamente o procfs pinned e somente leitura em `/proc`, sem
  consumir volumes Storage e preservando o slot do devfs.
- [x] Implementar `/proc/uptime` com as linhas `uptime_ticks` e
  `frequency_hz`, em ASCII e ordem fixa.
- [x] Implementar snapshot imutável de até 16 KiB por abertura, ownership no
  contexto privado do `file_t`, leitura parcial, EOF, `lseek()` e liberação em
  sucesso e erro.
- [x] Encaminhar lookup, abertura, listagem, leitura, fechamento e seek pelo
  VFS, mantendo arquivos procfs como `VFS_NODE_REGULAR`.
- [x] Integrar `procfs_self_test()` ao autoteste VFS, incluindo escrita
  recusada, callback com erro/excedente, seek inválido, snapshot imutável e
  ausência de recursos residuais.

### Critério de saída

O comando `ls /proc` lista `uptime` e `cat /proc/uptime` retorna texto ASCII
válido com snapshots distintos, EOF e liberação corretos. A confirmação
funcional no QEMU foi recebida; `regcheck full` e `health check` também
concluíram sem falha de VFS. `/sys` permanece reservado ao PROC3.

### Comandos Shell / Diagnóstico

- `mount`: confirma a montagem correta do `procfs` (o pipeline do Shell ainda
  não é suportado).
- `ls /proc`: confirma a enumeração determinística do primeiro nó.
- `cat /proc/uptime`: confirma o snapshot textual e o cursor VFS.

---

## PROC2 - Mapeamento de /proc para Sistema e Processos

PROC2 concluido no codigo e validado funcionalmente pelo usuario. O procfs publica os
cinco nos globais `uptime`, `meminfo`, `cpuinfo`, `version` e `cmdline`, seguidos
por PIDs numericos crescentes. Cada diretorio PID lista `status`, `cmdline` e
`maps`, mantendo a ABI ASCII, snapshots imutaveis de ate 16 KiB e acesso
publico somente leitura.

`process_t` recebeu uma geracao de identidade append-only. As APIs de snapshot
copiam status, argumentos de lancamento e VMAs sem ponteiros; a enumeracao
inclui Idle, processos nativos, ring 3 e zombies ate o reaping. A captura de
VMAs valida a geracao e tenta novamente uma vez, retornando `ERR_AGAIN` se o
churn persistir. A memoria publicada soma stack de kernel, paginas residentes
de usuario e imagens de codigo/dados.

### Implementação

- [x] Implementar os nós de sistema em `/proc`:
  - `/proc/meminfo`: memória total, livre, usada, SLAB e buffers de disco.
  - `/proc/cpuinfo`: identificador do processador, flags e frequência estimada.
  - Expandir `/proc/uptime` com segundos de atividade e tempo gasto em modo
    idle; o PROC1 já publica ticks e frequência.
  - `/proc/version`: versão do ZephyrOS, data de compilação e compilador utilizado.
  - `/proc/cmdline`: argumentos passados pelo bootloader ao kernel.
- [x] Implementar diretórios dinâmicos para cada processo registrado (`/proc/<pid>/`):
  - `/proc/<pid>/status`: nome, estado, PPID, threads e uso de memória.
  - `/proc/<pid>/cmdline`: comando e argumentos que iniciaram o processo.
  - `/proc/<pid>/maps`: lista de VMAs e regiões de memória mapeadas.
- [x] Definir acesso público somente leitura e impedir que um PID reutilizado
  seja confundido com o processo anterior por meio de geração de identidade.

### Critério de saída

Comandos como `cat /proc/meminfo`, `cat /proc/cpuinfo`, `cat /proc/0/status`
e `cat /proc/0/maps` exibiram dados precisos em snapshots imutáveis, com PIDs
ordenados e geração publicada no status. A confirmação funcional foi recebida
em QEMU; `/sys` permanece reservado ao PROC3.

### Comandos Shell / Diagnóstico

- `cat /proc/meminfo`: inspeciona o estado global de memória.
- `cat /proc/<pid>/status`: inspeciona os atributos de um processo específico.
- `cat /proc/cpuinfo`, `cat /proc/version` e `cat /proc/cmdline`: inspecionam
  os nós globais adicionais.
- `ls /proc/<pid>`, `cat /proc/<pid>/cmdline` e `cat /proc/<pid>/maps`:
  inspecionam os nós dinâmicos de um PID listado.

---

## PROC3 - Mapeamento de /sys para Hardware

### Implementação

- [x] Criar o ponto de montagem `/sys` com o provider separado `sysfs`.
- [x] Mapear a hierarquia fixa de barramentos, classes e energia:
  - `/sys/bus/pci/devices/`: dispositivos PCI descobertos em ordem `BB:DD.F`.
  - `/sys/class/net/`: interfaces ordenadas pelo identificador nativo e seus atributos.
  - `/sys/class/block/`: discos e mídias ordenados por `block_device_t.id`.
  - `/sys/power/state`: controle e leitura dos estados de energia disponíveis.
- [x] Associar cada diretório a um objeto proprietário e cada atributo a um
  valor textual pequeno, com remoção segura quando o dispositivo desaparecer.
- [x] Manter os atributos de `/sys` separados dos relatórios compostos de
  `/proc`; controles de energia só serão graváveis após contrato próprio.

### Contrato PROC3

O provider `sysfs` é separado do `procfs` e monta automaticamente o volume
lógico `sysfs` em `/sys`. A montagem é pinned, não desmontável, pública,
somente leitura, usa `STORAGE_FS_NONE` e não consome Storage. A raiz virtual
do VFS lista `sys` junto com `mnt`, `dev` e `proc`.

A hierarquia publicada é fixa e segue esta ordem: `bus`, `class`, `power` na
raiz; `pci` em `bus`; `devices` em `bus/pci`; `net` e `block` em `class`; e
`state` em `power`. Dispositivos PCI são ordenados por bus/device/function;
interfaces e blocos são ordenados lexicograficamente pelo identificador
estável. Hardware ausente não cria placeholders.

Cada atributo é um arquivo regular com uma linha ASCII no formato
`<atributo> <valor>\n`. PCI, rede, blocos e energia publicam os atributos
definidos no contrato PROC3; números são decimais, hardware e BARs usam
hexadecimal minúsculo com `0x`, MAC usa hexadecimal, erros são decimais com
sinal e estados usam tokens estáveis. `/sys/power/state` publica as linhas
repetidas `state S0` até `state S5`, seguidas por `cpu_idle`,
`hardware_poweroff` e `reboot`.

A abertura copia o inventário e captura um snapshot imutável de até 16 KiB no
contexto privado do `file_t`; nenhum ponteiro para `pci_device_t`,
`network_interface_info_t` ou `block_device_t` permanece no descritor. O
`file_t.offset` é o cursor, `read()` admite blocos parciais e EOF retorna
`OK` com zero bytes. `lseek()` aceita `SET`, `CUR` e `END` dentro do snapshot.
Excesso retorna `ERR_OVERFLOW`, caminho ou cursor inválido retorna
`ERR_INVALID`, dispositivo ausente retorna `ERR_NOT_FOUND`, falta de memória
retorna `ERR_MEM` e escrita, `ioctl` ou `sync` retornam `ERR_UNAVAILABLE`.

O provider mantém uma geração interna do inventário para futuras atualizações;
PROC3 não adiciona hotplug ou rescan. A remoção posterior não invalida um
snapshot já aberto. `sysfs_validate_state()` verifica a geração, o limite de
snapshots ativos e a montagem VFS; `sysfs_self_test()` verifica também
formato ASCII, ausência de referências residuais e funcionamento sem volumes
Storage.

### Critério de saída

A árvore `/sys` permite navegar pela topologia de hardware como uma hierarquia
de diretórios, com atributos legíveis por `ls` e `cat`. A validação funcional
foi confirmada no QEMU: `mount` exibiu `/sys` como `SYSFS` e somente leitura;
`ls` confirmou a raiz, a hierarquia PCI e as classes de rede e bloco; e
`regcheck full`/`health check` concluíram sem falha de VFS. O nó
`/sys/power/state` é um arquivo regular e deve ser lido com `cat`, não com
`ls`.

### Comandos Shell / Diagnóstico

- `ls /sys/bus/pci/devices`: lista todos os dispositivos conectados ao barramento PCI.
- `mount`: confirma `/sys -> sysfs` como montagem `SYSFS`, `RO` e pinned.
- `ls /sys`, `ls /sys/bus`, `ls /sys/bus/pci`, `ls /sys/bus/pci/devices`:
  validam a ordem da hierarquia PCI.
- `ls /sys/class`, `ls /sys/class/net`, `ls /sys/class/block`: validam classes
  vazias ou inventários disponíveis em ordem determinística.
- `cat /sys/power/state`: valida o snapshot textual das capacidades de energia.
- `regcheck full` e `health check`: validam invariantes VFS e sysfs.

---

## PROC4 - Integração de Ferramentas Nativas

### Implementação

- [x] Criar o adaptador interno do Shell para ler snapshots por VFS, com
  leituras parciais, EOF, validação ASCII e parser de atributos.
- [x] Atualizar o Task Manager Classic para obter a lista de processos lendo
  `/proc/<pid>/status` e a memória lendo `/proc/meminfo`; o fallback Simple e
  a aba de threads permanecem no caminho interno existente.
- [x] Atualizar `devices` e `device-info` para consultar `/sys` em PCI,
  rede e bloco, mantendo fallback para dispositivos legados sem nó sysfs.
- [x] Adicionar `proccheck` ao dispatcher para validar os namespaces e os
  snapshots públicos sem alterar inventários ou processos.
- [x] Manter layouts binários, `taskmanager.h`, App API, syscalls e bootloader
  inalterados; não foi criada uma GUI de Device Manager inexistente no repo.
- [ ] Reservar alteração de parâmetros do kernel via escrita em nós de
  `/proc/sys/` para uma etapa posterior, com permissões, validação e ABI
  próprios; não habilitar escrita genérica em PROC4.

### Contrato de integração

O Classic consome somente cópias dos campos publicados por `procfs`. Cada
linha de processo é identificada por `pid + generation`; uma atualização com
churn repete a enumeração uma vez e descarta a captura instável. Ações de
terminação e reinício revalidam essa identidade antes de consultar o processo
atual. EIP, ESP, CR3, page directory e ponteiros de stack não fazem parte da
visão Classic migrada.

Os comandos `devices` e `device-info` consomem atributos de snapshots `sysfs`
para PCI, interfaces `net-*` e blocos. O inventário legado continua sendo a
fonte de fallback para PS/2, PIT, VGA, VESA, AC97 e speaker. Nenhum ponteiro,
descritor ou buffer do provider permanece no Shell depois da leitura.

### Critério de saída

As ferramentas gráficas e de terminal funcionam de forma desacoplada das
estruturas internas, consumindo snapshots textuais estáveis e liberando
referências mesmo quando processos ou dispositivos mudam durante a leitura.
O código desta etapa está implementado; o resumo PROC4 somente será marcado
como concluído após a confirmação funcional do usuário no QEMU.

### Comandos Shell / Diagnóstico

- `proccheck`: suite de validação de leitura e formatação de todos os nós de `/proc` e `/sys`.
