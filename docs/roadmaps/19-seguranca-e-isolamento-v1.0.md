# Roadmap 19 — Segurança e isolamento para a versão 1.0.0

## Estado

Planejado. Esta frente endurece as fronteiras já existentes entre kernel,
processos ring 3, VFS, dispositivos e pacotes. Ela não cria um sistema
multiusuário completo nem altera silenciosamente a ABI de aplicativos.

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
- política mínima de acesso a caminhos, dispositivos, `/proc`, `/sys` e
  operações destrutivas;
- testes negativos, falhas injetadas, encerramento e limpeza.

Contas, UID/GID, ACL completa, sandbox de rede e permissões por aplicativo são
decisões separadas. Se forem indispensáveis para a 1.0.0, deverão receber um
contrato versionado antes do congelamento da ABI.

## Dependências

- [Roadmap 18](18-estabilizacao-e-release-v1.0.md) para a linha de base e
  reprodutibilidade;
- contratos atuais em `docs/qualidade/contratos-publicos.md` e `errors.h`.

## Fases

### SEC1 — Modelo de ameaça e fronteiras

- [ ] Inventariar todas as entradas vindas de ring 3, Shell, VFS, drivers,
  pacotes e interrupções.
- [ ] Definir para cada entrada o proprietário do recurso, a validade, a
  mutabilidade, o contexto de execução e o erro canônico.
- [ ] Separar claramente dados de diagnóstico, comandos privilegiados e
  operações que alteram estado.
- [ ] Confirmar que nenhum ponteiro de kernel, objeto privado ou endereço de
  hardware atravessa a ABI.
- [ ] Documentar quais capacidades continuam indisponíveis em processos ring3.

### SEC2 — Auditoria de memória e syscalls

- [ ] Validar ponteiros de entrada e saída antes de qualquer cópia ou acesso.
- [ ] Validar tamanhos, adições, multiplicações, alinhamento e conversões sem
  overflow.
- [ ] Revisar `mmap`/`munmap`, VMA, paging, cópia para usuário e encerramento
  após page fault.
- [ ] Revisar descritores, `lseek`, `ioctl`, pipes, sinais, IPC e sockets para
  handles obsoletos, double close e uso após liberação.
- [ ] Confirmar que falhas preservam ownership e retornam somente códigos
  definidos em `errors.h`.

### SEC3 — Ciclo de vida e isolamento de processos

- [ ] Revalidar PID e generation em ações administrativas e callbacks tardios.
- [ ] Impedir que processo encerrado continue recebendo eventos, sinais,
  descritores ou callbacks.
- [ ] Testar criação, execução, falha, `SIGTERM`, `SIGKILL`, zombie, reaping e
  reutilização de PID.
- [ ] Confirmar proteção dos processos ring0 sem manter ponteiros no Shell ou
  em snapshots de longa duração.
- [ ] Testar pressão da tabela de processos, heap, PMM, filas e limites de
  argumentos.

### SEC4 — Pacotes e confiança

- [ ] Validar assinatura, versão, tamanho, CRC/hash, dependências e limites de
  cada pacote antes de instalar ou executar.
- [ ] Rejeitar caminhos fora do destino, nomes inválidos, duplicatas,
  truncamento, arquivos inesperados e manifestos ambíguos.
- [ ] Confirmar que instalação, atualização, rollback e remoção são
  transacionais ou deixam estado explicitamente recuperável.
- [ ] Impedir execução de pacote não autorizado sem apagar o estado anterior.
- [ ] Garantir que logs de falha não exponham chaves, tokens ou dados sensíveis.

### SEC5 — Política mínima de recursos

- [ ] Definir a tabela de capacidades para arquivos, dispositivos, rede,
  energia e diagnósticos.
- [ ] Manter `/proc` somente leitura, `/sys` somente leitura e `/proc/sys`
  gravável apenas pelo contexto nativo previsto no contrato.
- [ ] Rejeitar escrita, `ioctl`, sync ou redirecionamento quando o tipo do nó
  ou o privilégio não permitirem a operação.
- [ ] Diferenciar `ERR_INVALID`, `ERR_NOT_FOUND`, `ERR_UNAVAILABLE`,
  `ERR_OVERFLOW`, `ERR_MEM`, `ERR_STATE` e `ERR_AGAIN` sem remapeamentos
  ambíguos.
- [ ] Registrar falhas na camada com contexto, evitando duplicação de logs.

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
- A matriz negativa passa sem panic, corrupção silenciosa ou alteração não
  autorizada de estado.

## Fora do escopo

Antivírus, rootkit detection, criptografia geral de arquivos, contas de
usuário, ACL completa, sandbox de rede e secure boot não serão simulados para
preencher este roadmap. Eles podem ser priorizados depois da 1.0.0.

## Validação do usuário

O agente não executará build, testes ou QEMU. A validação deve combinar os
diagnósticos existentes (`appcheck`, `memcheck`, `schedcheck`, `proccheck`,
`regcheck full` e `health check`) com as fixtures negativas registradas no
roadmap e no registro de validações.
