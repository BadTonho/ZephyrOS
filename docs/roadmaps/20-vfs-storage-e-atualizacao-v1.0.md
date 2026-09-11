# Roadmap 20 — VFS, Storage e atualização do sistema da 1.0.0

## Estado

STO1, STO2, STO3, STO4, STO5 e a implementação/validação host-only do STO6
concluídos; a validação funcional QEMU do STO5/STO6 e a fase STO7 permanecem
pendentes. Esta frente torna o caminho FAT12/FAT32, Block Layer, buffer cache,
VFS e Storage previsível diante de erro de I/O, cancelamento, reinicialização e
falha de energia. Não substitui os formatos existentes nem cria um filesystem
novo para a versão 1.0.0.

## Objetivo

Evitar corrupção silenciosa e garantir que uma operação interrompida termine
como concluída, desfeita ou explicitamente recuperável, com ownership claro de
blocos, buffers, volumes, descritores e transações.

O Roadmap 20 possui o backend da atualização do sistema: armazenamento de
artefatos, staging, ativação, confirmação e rollback. O Roadmap 21 fornece as
capacidades de rede, energia e hardware; o Roadmap 22 fornece os comandos e a
interface. Nenhuma dessas camadas poderá sobrescrever diretamente o sistema
em execução.

## Escopo

- invariantes do Block Layer, cache, filas, ATA, USB MSC, FAT12 e FAT32;
- sincronização única e ordenada entre cache, filesystem e dispositivo;
- montagem, desmontagem, CWD, handles abertos e volumes pinned/ocupados;
- verificação de consistência somente leitura e diagnóstico de corrupção;
- recuperação de transações interrompidas e limpeza de estado temporário;
- limites, overflow, clusters inválidos, cadeias circulares e setores
  inacessíveis;
- integração segura com `poweroff`, `reboot`, Updater e operações do Shell;
- atualização do próprio sistema pela internet, com staging, assinatura,
  preflight, ativação transacional, confirmação de boot e rollback.

Formatação inteligente, migração para outro filesystem, compressão de disco,
swap e recuperação automática destrutiva ficam fora desta frente.

## Dependências

- [Roadmap 18](18-kernel-processos-e-userland-v1.0.md) para os gates de base;
- [Roadmap 19](19-abi-seguranca-e-permissoes-v1.0.md) para ownership e fronteiras;
- Roadmaps 10 e 13 para VFS, Block Layer e buffer cache;
- contratos de Storage, FAT, VFS e atualizações em `docs/08-sistema-arquivos/`
  e `docs/14-atualizacoes/`.

## Fases

### STO1 — Inventário de invariantes

- [x] Documentar geometria, limites e ownership de cada camada: setor, bloco,
  cluster, buffer, requisição, fila, cache e volume.
- [x] Validar conversões LBA/cluster, tamanhos de arquivo, capacidade,
  contagens e endereços sem overflow.
- [x] Rejeitar FAT, diretório, cadeia ou entrada que apontem para regiões fora
  do volume ou para estruturas incompatíveis.
- [x] Confirmar que um erro de dispositivo nunca é publicado como escrita
  concluída.
- [x] Integrar contadores de erro e último erro ao diagnóstico sem logging em
  cada setor.
- [x] Manter uma separação explícita entre superblock/volume, inode/entrada,
  descritor aberto, cache e dispositivo físico.

STO1 foi implementado com validação de intervalos e aritmética protegida no
Block Layer, cache, Storage, FAT12, FAT32 e cursores da interface unificada.
As fixtures host-only cobrem BIOs inválidos, clusters fora do volume, cadeias
incompatíveis, falhas de leitura e a preservação de um FAT12 com diretório raiz
cheio sem formatação automática. A validação QEMU, transações e recuperação
permanecem nas fases STO2–STO7.

### STO2 — Sync, flush e transações

- [x] Definir a ordem de writeback de dados, FAT, diretórios, metadados e
  estruturas do cache.
- [x] Garantir que `storage_sync_all()` seja idempotente e que cada operação de
  encerramento faça sync apenas uma vez.
- [x] Definir estado sujo, em andamento, concluído, abortado e recuperável para
  operações compostas.
- [x] Preservar a versão anterior quando preflight, capacidade, escrita ou
  confirmação falharem.
- [x] Definir a diferença entre escrita aceita, `sync`, `flush`, durabilidade
  confirmada e recuperação após queda de energia.
- [x] Garantir que `rename` e substituição de metadados não deixem uma entrada
  parcialmente publicada após uma falha.
