# ABI, segurança e fronteiras de confiança

## Estado

SEC1 documental concluída em 2026-09-08. Este documento congela o modelo
mínimo de fronteiras e identidade para as etapas SEC2-SEC6. Ele não declara
que toda a política já esteja aplicada no runtime; a validação executável e a
implementação das permissões ficam registradas como pendências do Roadmap 19.

## Objetivo

Impedir que uma entrada externa ultrapasse o contrato da camada receptora,
publique estado privado ou transforme uma falha local em alteração global.
Cada fronteira deve usar dados copiados, snapshots, IDs geracionais ou
handles opacos, conforme o recurso, e retornar somente estados e erros
publicados pelo contrato correspondente.

## Domínios de confiança

| Domínio | Responsabilidade | Confiança publicada |
|---|---|---|
| Kernel e drivers ring 0 | Memória, interrupções, processos, VFS, dispositivos e energia | Possui acesso privilegiado; deve validar toda entrada externa e proteger estado privado. |
| Serviços nativos ring 0 | `System`, `Shell`, `Desktop` e `kworker` | São componentes internos confiáveis, supervisionados pelo kernel; não formam uma ABI de aplicativos. |
| Aplicativos ring 3 | Execução de ZAPP e uso da App API | Não confiáveis por padrão; acessam somente syscalls, handles e dados copiados autorizados. |
| Shell e comandos | Parsing, diagnóstico e solicitação de operações | Entrada controlada pelo usuário; parsing não concede privilégio e handlers devem separar diagnóstico de mutação. |
| VFS e pseudo-filesystems | Caminhos, descritores, pipes, dispositivos e snapshots | Ownership pertence ao processo, descritor ou volume definido pelo contrato; referências obsoletas devem falhar. |
| Pacotes e atualizações | ZPKG, ZAPP, ZUPD e ZSYS | Dados externos não confiáveis até preflight, verificação e autorização das etapas posteriores. |
| IRQs e callbacks | Eventos de hardware e trabalho adiado | Não carregam ponteiros persistentes de proprietários encerrados; identidade deve ser revalidada antes da entrega. |

## Inventário das entradas externas

| Entrada | Ponto de entrada atual | Dados aceitos | Ownership e mutabilidade | Contexto e erro |
|---|---|---|---|---|
| Aplicativo ring 3 | `int 0x80`, dispatcher de `syscall_handler()` e wrappers da App API | Número da syscall, escalares, buffers e estruturas em memória de usuário | Buffer pertence ao chamador durante a chamada; kernel lê ou copia conforme o contrato e não publica ponteiro privado | Contexto de syscall; rejeitar ponteiro, tamanho, estado ou handle inválido com erro canônico. |
| Loader de aplicativo | `process_create_user_image*()` e `app run` | Imagem, dados, entry offset e argumentos limitados | Loader copia a imagem e os argumentos para o processo; o chamador não retém ownership do armazenamento interno | Inicialização/criação; falha deve liberar recursos parciais e retornar erro. |
| Shell | Dispatcher e handlers de `shell_commands_*` | Linha normalizada, argumentos e comandos registrados | Buffer de entrada pertence ao Shell; handlers recebem argumentos validados e não devem acessar estado privado de outro módulo | Contexto cooperativo do Shell; entradas inválidas são rejeitadas sem mutação parcial. |
| VFS e descritores | `open`, `read`, `write`, `close`, `lseek`, `ioctl`, `pipe`, `poll` e `select` | Caminhos, handles, buffers, flags e estruturas de consulta | FD/handle pertence ao processo; buffers de usuário são emprestados somente durante a chamada; filas e vnodes seguem o owner do recurso | Syscall ou worker associado; handles obsoletos e operações fora de estado retornam erro canônico. |
| Processos e sinais | Criação, sinais, cancelamento, reaping e callbacks | PID, generation, sinal, código de saída e snapshots | Identidade pública é `PID + generation`; ponteiros para processos são internos e devem ser revalidados antes do uso | Kernel ou worker; identidade encerrada/reutilizada não pode receber evento. |
| Drivers | Inventário, comandos de dispositivo, IRQ e callbacks | IDs, capacidades, buffers, requests e estados de hardware | DMA, MMIO, filas e recursos de driver permanecem no driver; consultas públicas usam cópias | IRQ não bloqueante ou contexto adiável; hardware ausente retorna estado degradado/indisponível. |
| Pacotes e atualizações | App Store, loader, updater e runtimes ZPKG/ZAPP/ZUPD/ZSYS | Manifestos, nomes, versões, hashes, assinaturas, dados e planos | O pacote é entrada não confiável; staging, journal e estado anterior pertencem ao serviço de transação | Worker cooperativo; preflight e autorização devem ocorrer antes de efeito persistente. |
| Interrupções e eventos adiados | IDT, IRQ deferred, wait queues e workqueue | Número da linha, evento, callback e identidade do proprietário | Callback não possui o processo; o owner deve permanecer válido ou o trabalho é descartado | IRQ/top-half ou bottom-half; não bloquear, alocar sem contrato ou publicar ponteiro privado. |

## Regras de fronteira

1. Ponteiros de kernel, objetos privados, stacks, diretórios de paginação,
   endereços físicos e endereços MMIO nunca atravessam a ABI ring 3.
