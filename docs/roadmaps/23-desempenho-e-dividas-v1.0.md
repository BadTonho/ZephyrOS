# Roadmap 23 — Desempenho, validação e dívidas da 1.0.0

## Estado

Em andamento. A PERF1 foi implementada e validada operacionalmente. Esta frente mede e corrige gargalos conhecidos sem trocar o
scheduler, o modelo de memória, a ABI ou o boot por suposição de desempenho.
Cada otimização precisa de uma linha de base, um ganho observável e uma
regressão controlada.

## Objetivo

Fechar as dívidas técnicas aceitas para a versão 1.0.0, preservar a
responsividade do Shell e das interfaces, reduzir trabalho ativo em idle e
manter previsíveis boot, entrada, scheduler, VFS, rede e vídeo.

## Escopo

- `DT100-001`: saturação de entrada PS/2 durante `regcheck full`;
- `DT100-002`: `kworker` como processo ring0 e sua integração com o modelo de
  execução;
- residência Idle, `active_ticks`, `idle_ticks` e custo real do ciclo System;
- latência de Shell, jobs, foco, IPC, VFS, rede e renderização VESA;
- heap, PMM, SLAB/SLUB, cache, filas e tamanho da imagem;
- medição separada de CPU lógica, tempo de PIT, uso do host e custo de
  apresentação.

RDTSC/PMU só será usado quando a fonte for validada. Percentuais de ticks não
  serão apresentados como consumo elétrico ou utilização física da CPU.

## Dependências

- [Roadmap 18](18-kernel-processos-e-userland-v1.0.md) para a linha de base;
- [Roadmap 19](19-abi-seguranca-e-permissoes-v1.0.md) para não otimizar removendo
  validações de segurança;
- [Roadmap 20](20-vfs-storage-e-atualizacao-v1.0.md) para os limites de
  I/O e cache;
- [Roadmap 21](21-hardware-rede-e-energia-v1.0.md) para os perfis
  comparáveis;
- [Roadmap 22](22-shell-interface-e-aplicativos-v1.0.md) para a experiência
  observável e a liveness dos aplicativos;
- Roadmaps 03, 09, 11, 12 e 14;
- [Dívidas técnicas da v1.0.0](../qualidade/dividas-tecnicas-v1.0.0.md).

## Regra transversal de validação

Cada roadmap deve validar sucesso, erro, timeout, cancelamento, repetição,
ausência de recurso e limpeza antes de entregar sua implementação ao Roadmap
23. Esta frente consolida as medições e otimizações, mas não posterga os gates
de segurança, memória, processos, VFS, hardware ou Shell para a release.

## Fases

### PERF1 — Instrumentação sem mudança de comportamento

- [x] Definir pontos de medição para boot, IRQ, scheduler, troca de contexto,
  System, Shell, jobs, VFS, rede, cache e desenho.
- [x] Registrar contadores, unidade, resolução, overflow, custo da própria
  medição e contexto de execução.
- [x] Separar ticks do PIT, ciclos RDTSC/PMU, bytes copiados, tempo de host e
  utilização da VM.
- [x] Publicar linha-base de memória, tamanho da imagem, latência e filas sem
  alterar o fluxo produtivo.
- [x] Incluir no baseline o supervisor de serviços, limites por processo,
  permissões, atualização A/B e recuperação de boot quando aplicável.
- [x] Garantir que diagnósticos não gerem logging por tick ou perturbem o
  cenário medido de forma significativa.

Implementação entregue: `kmetrics machine` em `ZMETRIC/1`, coleta estruturada
em módulo interno, parser/agregador host, relatório guest + processo QEMU,
caso `qemu:tst5:perf1-baseline`, testes determinísticos, catálogo e alvos
Windows/Linux. `kmetrics` e `kmetrics reset` mantêm o contrato anterior;
RDTSC/PMU publica `ND` e o `src/boot/boot.asm` permanece inalterado.

Validação operacional concluída em 2026-09-13: `make q3check`, `make clean &&
make`, `make test-perf1-host` e `make test-perf1-qemu` passaram. A matriz
Simple/Classic com três iterações por modo passou 6/6 sessões, com envelopes
`ZMETRIC/1` completos, amostras guest e coleta do processo QEMU. O relatório
está em `build/test-results/perf1-baseline/perf1-baseline.json`. A PERF1 não
quita nenhuma dívida técnica; ela apenas produz a evidência necessária para
PERF2/PERF3.

### PERF2 — Entrada e responsividade

- [x] Reproduzir `DT100-001` com contadores antes/depois e carga de teclado,
  mouse, roda, clique e arraste durante `regcheck full`.
- [x] Ajustar somente orçamento, coalescência, pontos de yield e processamento
  diferido que preservem todos os eventos relevantes.
