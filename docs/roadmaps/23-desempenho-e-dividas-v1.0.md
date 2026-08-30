# Roadmap 23 — Desempenho, validação e dívidas da 1.0.0

## Estado

Planejado. Esta frente mede e corrige gargalos conhecidos sem trocar o
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

- [ ] Definir pontos de medição para boot, IRQ, scheduler, troca de contexto,
  System, Shell, jobs, VFS, rede, cache e desenho.
- [ ] Registrar contadores, unidade, resolução, overflow, custo da própria
  medição e contexto de execução.
- [ ] Separar ticks do PIT, ciclos RDTSC/PMU, bytes copiados, tempo de host e
  utilização da VM.
- [ ] Publicar linha-base de memória, tamanho da imagem, latência e filas sem
  alterar o fluxo produtivo.
- [ ] Incluir no baseline o supervisor de serviços, limites por processo,
  permissões, atualização A/B e recuperação de boot quando aplicável.
- [ ] Garantir que diagnósticos não gerem logging por tick ou perturbem o
  cenário medido de forma significativa.

### PERF2 — Entrada e responsividade

- [ ] Reproduzir `DT100-001` com contadores antes/depois e carga de teclado,
  mouse, roda, clique e arraste durante `regcheck full`.
- [ ] Ajustar somente orçamento, coalescência, pontos de yield e processamento
  diferido que preservem todos os eventos relevantes.
- [ ] Confirmar que não haja overflow PS/2, rejeição permanente ou perda de
  transições no cenário de saída.
- [ ] Validar prompt, foco, cancelamento, jobs e retorno de cenas sob carga.
- [ ] Repetir Simple, Classic e fallback de vídeo.

### PERF3 — Scheduler, Idle e kworker

- [ ] Medir residência do PID 0, `active_ticks`, `idle_ticks`, wakeups e
  latência de serviços.
- [ ] Confirmar que `sti; hlt` não tenha janela de corrida nem busy-wait e que
  System/Desktop bloqueiem quando não houver trabalho.
- [ ] Decidir, com métricas, a integração de `thread_t` e `kworker` para quitar
  `DT100-002`, mantendo rollback para a implementação atual e sem transformar
  um segundo scheduler em requisito automático da 1.0.0.
- [ ] Registrar decisão explícita caso o modelo atual de kworker seja mantido
  como suficiente para a versão, com impacto e limite documentados.
- [ ] Não alterar quantum, prioridade, ABI ou identidade do PID 0 sem um
  contrato próprio e validação completa.
- [ ] Confirmar que entrada, timer, rede e workqueue acordem consumidores sem
  perda ou polling excessivo.

### PERF4 — Memória, VFS e rede

- [ ] Medir alocações, picos, fragmentação, caches, filas, cópias e tempo de
  resposta nos cenários representativos.
- [ ] Reduzir cópias e contenções somente quando ownership e invariantes
  permanecerem explícitos.
- [ ] Comparar SLAB/SLUB, buffer cache, pipes, sockets e snapshots sem usar
  ponteiros emprestados depois do ciclo de vida do objeto.
- [ ] Validar que otimizações não introduzam alocação ou bloqueio em IRQ/hot
  path sem justificativa documentada.
- [ ] Repetir `memcheck`, `schedcheck`, `proccheck`, VFS, rede e regressão.

### PERF5 — Vídeo e interfaces

- [ ] Medir regiões VESA, cursor, backbuffer, taskbar, relógio, WM e janelas
  Classic.
- [ ] Preservar fallback VGA/Simple e evitar repaint completo quando uma região
  menor for suficiente.
- [ ] Medir latência de entrada e custo de apresentação em vez de inferir
  desempenho apenas pelo tamanho do código.
- [ ] Confirmar que Task Manager, Explorer, Settings e Desktop permaneçam
  responsivos durante jobs e diagnósticos.
- [ ] Registrar ganhos e regressões por perfil de hardware.

### PERF6 — Quitação e release

- [ ] Atualizar `DT100-001` e `DT100-002` somente com evidência reproduzível.
- [ ] Registrar tamanho, boot, memória, latência e uso do host antes/depois.
- [ ] Confirmar que o diagnóstico continue correto depois de ciclos de pressão,
  cancelamento, reboot e ausência de hardware.
- [ ] Publicar decisões negativas quando uma otimização não trouxer benefício
  suficiente.
- [ ] Entregar à RLS5 apenas alterações com rollback e documentação completa.

## Critérios de saída

- As duas dívidas técnicas estão quitadas ou possuem aceite explícito renovado
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