2. Snapshots públicos devem ser cópias por valor e conter somente campos
   necessários ao diagnóstico ou ao contrato da operação.
3. Handles e IDs não são ponteiros. Quando houver reutilização de identidade,
   a geração deve fazer parte da validação interna antes de executar a ação.
4. Buffers de usuário são válidos apenas durante a chamada que os recebe; o
   kernel não deve guardar aliases para uso posterior sem copiar os dados e
   assumir ownership explícito.
5. Toda operação que adquire recurso define owner, validade, transferência e
   liberação. Falhas devem desfazer aquisições parciais.
6. Diagnóstico somente consulta estado; comando privilegiado solicita uma
   ação autorizada; operação mutável deve declarar o efeito e o rollback ou
   estado recuperável aplicável.
7. Erros públicos devem usar os valores de `errors.h`, sem remapeamento local
   que confunda entrada inválida, ausência, indisponibilidade, memória,
   overflow, estado ou tentativa posterior.
8. Logs registram o contexto suficiente para diagnosticar a falha, mas nunca
   expõem tokens, chaves, buffers sensíveis, ponteiros ou endereços privados.

## Capacidades indisponíveis para ring 3

Processos ring 3 não possuem acesso direto a:

- memória do kernel, tabelas de páginas, stacks de ring 0, PMM, SLAB ou MMIO;
- portas de I/O, IRQs, DMA, PCI, ATA, USB, ACPI, speaker e controladores de
  rede sem uma API nativa autorizada;
- destruição ou reconfiguração arbitrária de outros processos, serviços,
  descritores, filas e callbacks;
- escrita direta em `/proc`, `/sys`, `/proc/sys`, dispositivos e volumes sem
  que o contrato posterior de permissões autorize explicitamente;
- instalação, atualização, rollback, remoção ou execução de pacote sem o
  preflight e a política de confiança das etapas SEC4/SEC5;
- acesso a credenciais, chaves, tokens e estado privado de outros processos.

As capacidades que já existem pela App API continuam limitadas aos handles,
buffers, sinais, IPC, VFS e operações explicitamente publicados por seus
contratos. Esta lista descreve a fronteira; a revalidação efetiva e a política
de permissão está implementada na SEC5 e foi validada pela bateria host
diretamente afetada.

## Modelo mínimo de identidade

| Identidade | UID | GID primário | Uso nesta versão |
|---|---:|---:|---|
| root | 0 | 0 | Contexto administrativo e serviços nativos confiáveis. |
| usuário comum | 1000 | 1000 | Identidade padrão de aplicativos ring 3 iniciados pelo usuário. |

Regras do modelo:

- cada processo possui uma credencial efetiva e uma credencial herdada na
  criação; a herança não transforma um aplicativo ring 3 em serviço ring 0;
- filhos herdam UID, GID primário e a máscara fixa de capacidades do criador;
  não existe transição pública para root;
- grupos são inicialmente uma lista fixa associada à credencial, sem contas
  persistentes, login, troca de usuário ou ACL completa;
- ring 0 é uma fronteira de privilégio de execução, não um UID que possa ser
  solicitado por aplicativo;
- nenhuma credencial, grupo ou ponteiro privado é exposto em snapshots que não
  precisem dessa informação;
- a aplicação efetiva de bits de leitura, escrita, execução e capacidades por
  recurso pertence ao VFS e revalida o contexto no ponto de efeito.

## Estado da implementação

Esta SEC1 congela o contrato documental e não adiciona campos a `process_t`,
não cria syscall, não altera a App API e não implementa ainda enforcement de
UID/GID ou permissões. A SEC2 deverá revisar validação de entradas e syscalls;
SEC3 deverá revisar ciclo de vida e identidade; SEC4 deverá revisar confiança
de pacotes; SEC5 aplica capacidades, permissões e quotas mínimas; SEC6 deverá
executar a matriz adversarial completa.

## Validação pendente

Nenhum build, teste host-only, QEMU, TST7 ou matriz negativa foi executado
para esta etapa. O fechamento do Roadmap 19 deve validar, no mínimo, ponteiros
inválidos, tamanhos extremos, handles obsoletos, caminhos e pacotes inválidos,
falhas de recurso, limpeza de callbacks e perfis de hardware ausente.

## Estado SEC5

A implementação da SEC5 adiciona credenciais append-only a processos e
snapshots, capacidades fixas, autorização POSIX mínima no VFS, `ZPERM.DAT` em
FAT32 gravável, defaults de migração, modos de pacotes e quotas adicionais.
Procfs, Shell e diagnósticos expõem apenas snapshots sem ponteiros ou estado
privado. Os testes host diretamente afetados passaram, assim como `make clean`,
`make` e `make q3check`, que exibe `DT100-003` como dívida aceita em
`confianca_as5`. O gate estrito continua distinguindo essa ocorrência, sem nova
falha SEC5.

Os cenários QEMU de storage/VFS, Shell, aplicativos, processos,
update-recovery, baseline, mínimo e estresses de aplicativos/storage passaram
após a correção do backup transacional de permissões para heap. O cenário
remoto da App Store continua pendente apenas por `DT100-003`.

Bootloader, Stage 2, ABI ring3 e numeração de syscalls permaneceram
inalterados.
