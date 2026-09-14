# Dividas tecnicas da v1.0.0

## Objetivo

Este documento e a fonte canonica das dividas tecnicas aceitas que devem ser
quitadas antes do fechamento da v1.0.0 do ZephyrOS. Ele nao substitui os
roadmaps: cada roadmap continua responsavel pelo escopo da implementacao, e
este registro concentra identidade, impacto, evidencia e criterio de quitacao.

Pendencias funcionais ainda planejadas, ideias futuras e itens fora de escopo
nao sao dividas tecnicas por si so. Um item entra aqui somente quando uma
limitacao conhecida e aceita explicitamente para permitir o encerramento de
uma etapa.

## Convencoes

- Identificador imutavel no formato `DT100-NNN`, em ordem crescente.
- Estados permitidos: `ACEITA`, `EM TRATAMENTO` e `QUITADA`.
- O roadmap proprietario deve apontar para o identificador da divida.
- A quitacao exige o criterio de saida atendido, evidencia reproduzivel e o
  registro da validacao em `registro-validacoes.md`.
- Itens nao podem ser removidos nem marcados como quitados apenas porque o
  sintoma deixou de ser observado em uma execucao isolada.
- Uma divida quitada permanece neste documento como historico, com data e
  referencia da validacao final.

## Resumo

| ID | Estado | Origem | Responsavel | Alvo |
|---|---|---|---|---|
| `DT100-001` | QUITADA | SYNC1 | Roadmap 23 / PERF2 | v1.0.0 |
| `DT100-002` | ACEITA | SYNC3 / R4 | Roadmap 23 / PERF6 | v1.0.0 |
| `DT100-003` | ACEITA | SEC4 | Roadmap 19 / SEC4 | v1.0.0 |
| `DT100-004` | ACEITA | STO6 | Roadmap 20 / STO6 | v1.0.0 |
| `DT100-005` | ACEITA | SHELL3 | Roadmap 22 / SHELL3 | v1.0.0 |

## DT100-005 - Comandos CLI de administracao de arquivos

- **Estado:** `ACEITA`.
- **Aceita em:** 2026-09-12 (America/Sao_Paulo).
- **Origem:** SHELL3 - Arquivos, VFS e administracao.
- **Responsavel:** [Roadmap 22 - Shell, interface e aplicativos](../roadmaps/22-shell-interface-e-aplicativos-v1.0.md).
- **Versao limite:** v1.0.0.

### Motivo da aceitacao

O SHELL3 foi fechado com as operacoes de criacao, renomeacao, exclusao e
copiacao acessiveis pelo Explorer Simple/Classic e pelas APIs existentes de
FS/VFS. Para evitar duplicacao de parsing, permissao, transacao e limpeza, a
interface CLI nao recebeu os comandos `mkdir`, `rm`, `mv` e `cp` nesta etapa.

### Impacto conhecido

Usuarios do terminal nao podem administrar arquivos por esses quatro comandos.
As operacoes continuam disponiveis pela interface Explorer e pelas APIs
existentes, sujeitas as mesmas permissoes, quotas, volumes e transacoes. Esta
divida nao reduz a protecao nem cria um bypass de seguranca.

### Escopo de quitacao

- Adicionar os comandos a tabela unica do dispatcher, sem duplicar a logica do
  Explorer ou das APIs FS/VFS.
- Preservar permissoes, quotas, volumes pinned/ocupados, transacoes, erros
  canonicos, cancelamento, redirecionamento e retorno ao prompt.
- Cobrir sucesso, caminhos invalidos, permissao negada, falta de espaco,
  filesystem indisponivel, cancelamento, timeout e limpeza sem residuos.
- Atualizar catalogo, documentacao, manifesto de cobertura e testes host/QEMU.

### Criterio de quitacao

Os quatro comandos devem funcionar em Simple, Classic e serial, com mutacoes
consistentes no VFS, nenhum FD, pipe, job, lock, buffer ou callback residual,
erros canonicos observaveis e validacao host/QEMU reproduzivel.

### Referencia de validacao

A aceitacao desta divida foi registrada junto ao fechamento do SHELL3 em
`registro-validacoes.md`. A implementacao futura deve registrar a quitacao no
mesmo documento antes de mudar o estado para `QUITADA`.

## DT100-004 - Matriz QEMU completa de recuperação STO6

