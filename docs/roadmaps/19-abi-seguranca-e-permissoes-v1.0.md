# Roadmap 19 — ABI, segurança e permissões da versão 1.0.0

## Estado

Em andamento. A SEC1 documental foi concluída; a validação executável e as
alterações de enforcement permanecem reservadas ao fechamento do Roadmap 19.
Esta frente endurece as fronteiras já existentes entre kernel,
processos ring 3, VFS, dispositivos e pacotes. Ela não cria um sistema
multiusuário completo nem altera silenciosamente a ABI de aplicativos.

Embora seja numerada depois do Roadmap 18, esta frente é transversal: seus
gates devem ser aplicados durante a implementação de memória, processos,
syscalls, VFS, dispositivos, Shell e atualização.

## Objetivo

Garantir que um aplicativo, uma entrada externa ou um pacote inválido não
consiga escapar dos limites publicados pelo kernel, corromper recursos de outro
processo ou transformar uma falha recuperável em falha global.

## Escopo

- validação de ponteiros, tamanhos, ranges, handles e códigos em todas as
  syscalls e na App API;
- separação efetiva entre ring 3 e ring 0, paging e ciclo de vida do processo;
- ownership de descritores, pipes, IPC, sinais, filas, VMA e buffers;
- confiança, versão, assinatura e preflight de pacotes ZPKG/ZAPP/ZUPD;
- identidade mínima de usuário/grupo, proprietário e permissões básicas;
- política mínima de acesso a caminhos, dispositivos, `/proc`, `/sys` e
  operações destrutivas;
- testes negativos, falhas injetadas, encerramento e limpeza.

Uma identidade mínima de usuário/grupo e permissões básicas fazem parte desta
frente quando forem necessárias para que a 1.0.0 seja apresentada como um
sistema de uso geral básico. ACL completa, domínio multiusuário, sandbox de
rede e políticas empresariais continuam fora do escopo.

## Dependências

- [Roadmap 18](18-kernel-processos-e-userland-v1.0.md) para a linha de base e
  reprodutibilidade;
- contratos atuais em `docs/qualidade/contratos-publicos.md` e `errors.h`.
- [Contrato de ABI, segurança e fronteiras](../qualidade/abi-seguranca-fronteiras.md)
  para o inventário e o modelo mínimo de identidade da SEC1.

## Fases

### SEC1 — Modelo de ameaça e fronteiras

- [x] Inventariar todas as entradas vindas de ring 3, Shell, VFS, drivers,
  pacotes e interrupções.
- [x] Definir para cada entrada o proprietário do recurso, a validade, a
  mutabilidade, o contexto de execução e o erro canônico.
- [x] Separar claramente dados de diagnóstico, comandos privilegiados e
  operações que alteram estado.
- [x] Confirmar documentalmente que nenhum ponteiro de kernel, objeto privado ou endereço de
  hardware atravessa a ABI.
- [x] Documentar quais capacidades continuam indisponíveis em processos ring3.
- [x] Fixar o modelo mínimo de identidade: root, usuário comum, UID, GID,
  grupos e credenciais herdadas na criação do processo.

Os itens da SEC1 estão concluídos no escopo documental. A confirmação
executável das fronteiras, credenciais e capacidades permanece pendente para
a matriz final do Roadmap 19.

### SEC2 — Auditoria de memória e syscalls

- [x] Validar ponteiros de entrada e saída antes de qualquer cópia ou acesso;
  strings e estruturas ring 3 são copiadas para buffers internos.
- [x] Validar tamanhos, adições, multiplicações, alinhamento e conversões sem
  overflow.
- [x] Revisar `mmap`/`munmap`, VMA, paging, cópia para usuário e encerramento
  após page fault, incluindo ranges extremos e publicação segura.
- [x] Revisar descritores, `lseek`, `ioctl`, pipes, sinais, IPC e sockets para
  handles obsoletos, double close e uso após liberação; rollback de handles e
  endereços foi coberto nas rotas ring 3 afetadas.
- [ ] Executar a decisão de permissão no `open` e revalidar operações sensíveis
  com as credenciais associadas ao processo e ao descritor; metadados de
  proprietário/modo ainda não existem no VFS e este item fica reservado à
  SEC5.