- [x] Testar timeout, erro ATA, erro USB MSC, cancelamento e reinicialização no
  meio de cada fase.

A implementação STO2 formaliza as fases internas de transação, reserva clusters
FAT32 antes da mutação persistente e aplica barreiras entre dados, FAT e
diretórios. `storage_sync_volume()` e `storage_sync_all_until()` preservam o
primeiro erro, recusam sync concorrente e mantêm a semântica de durabilidade
degradada para dispositivos sem `FLUSH`. O escritor `ZSTG.ZSY` publica o
destino somente depois do conteúdo e da entrada temporária estarem
sincronizados; falhas pós-commit permanecem diagnosticáveis como recuperáveis.
O agregado host-only é `make test-sto2-host`; journal persistente e recuperação
após reboot continuam reservados ao STO6–STO7.

### STO3 — VFS e ciclo de vida dos volumes

- [x] Bloquear novas operações normais durante sync/desmontagem sem bloquear
  diagnósticos necessários para explicar a falha.
- [x] Rejeitar desmontagem de volume com arquivo aberto, operação ativa ou CWD
  apontando para ele.
- [x] Desmontar apenas volumes não-pinned e preservar `/`, `/dev`, `/proc` e
  `/sys` conforme seus contratos.
- [x] Confirmar que handles, CWD, caches, filas e referências ao volume sejam
  invalidados ou transferidos sem uso após liberação.
- [x] Definir a hierarquia mínima persistente e temporária (`/etc`, `/var`,
  `/run`, `/home` e `/tmp`) e quais volumes podem hospedá-la.
- [x] Repetir mount/unmount, perda de dispositivo e ausência de Storage sem
  referências residuais.

STO3 foi implementado com um gate interno de ciclo de vida, refresh atômico e
gerações monotônicas próprias do namespace VFS. Operações normais são recusadas
durante transições, enquanto fechamento, liberação e diagnósticos de snapshot
permanecem disponíveis. Montagens pinned, arquivos abertos, CWDs ativos e
operações em andamento impedem desmontagens inseguras; perda de Storage remove
aliases sem referências e conserva aliases diagnosticáveis enquanto houver
referências. A hierarquia `/etc`, `/var` e `/home` permanece no volume raiz,
com `/run` e `/tmp` temporários, sem criação automática durante refresh.
O agregado host-only é `make test-sto3-host`; a matriz QEMU completa permanece
reservada às fases posteriores.

### STO4 — Verificação de consistência

- [x] Criar diagnóstico somente leitura para BPB, FAT, diretórios, cadeias,
  tamanhos, clusters livres e duplicidades.
- [x] Detectar ciclo, cluster reservado, arquivo truncado, tamanho impossível,
  nome inválido, diretório inconsistente e setores fora do volume.
- [x] Publicar contagem de erros, avisos e estruturas verificadas com retorno
  canônico.
- [x] Garantir que a verificação não altere FAT, diretórios, timestamps,
  cache, processos ou hardware.
- [x] Adicionar fixtures pequenas, grandes, vazias, corrompidas e de volume
  ausente.

STO4 foi implementado como uma verificação somente leitura comum a FAT12 e
FAT32. O caminho valida novamente o MBR e o BPB, compara todas as cópias da
FAT, verifica entradas reservadas, cadeias, ciclos, tamanhos, ownership,
duplicidades e LFNs, e publica órfãos e divergências não essenciais como
avisos. As leituras usam BIO físico sem criar ou invalidar entradas do cache.
`storage_check()` preserva a assinatura pública; os contadores chegam ao Shell
por um bridge interno e aparecem em `storage check <id>` para FAT12 e FAT32.
O agregado host-only é `make test-sto4-host`; a matriz QEMU, reparo e
recuperação permanecem para STO6–STO7.

### STO5 — Atualização do sistema

- [x] Consultar manifesto remoto autenticado por HTTPS ou pelo transporte
  remoto já validado pelo projeto.
- [x] Validar assinatura, hash, tamanho, versão, arquitetura, compatibilidade,
  dependências e política de downgrade antes de escrever no destino.
- [x] Baixar kernel, arquivos de sistema e componentes autorizados para uma
  área de staging sem sobrescrever a versão em execução.
- [x] Verificar espaço, integridade do Storage, energia disponível e capacidade
  de recuperação antes do commit.
- [x] Manter slots A/B ou mecanismo equivalente com versão ativa e candidata
  fisicamente separadas.