- [x] Confirmar que não haja overflow PS/2, rejeição permanente ou perda de
  transições no cenário de saída.
- [x] Validar prompt, foco, cancelamento, jobs e retorno de cenas sob carga.
- [x] Repetir Simple, Classic e fallback de vídeo.

Implementacao registrada em 2026-09-13: `kmetrics machine` recebeu o fluxo
detalhado de entrada e os estados do mouse; os drivers publicam getters
append-only para filas brutas, coalescencia, rejeicoes, pacotes e eventos;
`tools/qemu_test_runner.py` passou a aceitar operacoes QMP declarativas de
movimento, roda, botoes, arraste e carga temporizada. O caso
`qemu:tst5:perf2-input-responsiveness`, o relatorio versionado, testes host e
os alvos Windows/Linux foram adicionados. A validacao essencial passou em
2026-09-13: `make q3check`, `make clean`, `make`, `make test-perf2-host` e
`make test-perf2-qemu`. A matriz fixa passou 9/9 sessoes, com tres amostras
guest validas por sessao, zero descartes/rejeicoes de entrada e sem rejeicao
deferred nas IRQ1/IRQ12. Press/release/roda foram preservados conforme o
`input.log`, os botoes terminaram soltos e o observer confirmou o retorno ao
prompt. O relatorio esta em
`build/test-results/perf2-responsiveness/perf2-responsiveness.json`.
DT100-001 foi marcada `QUITADA`; nenhuma outra divida tecnica foi alterada.

### PERF3 — Scheduler, Idle e kworker

- [x] Medir residência do PID 0, `active_ticks`, `idle_ticks`, wakeups e
  latência de serviços.
- [x] Confirmar que `sti; hlt` não tenha janela de corrida nem busy-wait e que
  System/Desktop bloqueiem quando não houver trabalho.
- [x] Decidir, com métricas, a integração de `thread_t` e `kworker` para quitar
  `DT100-002`, mantendo rollback para a implementação atual e sem transformar
  um segundo scheduler em requisito automático da 1.0.0.
- [x] Registrar decisão explícita caso o modelo atual de kworker seja mantido
  como suficiente para a versão, com impacto e limite documentados.
- [x] Não alterar quantum, prioridade, ABI ou identidade do PID 0 sem um
  contrato próprio e validação completa.
- [x] Confirmar que entrada, timer, rede e workqueue acordem consumidores sem
  perda ou polling excessivo.

Implementacao PERF3 entregue: `scheduler_runtime_stats_t` e
`scheduler_get_runtime_stats()` preservam `scheduler_get_stats()`; o Idle
mantem `sti; hlt`; a workqueue publica latencia de despacho; `kmetrics machine`,
o runner QMP, o caso `qemu:tst5:perf3-scheduler-idle`, testes host/Python,
catalogo, cobertura e alvos Windows/Linux foram atualizados. A `thread_t`
permanece isolada e `DT100-002` permanece `ACEITA`.

Validacao concluida em 2026-09-14: `make q3check`, `make clean`, `make`,
`make test-perf3-host` (32 testes) e `make test-perf3-qemu` passaram. A matriz
QEMU fixa foi aprovada em 9/9 sessoes (`baseline/Simple`, `baseline/Classic` e
`no-vesa/Simple fallback`, tres iteracoes cada), com envelopes completos,
metricas guest obrigatorias disponiveis, contabilidade Idle consistente,
fila READY/RUNNING final vazia, kworker vinculada e prompt restaurado. O
relatorio esta em
`build/test-results/perf3-scheduler-idle/perf3-scheduler-idle.json`.

### PERF4 — Memória, VFS e rede

- [x] Medir alocações, picos, fragmentação, caches, filas, cópias e tempo de
  resposta nos cenários representativos.
- [x] Reduzir cópias e contenções somente quando ownership e invariantes
  permanecerem explícitos.
- [x] Comparar SLAB/SLUB, buffer cache, pipes, sockets e snapshots sem usar
  ponteiros emprestados depois do ciclo de vida do objeto.
- [x] Validar que otimizações não introduzam alocação ou bloqueio em IRQ/hot
  path sem justificativa documentada.
- [x] Repetir `memcheck`, `schedcheck`, `proccheck`, VFS, rede e regressão.

Implementacao PERF4 entregue: `kmetrics machine` passou a publicar os
getters existentes de memoria detalhada, SLAB, VFS, fila/cache de bloco,
durabilidade, buffers, `sk_buff`, sockets e rotas. O caso
`qemu:tst5:perf4-memory-storage-network`, o relatorio versionado, os testes
host/Python, o catalogo, a cobertura e os alvos Windows/Linux foram
atualizados. Nenhum bootloader, ABI, syscall, formato, capacidade de fila ou
caminho de IRQ foi alterado; nenhuma divida tecnica foi quitada.