- [x] Confirmar que falhas preservam ownership e retornam somente códigos
  definidos em `errors.h` nos caminhos auditados.

Registro técnico da matriz: [`docs/qualidade/auditoria-sec2-memoria-syscalls.md`](../qualidade/auditoria-sec2-memoria-syscalls.md).

A implementação executável foi atualizada. Os testes essenciais da SEC2 e os
gates de integração `make q3check`, `make clean` e `make` passaram; warnings
legados fora da SEC2 foram observados sem erro de build. A suíte completa,
QEMU, TST7 e a matriz adversarial continuam concentrados no fechamento do
Roadmap 19.

### SEC3 — Ciclo de vida e isolamento de processos

- [x] Revalidar PID e generation em ações administrativas e callbacks tardios.
- [x] Impedir que processo encerrado continue recebendo eventos, sinais,
  descritores ou callbacks.
- [x] Testar criação, execução, falha, `SIGTERM`, `SIGKILL`, zombie, reaping e
  reutilização de PID nos testes essenciais afetados.
- [x] Confirmar proteção dos processos ring0 sem manter ponteiros no Shell ou
  em snapshots de longa duração.
- [x] Testar os caminhos afetados de tabela de processos, recursos, threads,
  filas, callbacks geracionais e falhas de recursos.

A implementação e a validação essencial da SEC3 estão concluídas. A matriz
completa, QEMU, TST7 e a validação adversarial permanecem pendentes para o
fechamento do Roadmap 19. Permissões efetivas por UID/GID continuam reservadas
à SEC5.

Registro técnico: [`docs/qualidade/auditoria-sec3-processos.md`](../qualidade/auditoria-sec3-processos.md).

### SEC4 — Pacotes e confiança (encerrada; DT100-003 aceita)

SEC4 encerrada por decisão de escopo, com dívida técnica `DT100-003` aceita.
Implementação concluída nas camadas de formato, raiz de confiança de
pacotes, serviço `PKG`, App Store local/remota, Shell e empacotador. ZPKG v1
permanece somente para inspeção/remoção; ZPKG v2 usa header de 128 bytes,
Ed25519 e `AUTH.DAT` na transação de três arquivos. A validação host e o build
completo passaram. A ausência das chaves privadas externas necessárias para
regenerar os fixtures AS5 foi registrada como `DT100-003`; por isso o gate
estrito `python tools/q3check.py` permanece pendente somente em
`confianca_as5`. O alvo `make q3check` aceita explicitamente essa dívida,
exibindo-a como `ACEITA`; os testes QEMU da App Store remota ficam para a
quitação da dívida. Os fixtures v1 não são aceitos pelo runtime.

- [x] Validar assinatura, versão, tamanho, CRC/hash, dependências e limites de
  cada pacote antes de instalar ou executar.
- [x] Rejeitar caminhos fora do destino, nomes inválidos, duplicatas,
  truncamento, arquivos inesperados e manifestos ambíguos.
- [x] Confirmar que instalação, atualização, rollback e remoção são
  transacionais ou deixam estado explicitamente recuperável.
- [x] Impedir execução de pacote não autorizado sem apagar o estado anterior.
- [x] Garantir que logs de falha não exponham chaves, tokens ou dados sensíveis.
- [x] Encerrar SEC4 com a limitação de chaves externas registrada em
  `DT100-003`, sem versionar material privado nem enfraquecer a verificação.

### SEC5 — Política mínima de recursos

- [x] Definir a tabela de capacidades para arquivos, dispositivos, rede,
  energia e diagnósticos.
- [x] Definir bits de leitura, escrita e execução para o proprietário, grupo e
  demais usuários, incluindo a política de criação e herança de arquivos.
- [x] Definir limites por processo para memória, descritores, processos filhos,
  filas e tamanho de argumentos.
- [x] Manter `/proc` somente leitura, `/sys` somente leitura e `/proc/sys`
  gravável apenas pelo contexto nativo previsto no contrato.
