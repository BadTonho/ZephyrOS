# Roadmap 20 — VFS, Storage e atualização do sistema da 1.0.0

## Estado

Planejado. Esta frente torna o caminho FAT12/FAT32, Block Layer, buffer cache,
VFS e Storage previsível diante de erro de I/O, cancelamento, reinicialização e
falha de energia. Não substitui os formatos existentes nem cria um filesystem
novo para a versão 1.0.0.

## Objetivo

Evitar corrupção silenciosa e garantir que uma operação interrompida termine
como concluída, desfeita ou explicitamente recuperável, com ownership claro de
blocos, buffers, volumes, descritores e transações.

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
  preflight, ativação transacional e rollback.

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

- [ ] Documentar geometria, limites e ownership de cada camada: setor, bloco,
  cluster, buffer, requisição, fila, cache e volume.
- [ ] Validar conversões LBA/cluster, tamanhos de arquivo, capacidade,
  contagens e endereços sem overflow.
- [ ] Rejeitar FAT, diretório, cadeia ou entrada que apontem para regiões fora
  do volume ou para estruturas incompatíveis.
- [ ] Confirmar que um erro de dispositivo nunca é publicado como escrita
  concluída.
- [ ] Integrar contadores de erro e último erro ao diagnóstico sem logging em
  cada setor.

### STO2 — Sync, flush e transações

- [ ] Definir a ordem de writeback de dados, FAT, diretórios, metadados e
  estruturas do cache.
- [ ] Garantir que `storage_sync_all()` seja idempotente e que cada operação de
  encerramento faça sync apenas uma vez.
- [ ] Definir estado sujo, em andamento, concluído, abortado e recuperável para
  operações compostas.
- [ ] Preservar a versão anterior quando preflight, capacidade, escrita ou
  confirmação falharem.
- [ ] Testar timeout, erro ATA, erro USB MSC, cancelamento e reinicialização no
  meio de cada fase.

### STO3 — VFS e ciclo de vida dos volumes

- [ ] Bloquear novas operações normais durante sync/desmontagem sem bloquear
  diagnósticos necessários para explicar a falha.
- [ ] Rejeitar desmontagem de volume com arquivo aberto, operação ativa ou CWD
  apontando para ele.
- [ ] Desmontar apenas volumes não-pinned e preservar `/`, `/dev`, `/proc` e
  `/sys` conforme seus contratos.
- [ ] Confirmar que handles, CWD, caches, filas e referências ao volume sejam
  invalidados ou transferidos sem uso após liberação.
- [ ] Repetir mount/unmount, perda de dispositivo e ausência de Storage sem
  referências residuais.

### STO4 — Verificação de consistência

- [ ] Criar diagnóstico somente leitura para BPB, FAT, diretórios, cadeias,
  tamanhos, clusters livres e duplicidades.
- [ ] Detectar ciclo, cluster reservado, arquivo truncado, tamanho impossível,
  nome inválido, diretório inconsistente e setores fora do volume.
- [ ] Publicar contagem de erros, avisos e estruturas verificadas com retorno
  canônico.
- [ ] Garantir que a verificação não altere FAT, diretórios, timestamps,
  cache, processos ou hardware.
- [ ] Adicionar fixtures pequenas, grandes, vazias, corrompidas e de volume
  ausente.

### STO5 — Atualização do sistema

- [ ] Consultar manifesto remoto autenticado por HTTPS ou pelo transporte
  remoto já validado pelo projeto.
- [ ] Validar assinatura, hash, tamanho, versão, arquitetura, compatibilidade,
  dependências e política de downgrade antes de escrever no destino.
- [ ] Baixar kernel, arquivos de sistema e componentes autorizados para uma
  área de staging sem sobrescrever a versão em execução.
- [ ] Verificar espaço, integridade do Storage, energia disponível e capacidade
  de recuperação antes do commit.
- [ ] Ativar a versão nova de forma transacional, preservando bootloader e
  layout da imagem, com a versão anterior disponível para rollback.
- [ ] Recuperar interrupções por falha de rede, falta de espaço,
  reinicialização, queda de energia ou erro de escrita.
- [ ] Publicar estado, progresso, versão candidata, erro e resultado no Shell,
  Settings e diagnósticos, mantendo o prompt utilizável.
- [ ] Manter fallback para atualização local/offline quando o servidor remoto
  estiver indisponível.
- [ ] Rejeitar manifestos, imagens e componentes não assinados, truncados,
  incompatíveis ou fora da política de atualização.

### STO6 — Recuperação

- [ ] Definir como estados temporários são identificados após boot, falha ou
  cancelamento.
- [ ] Recuperar somente operações com evidência suficiente; nunca inventar
  metadados nem sobrescrever dados sem autorização explícita.
- [ ] Preservar journal/registro de atualização existente e separar sua
  recuperação da consistência FAT geral.
- [ ] Garantir rollback limpo para transações do Updater e operações normais do
  Storage.
- [ ] Expor motivo e limite da recuperação em `health` e diagnósticos.

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