Validacao concluida em 2026-09-14: `make q3check`, `make clean`, `make`,
`make catalog-test` e `make test-perf4-host` passaram. A matriz QEMU fixa foi
aprovada em 9/9 sessoes (`baseline/Simple`, `baseline/Classic` e
`no-vesa/Simple fallback`, tres iteracoes cada), com envelopes completos,
metricas guest obrigatorias disponiveis, zonas de memoria coerentes, filas
drenadas, ausencia de crescimento residual e prompt restaurado. O host foi
amostrado a cada 250 ms. O relatorio esta em
`build/test-results/perf4-memory-storage-network/perf4-memory-storage-network.json`,
schema `zephyros-perf4-memory-storage-network-v1`, imagem SHA-256
`19052f96819368e6180bd1b295281be6b1c961f7c0fe1ecc5ea17f4b8e3b7073`.

A comparacao nao encontrou ganho reproduzivel que justificasse uma alteracao
funcional de copias, contencoes ou capacidade; por isso a linha de base foi
preservada e o suporte SLUB inexistente permaneceu explicitamente fora do
escopo.

### PERF5 — Vídeo e interfaces

- [x] Medir regiões VESA, cursor, backbuffer, taskbar, relógio, WM e janelas
  Classic.
- [x] Preservar fallback VGA/Simple e evitar repaint completo quando uma região
  menor for suficiente.
- [x] Medir latência de entrada e custo de apresentação em vez de inferir
  desempenho apenas pelo tamanho do código.
- [x] Confirmar que Task Manager, Explorer, Settings e Desktop permaneçam
  responsivos durante jobs e diagnósticos.
- [x] Registrar ganhos e regressões por perfil de hardware.

Implementação PERF5 entregue: `kmetrics machine` agora agrega as métricas
append-only de VESA, vídeo, cursor, Taskbar, Desktop e Window Manager. O
runner recebeu fases declarativas, screenshots, operações QMP, amostras do
processo QEMU e o caso `qemu:tst5:perf5-video-ui`, com nove sessões executadas
em `baseline/Simple`, `baseline/Classic` e `no-vesa/Simple fallback`. O
catálogo, manifesto de cobertura, testes host/Python, Makefiles e documentos
operacionais foram atualizados. Nenhuma otimização A/B foi mantida sem ganho
reprodutível e `src/boot/boot.asm`, ABI, syscalls, scheduler e IRQs ficaram
inalterados.

Validação da PERF5 concluída com `make q3check`, `make clean && make`,
`make catalog-test`, `make test-perf5-host` e matriz QEMU 9/9 `PASS`. O relatório
`build/test-results/perf5-video-ui/perf5-video-ui.json` registrou as três
iterações de `baseline/Simple`, `baseline/Classic` e `no-vesa/Simple fallback`,
com imagem de 268435456 bytes e SHA-256
`12dd01cdbf9dd046b69c50c7249d61b784cfc15ea83afb9eeefe6af7f85eb069`. Nenhuma
otimização A/B foi solicitada ou mantida; a etapa entregou instrumentação e
evidência. Nenhuma dívida técnica será quitada nesta fase.

### PERF6 — Quitação e release

- [ ] Atualizar `DT100-001` e `DT100-002` somente com evidência reproduzível.
- [ ] Registrar tamanho, boot, memória, latência e uso do host antes/depois.
- [ ] Confirmar que o diagnóstico continue correto depois de ciclos de pressão,
  cancelamento, reboot e ausência de hardware.
- [ ] Publicar decisões negativas quando uma otimização não trouxer benefício
  suficiente.
- [ ] Entregar à RLS5 apenas alterações com rollback e documentação completa.

## Critérios de saída

- As dívidas técnicas estão quitadas ou possuem aceite explícito renovado
  com impacto e prazo, sem serem escondidas no percentual da versão.
- Todos os Roadmaps 18–22 possuem evidência própria de sucesso, falha,
  repetição e limpeza; o Roadmap 23 não substitui esses gates.
- A entrada intensa não produz perdas ou overflow no cenário definido.
- O Idle reduz trabalho ativo mensurável sem comprometer wakeups ou
  responsividade.
- O Shell não perde prompt, foco, cancelamento ou eventos sob carga.
- Nenhuma otimização viola ownership, validação, ABI, logs, memória ou
  compatibilidade de hardware.

## Fora do escopo

Não haverá SMP, PMU obrigatório, novo scheduler, reescrita ampla em Rust,
mudança de ABI, renderizador novo ou otimização baseada apenas em percepção.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário deve coletar as linhas
de base e repetir a matriz em condições comparáveis, registrando métricas,
comandos, perfil da VM e horário real antes de alterar o estado das dívidas.