- [x] Rejeitar escrita, `ioctl`, sync ou redirecionamento quando o tipo do nó
  ou o privilégio não permitirem a operação.
- [x] Diferenciar `ERR_INVALID`, `ERR_NOT_FOUND`, `ERR_UNAVAILABLE`,
  `ERR_OVERFLOW`, `ERR_MEM`, `ERR_STATE` e `ERR_AGAIN` sem remapeamentos
  ambíguos.
- [x] Registrar falhas na camada com contexto, evitando duplicação de logs.
- [x] Persistir permissões FAT32 em `ZPERM.DAT` versionado, com CRC32,
  registros ordenados, limite de 128 entradas, defaults de migração e bloqueio
  seguro para corrupção, truncamento, duplicatas e falta de capacidade.
- [x] Integrar criação, remoção, renomeação, staging e diagnósticos aos
  metadados, preservando FAT12 somente leitura sem sidecar.
- [x] Acrescentar credenciais append-only aos processos e snapshots e expor
  quotas, UID, GID, capacidades e modos efetivos sem ponteiros privados.

Implementação SEC5 concluída nas camadas de processo, IPC, VFS, Storage,
procfs, Shell, Makefile e testes host. O caso `host:storage:permissions` está
`AUTOMATED` e passou junto com as demais suítes diretamente afetadas; o
catálogo foi sincronizado, renderizado e validado. `make clean`, `make` e
`make q3check` passaram, com `DT100-003` exibida como dívida aceita em
`confianca_as5`; o gate estrito continua distinguindo essa ocorrência. Não há
nova falha SEC5. Bootloader, Stage 2, ABI ring3 e syscalls não foram alterados.

Os testes QEMU diretamente relacionados também passaram após a correção de um
backup transacional grande demais na pilha do Shell: TST4 storage/VFS, TST5
Shell, aplicativos, processos, storage e update-recovery, além dos perfis TST6
baseline e mínimo e dos estresses de aplicativos e storage. A execução remota
da App Store continua pendente exclusivamente por `DT100-003`. SEC5 está
encerrada; a pendência não é uma falha nova desta etapa.

### SEC6 — Validação adversarial

- [ ] Criar fixtures para ponteiros inválidos, tamanhos máximos, handles
  obsoletos, caminhos inválidos, pacotes corrompidos e recursos ausentes.
- [ ] Exercitar falha de memória, tabela cheia, timeout, cancelamento e erro
  de hardware sem deixar recursos residuais.
- [ ] Repetir os testes nos modos Simple e Classic e nos perfis sem ACPI, NIC,
  USB, áudio, VESA e Storage adicional.
- [ ] Integrar os invariantes ao diagnóstico apropriado sem tornar o comando
  destrutivo.

## Critérios de saída

- Não existe caminho conhecido de ring 3 para acessar memória, handles ou
  recursos de outro processo fora do contrato.
- Pacotes inválidos e entradas malformadas falham antes do efeito persistente.
- Falhas, cancelamentos e encerramentos não deixam processos, descritores,
  buffers, callbacks ou locks residuais.
- O conjunto de contratos públicos está congelado e documenta qualquer
  capacidade deliberadamente ausente.
- A identidade mínima e as permissões básicas são verificadas em processos,
  VFS, dispositivos, energia e pacotes.
- A matriz negativa passa sem panic, corrupção silenciosa ou alteração não
  autorizada de estado.

## Fora do escopo

Antivírus, rootkit detection, criptografia geral de arquivos, contas
multiusuário, ACL completa, sandbox de rede e secure boot não serão simulados
para preencher este roadmap. Eles podem ser priorizados depois da 1.0.0.

## Validação por etapa e fechamento

Cada etapa deve executar, após autorização explícita, os gates essenciais
`make q3check` e `make clean && make`, além dos testes automatizados diretamente
afetados pela alteração. Isso não exige executar 100% da suíte em cada etapa.

QEMU, TST7, a matriz adversarial completa e os diagnósticos finais
(`appcheck`, `memcheck`, `schedcheck`, `proccheck`, `regcheck full` e
`health check`) ficam concentrados no fechamento do Roadmap 19, salvo
autorização explícita para antecipá-los.