- [x] Registrar tentativa de boot, estado `pending`, confirmação `good`,
  limite de tentativas e rollback automático para a versão anterior.
- [x] Ativar a versão nova de forma transacional, preservando bootloader e
  layout da imagem, somente após a gravação integral e a validação local.
- [x] Validar compatibilidade mínima entre kernel, recovery, bootloader,
  filesystem e componentes do artefato.
- [x] Recuperar interrupções por falha de rede, falta de espaço,
  reinicialização, queda de energia ou erro de escrita.
- [x] Publicar estado, progresso, versão candidata, erro e resultado no Shell,
  Settings e diagnósticos por um contrato de estado, mantendo o prompt
  utilizável.
- [x] Manter fallback para atualização local/offline quando o servidor remoto
  estiver indisponível.
- [x] Rejeitar manifestos, imagens e componentes não assinados, truncados,
  incompatíveis ou fora da política de atualização.
- [x] Definir rotação, revogação e expiração da confiança usada para validar
  futuras atualizações, sem aceitar chave remota arbitrária.

STO5 foi implementado sobre os caminhos existentes de ZUPD, ZSYS, runtime,
transporte remoto, staging e slots A/B. A política de confiança agora inclui
janela de `target_epoch` e revogação estática, sem versionar chaves privadas;
ZSYS remoto exige HTTPS, e o slot ativo só é substituído após a confirmação
da candidata. O agregado `make test-sto5-host`, `q3check`, build completo,
`catalog-test` e os testes Python do updater passaram. A matriz QEMU de
atualização, reboot e rollback permanece `PENDING` para execução funcional.

### STO6 — Recuperação

- [x] Definir como estados temporários são identificados após boot, falha ou
  cancelamento.
- [x] Recuperar somente operações com evidência suficiente; nunca inventar
  metadados nem sobrescrever dados sem autorização explícita.
- [x] Preservar journal/registro de atualização existente e separar sua
  recuperação da consistência FAT geral.
- [x] Garantir rollback limpo para transações do Updater e operações normais do
  Storage.
- [x] Recuperar uma atualização que morreu entre staging, ativação, primeiro
  boot e confirmação de estado saudável.
- [x] Expor motivo e limite da recuperação em `health` e diagnósticos.

A implementação STO6 agora valida o estado redundante e os journals antes de
qualquer limpeza, preserva staging órfão sem journal comprovante, verifica os
arquivos do runtime antes de publicar um commit e limita a política de boot a
duas tentativas totais. Após o limite, o recovery tenta retornar ao slot
anterior validado; divergências permanecem em recuperação pendente. Passaram
`make q3check`, `make clean`, `make`, `make test-sto6-host` e
`make catalog-test`; a matriz QEMU de recuperação permanece `PENDING` para
execução funcional.

### STO7 — Matriz de falhas

- [ ] Validar FAT12 e FAT32 com ATA PIO, volumes adicionais e USB MSC.
- [ ] Validar leitura, escrita, exclusão, rename, diretórios, índice global,
  pipes, redirecionamento e snapshots virtuais.
- [ ] Exercitar falta de memória, cache cheio, fila cheia, dispositivo removido,
  setor inválido e timeout.
- [ ] Repetir os cenários depois de `poweroff`, `reboot` e cancelamento quando
  o commit ainda não começou.
- [ ] Confirmar ausência de corrupção silenciosa e de recursos residuais.

## Critérios de saída

- Toda operação de Storage possui estado, owner, limite e resultado
  observáveis.
- Falhas antes do commit preservam o estado anterior ou publicam recuperação
  necessária; nenhuma falha é tratada como sucesso silencioso.
- Sync não é duplicado em caminhos de encerramento, update ou desmontagem.
- Uma atualização nunca destrói a única cópia inicializável do sistema antes
  de existir uma candidata validada e um caminho de rollback.
- Verificação e recuperação não quebram volumes pinned nem pseudo-filesystems.
- A matriz de falhas termina sem leaks, double free, handles obsoletos ou
  referências a dispositivos desaparecidos.

## Fora do escopo

Não serão implementados neste roadmap um filesystem novo, ext4/NTFS/exFAT,
swap, compressão persistente, recuperação destrutiva automática ou uma
ferramenta gráfica de formatação.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário deve combinar os
diagnósticos de Storage, VFS, memória e atualização com fixtures de falha e
reinicialização, registrando a evidência antes de marcar qualquer fase como
concluída.