- **Estado:** `ACEITA`.
- **Aceita em:** 2026-09-11 (America/Sao_Paulo).
- **Origem:** STO6 - Recuperação transacional.
- **Responsável:** [Roadmap 20 - STO6](../roadmaps/20-vfs-storage-e-atualizacao-v1.0.md#sto6--recuperação).
- **Versão limite:** v1.0.0.

### Motivo da aceitação

A implementação, os testes host-only, os gates de build, TST5, TST6 e a
geração da matriz `system-slots-matrix` foram concluídos. A fixture
`BOOT_ACTIVE_VALID` foi validada no QEMU, incluindo F8 e renderização do menu.
Os demais casos da matriz completa não serão executados nesta etapa para
permitir o avanço ao STO7.

### Impacto conhecido

Os estados específicos das fixtures restantes não possuem confirmação QEMU
individual nesta revisão. Isso não altera o comportamento fail-closed nem
oculta falhas: a matriz permanece disponível em `build/system-slots-matrix`
para reprodução posterior.

### Critério de quitação

Executar todas as imagens da matriz com os estados esperados, registrar os
resultados e confirmar ausência de processos QEMU residuais, sem solicitar ou
versionar chaves privadas adicionais.

### Referência de validação

O aceite e a evidência reproduzível estão registrados em
[`registro-validacoes.md`](registro-validacoes.md), no registro STO6 de
2026-09-11.

## DT100-003 - Chaves externas dos fixtures AS5/ZPKG

- **Estado:** `ACEITA`.
- **Aceita em:** 2026-09-10 (America/Sao_Paulo).
- **Origem:** SEC4 - Pacotes e confiança.
- **Responsável:** [Roadmap 19 - SEC4](../roadmaps/19-abi-seguranca-e-permissoes-v1.0.md#sec4--pacotes-e-confiança).
- **Versão limite:** v1.0.0.

### Motivo da aceitação

As chaves privadas de distribuição ZPKG e de assinatura do catálogo AS5 não
serão versionadas. As chaves públicas e os contratos de verificação estão
implementados, mas as chaves privadas externas necessárias para regenerar os
fixtures AS5 não estão disponíveis neste ciclo.

### Impacto conhecido

Os fixtures AS5 legados permanecem bloqueados pelo auditor e pelo runtime. O
`python tools/q3check.py` sem argumentos continua reprovando o gate
`confianca_as5`; o alvo `make q3check` usa a aceitação explícita de `DT100-003`,
exibe a ocorrência como `ACEITA` e mantém a proteção de produção ativa. A
matriz QEMU específica da App Store remota permanece sem execução; pacote sem
assinatura ZPKG v2 não é instalável nem executável.

### Critério de quitação

Disponibilizar externamente uma chave privada ZPKG correspondente a
`app_package_trust.h` e uma chave privada AS5 correspondente a
`config/app-store-test-public.json`; regenerar os perfis `seed` e `update`,
auditar hashes e assinaturas, obter `python tools/q3check.py` estrito em `OK`,
remover `DT100-003` de `Q3CHECK_ARGS` e executar a matriz QEMU remota
correspondente. Nenhuma chave privada deverá entrar no repositório.

### Referência de validação

O aceite e o bloqueio reproduzível estão registrados em
[`registro-validacoes.md`](registro-validacoes.md), no registro SEC4 de
2026-09-10.

## DT100-001 - RegCheck full e entrada PS/2

- **Estado:** `QUITADA`.
- **Aceita em:** 2026-08-27 00:19 (America/Sao_Paulo).
- **Quitada em:** 2026-09-13 14:30 (America/Sao_Paulo).
- **Origem:** SYNC1 - Top-Half e Bottom-Half de interrupcoes.
- **Responsavel:** [Roadmap 23 - PERF2](../roadmaps/23-desempenho-e-dividas-v1.0.md#perf2--entrada-e-responsividade).
- **Versao limite:** v1.0.0.

### Motivo da aceitacao

A infraestrutura da SYNC1 e seus diagnosticos foram validados e terminam em
`OK`. A otimizacao do caminho cooperativo de `regcheck full` foi separada da
entrega de concorrencia para ser executada com medicao sistemica na K5, sem
reabrir a SYNC1 nem antecipar a `kworker` da SYNC3.

### Impacto conhecido

O risco historico era a saturacao do pipeline PS/2 durante `regcheck full`, com
perda de pacotes e atraso no retorno ao Shell. A PERF2 ajustou somente o
processamento cooperativo, budgets e coalescencia valida; o cenario reproduzivel
agora preserva as transicoes e retorna ao prompt sem descarte.

### Evidencia de referencia

Na matriz PERF2 de 2026-09-13, o relatorio
`build/test-results/perf2-responsiveness/perf2-responsiveness.json` passou as
9/9 sessoes fixas (`baseline/Simple`, `baseline/Classic` e
`no-vesa/Simple fallback`, tres iteracoes por faixa). Todas tiveram tres
amostras guest validas; `input_key_dropped`, `input_pointer_dropped`,
`keyboard_raw_dropped`, `mouse_raw_dropped`, `mouse_packets_dropped`,
`mouse_queue_rejected`, `deferred_irq_1_rejected` e
`deferred_irq_12_rejected` terminaram em zero. Press/release/roda coincidiram
com os eventos enviados no `input.log`, `mouse_button_state` e
`mouse_raw_button_state` terminaram em zero, e `mouse_wheel_supported` ficou
disponivel em todas as sessoes. O processo QEMU foi amostrado no host e cada
sessao preservou serial, QMP, entrada e manifesto.

### Observacao apos a SYNC3

A matriz final da SYNC3 foi substituida pela evidencia automatizada da PERF2
para este risco. A validacao inclui contadores antes/depois, carga QMP real,
cancelamento por F11, retorno ao prompt e o fallback `no-vesa`; recorrencias
fora desses limites continuam sendo novas evidencias de desempenho, sem
reabrir esta divida quitada sem reproducao do mesmo protocolo.

### Escopo da quitacao

- Medir separadamente as fases de `regcheck full` e os ciclos do processo
  System.
- Correlacionar IRQ1/IRQ12, filas brutas e normalizadas, `input core` e
  execucoes diferidas no mesmo cenario reproduzivel.
- Ajustar orcamentos, pontos de yield e coalescencia sem ocultar descarte nem
  fundir teclas, roda ou transicoes de botoes.
- Preservar Top-Halves minimos e callbacks diferidos somente na `Zephyr
  kworker`, com interrupcoes habilitadas; System permanece apenas como
  fallback controlado.

### Criterio de quitacao

No mesmo ciclo de validacao, `irqstat check` e `regcheck full` devem terminar
em `OK`; movimento, clique, arraste e roda intensos devem produzir incremento
zero de pacotes descartados e nenhum novo `ERR_OVERFLOW`; teclas, roda e
transicoes de botoes devem ser preservadas; nao pode haver rejeicao diferida
permanente; e as interfaces Classic e Shell devem permanecer responsivas.

### Procedimento de validacao

Executar no QEMU, registrando os contadores antes e depois do estresse:

```text
irqstat check
irqstat list
mouse
regcheck full
irqstat list
mouse
```

Durante `regcheck full`, mover o ponteiro, clicar, arrastar e usar a roda. A
validacao final e seu horario estao registrados em
`registro-validacoes.md`, na entrada PERF2 de 2026-09-13.

## DT100-002 - kworker como processo ring0

- **Estado:** `ACEITA`.
- **Aceita em:** 2026-08-27 17:09 (America/Sao_Paulo).
- **Origem:** SYNC3 / R4 - Kernel Workqueues.
- **Responsavel:** [Roadmap 23 - PERF6](../roadmaps/23-desempenho-e-dividas-v1.0.md#perf6--quita%C3%A7%C3%A3o-e-release).
- **Versao limite:** v1.0.0.

### Motivo da aceitacao

O scheduler produtivo do ZephyrOS gerencia processos ring0 e ring3, enquanto
o scheduler de `thread_t` ainda e isolado e usado apenas por diagnosticos.
Integrar os dois schedulers ampliaria a SYNC3 para uma troca do modelo de
execucao. O usuario aceitou concluir a workqueue com uma `Zephyr kworker`
ring0 e transferir essa unificacao para a K5.

### Atualizacao da PERF6

A implementacao da PERF6 foi preparada para substituir o processo dedicado por
uma `thread_t` kernel com identidade geracional `(tid, generation)`. O
supervisor agora distingue alvos `PROCESS` e `THREAD`; somente a kworker usa
`THREAD`, enquanto System, Shell e Desktop continuam processos. A workqueue
preserva as APIs legadas e aceita binding por thread, com fallback explicito e
estado `DEGRADED` quando a criacao nao for possivel.

Esta atualizacao ainda nao altera o estado de aceite. A evidencia necessaria e
o relatorio `build/test-results/perf6-kworker-release/perf6-kworker-release.json`
com 9/9 sessoes `PASS`, identidade thread valida, nenhum processo kworker,
filas drenadas, reboot/fallback verificados e release `0.1.0` auditado. Ate
essa execucao, `DT100-002` permanece `ACEITA`.

### Impacto conhecido

Antes da PERF6, a `kworker` consumia um slot da tabela de processos e uma
stack nativa de 16 KiB. A implementacao em validacao usa a stack de
`thread_t`, nao publica processo kworker e mantem a espera por Wait Queue,
sem busy-wait e sem callbacks na IRQ.

### Evidencia de referencia

`workq status` publica o PID e o contexto `KWORKER`; `procs` identifica
`Zephyr kworker` como processo ring0. O contrato `thread_t` permanece sem uso
produtivo fora do seu scheduler e autoteste isolados.

### Escopo da quitacao

- Unificar o scheduler nativo de processos e threads ou tornar `thread_t` um
  participante real do scheduler principal.
- Migrar a `Zephyr kworker` sem alterar as APIs `work_struct_t`, prioridades,
  prazos, cancelamento e snapshots.
- Preservar a Wait Queue `KWORKER`, os fallbacks System/kernel e callbacks com
  interrupcoes habilitadas.

### Criterio de quitacao

`workq status` deve identificar contexto de thread real, sem consumir slot da
tabela de processos; `workq check`, `wait check`, `schedcheck` e
`regcheck full` devem terminar em `OK`; Bottom-Halves, timers, rede e indice
devem continuar funcionais; nenhuma entrada orfa ou rejeicao permanente pode
ser criada; Classic e Shell devem permanecer responsivos.

### Procedimento de validacao

No mesmo ciclo QEMU, executar:

```text
procs
threads
workq status
workq list
workq check
wait check
schedcheck
regcheck full
health check
```

A quitacao e seu horario exato devem permanecer registrados em
`registro-validacoes.md`.

## Auditoria das demais etapas

Auditoria documental concluida em: 2026-08-27 00:28 (America/Sao_Paulo).

Nenhum novo identificador foi criado. As ocorrencias abaixo foram encontradas,
mas nao atendem ao criterio de divida tecnica aceita para a v1.0.0:

| Fonte | Ocorrencia | Classificacao |
|---|---|---|
| [Roadmap 03](../roadmaps/03-kernel-e-desempenho.md) | CPU real por RDTSC/PMU e memoria por processo ainda sem fonte de medicao confiavel | Instrumentacao futura, sem aceite ou prazo da v1.0.0 |
| [Roadmap 05](../roadmaps/05-sistema-e-ecossistema.md) e [`ROADMAP.md`](../../ROADMAP.md) | Coberturas complementares de ACPI, rede, RTL8139, peer externo e Simple/Classic | Backlog de validacao explicitamente nao bloqueante |
| [Roadmap 07](../roadmaps/07-modernizacao-visual.md) / [`metricas.md`](metricas.md) | Comparacao numerica MV4 em `N/D` por falta da linha-base AS3 | Excecao documental ja declarada, sem reivindicacao de ganho |
| [Roadmap 08](../roadmaps/08-evolucao-da-plataforma.md) | Gates sem horario da EP7.0 e registros de smoke/gates da EP6.4 pendentes | Lacuna de evidencia; requer validacao ou aceite proprio |
| [Roadmap 08](../roadmaps/08-evolucao-da-plataforma.md) | EP7.1 e EP8 adiadas por dependencia de hardware real | Decisao de escopo e dependencia externa, nao divida aceita |
| [Roadmaps 09](../roadmaps/09-funcionalidades-aplicaveis.md), [10](../roadmaps/10-vfs-e-abstracao-io.md), [11](../roadmaps/11-gerenciamento-avancado-de-memoria.md), [12](../roadmaps/12-concorrencia-e-sincronizacao.md) e [13](../roadmaps/13-armazenamento-e-buffer-cache.md) | R5-R9, VFS, MM e BLK1-BLK4 ainda nao iniciados | Trabalho planejado ou etapa ainda aberta, nao divida tecnica |

Esses itens permanecem nos seus roadmaps de origem. Caso algum seja aceito
como limitacao a ser quitada antes da v1.0.0, ele deve receber o proximo ID
`DT100-NNN`, criterio reproduzivel, responsavel e registro de aceitacao antes
de entrar neste resumo.

## Dividas quitadas

Nenhuma ate o momento.

## Referencias

- [Roadmap 03 - Kernel e desempenho](../roadmaps/03-kernel-e-desempenho.md)
- [Roadmap 12 - Concorrencia e sincronizacao](../roadmaps/12-concorrencia-e-sincronizacao.md)
- [Responsividade do sistema](../melhorias%20futuras/responsividade%20do%20sistema.md)
- [Metricas de otimizacao](metricas.md)
- [Registro de validacoes](registro-validacoes.md)

tem um problema do shell, algumas execuções nao me retornam o zephyros>, ele só fica em branco
