# Roadmap 24 — Release e aceitação da versão 1.0.0

## Estado

Em execução. A RLS1 está em `PASS` no commit `d22e07da`, com auditoria
host-only e artefatos de baseline reproduzidos sobre a árvore limpa. A RLS2
está em `PASS` com 9/9 sessões QEMU aprovadas; RLS3 também está em `PASS` com
9/9 sessões QEMU aprovadas; RLS5 continua pendente.
Esta frente prepara uma linha de base reproduzível e suportada para a versão
1.0.0. Ela não adiciona uma nova API, syscall, formato binário ou driver;
organiza a correção das falhas que impediriam declarar o sistema estável.

## Objetivo

Entregar uma versão que possa ser construída, inicializada, usada e
diagnosticada de forma determinística nos perfis suportados, preservando os
fallbacks Simple/Classic, o boot atual e a compatibilidade dos aplicativos.

## Escopo

- build reprodutível, imagem coerente e artefatos de release identificáveis;
- ciclo de vida completo de comandos, jobs, cenas, processos e descritores;
- prompt do Shell sempre restaurado após sucesso, erro, cancelamento ou
  término de operação cooperativa;
- investigação do caso em que uma execução termina sem devolver `zephyr>` e
  deixa a tela aparentemente vazia;
- limpeza de buffers, referências, filas, locks e recursos temporários;
- regressão nos modos Simple e Classic e nos perfis de hardware ausente;
- documentação final de suporte, limitações, recuperação e validação.

Não fazem parte desta frente novos recursos de Bluetooth, Rust, ACPI S1-S4,
antivírus ou carregamento dinâmico de módulos.

## Dependências

- Roadmaps 01–16 concluídos e Roadmaps 18–23 encerrados como base da
  aceitação;
- [Roadmap 18 — Kernel, processos e userland](18-kernel-processos-e-userland-v1.0.md),
  [19 — ABI, segurança e permissões](19-abi-seguranca-e-permissoes-v1.0.md),
  [20 — VFS, Storage e atualização](20-vfs-storage-e-atualizacao-v1.0.md),
  [21 — Hardware, rede e energia](21-hardware-rede-e-energia-v1.0.md),
  [22 — Shell, interface e aplicativos](22-shell-interface-e-aplicativos-v1.0.md)
  e [23 — Desempenho e validação](23-desempenho-e-dividas-v1.0.md);
- [Dívidas técnicas da v1.0.0](../qualidade/dividas-tecnicas-v1.0.0.md).

## Fases

### RLS1 — Linha de base e artefatos

- [x] Fixar a identificação documental da versão 1.0.0, mantendo o build em
  0.1.0, registrando a origem dos fontes e o conjunto de ferramentas aceito.
- [x] Criar o auditor `tools/release_baseline.py` com o schema
  `zephyros-rls1-baseline-v1` e os estados `PASS`, `FAIL` e `BLOCKED`.
- [x] Registrar tamanho, checksum, layout de LBAs, símbolos e seções da
  imagem gerada.
- [x] Confirmar que `boot.bin` continua com 512 bytes e que kernel, recovery
  loader e FAT32 não se sobrepõem.
- [x] Reproduzir o build limpo em uma configuração documentada, sem caminhos
  pessoais ou ferramentas implícitas.
- [x] Definir os artefatos que podem ser distribuídos e os que são apenas
  intermediários de validação.
- [x] Fixar a matriz de compatibilidade entre kernel, recovery, bootloader,
  filesystem, pacotes e slots A/B da atualização.

O relatório produzido pela RLS1 ficará em
`build/test-results/rls1-baseline/rls1-baseline.json`, distinguindo
`build_version=0.1.0`, `target_version=1.0.0` e
`candidate_state=DOCUMENTAL_ONLY`. A execução final deve ocorrer com o
worktree limpo e não cria tag, assinatura ou publicação; a aceitação da RLS2
foi registrada como `PASS`; RLS3 também foi aprovada, e RLS4 foi aprovada
com a matriz suportada reproduzida; RLS5 continua pendente.

### RLS2 — Liveness do Shell e dos jobs

- [x] Mapear cada comando que cria job, abre cena, bloqueia entrada ou altera o
  foco.
- [x] Garantir retorno único ao prompt após sucesso, erro, cancelamento,
  timeout, processo encerrado e recurso indisponível.
- [x] Reproduzir o caso de tela vazia sem prompt e registrar em qual camada a
  execução terminou: dispatcher, job, cena, foco, vídeo ou entrada.
- [x] Validar que erros não deixam o Shell esperando um callback, descritor,
  processo ou evento que já não existe.
- [x] Validar reentrada, `F12`, `Ctrl+C`, fechamento de cenas e comandos
  inválidos sem prompt duplicado ou prompt ausente.

A implementacao RLS2 adiciona o snapshot interno de liveness ao `kmetrics
machine`, finalizacao idempotente por geracao, identificacao das camadas do
Shell e o caso `qemu:tst5:rls2-shell-liveness`. Os gates `q3check`, build
limpo, catalogo e host passaram, e a matriz QEMU fixa terminou com 9/9
sessoes `PASS` no relatorio
`build/test-results/rls2-shell-liveness/rls2-shell-liveness.json`, schema
`zephyros-rls2-shell-liveness-v1`. RLS3 e RLS4 também foram validadas; RLS5 é
a próxima etapa.

### RLS3 — Limpeza e invariantes

A instrumentacao RLS3 foi integrada ao snapshot `kmetrics machine` por meio
dos campos agregados `invariant_*`, ao caso `qemu:tst5:rls3-invariants` e ao
auditor `tools/rls3_invariants.py`. Os gates de qualidade, build limpo,
catalogo, host e a matriz QEMU terminaram com 9/9 sessoes `PASS` no
relatorio `build/test-results/rls3-invariants/rls3-invariants.json`. A
repeticao usa intervalo declarativo de 4 s entre capturas machine, uma espera
adicional de 3 s antes da captura final e 10 s antes do marcador final para
drenar a saida serial/VESA; nao houve alteracao funcional no produto. A lane
`no-vesa/Classic` continua nao aplicavel, e DT100-003, DT100-004 e DT100-005
permanecem `ACEITA`.

- [x] Auditar ownership de buffers, snapshots, descritores, jobs, filas e
  referências de processo.
- [x] Garantir que todos os caminhos de erro liberem ou transfiram seus
  recursos exatamente uma vez.
- [x] Confirmar que locks e interrupções são restaurados em todos os retornos.
- [x] Integrar falhas relevantes ao log da camada que possui o contexto, sem
  produzir logging pesado em IRQ ou hot path.
- [x] Confirmar que credenciais, permissões, limites e estados do supervisor
  sejam publicados de forma reproduzível nos diagnósticos.
- [x] Confirmar que atualização interrompida, boot não confirmado e rollback
  não deixem a única imagem inicializável inutilizada.
- [x] Manter `health`, `regcheck`, `memcheck`, `schedcheck`, `proccheck` e os
  diagnósticos de rede/ACPI coerentes depois de ciclos repetidos.

### RLS4 — Regressão da matriz suportada

- [x] Executar a matriz Simple/Classic com ACPI, sem ACPI, com NIC, sem NIC,
  com USB HID, sem USB, com Storage e sem volumes adicionais.
- [x] Repetir abertura e fechamento de Shell, Explorer, Settings, Task Manager,
  Desktop, WM e Updater.
- [x] Testar comandos que terminam normalmente, falham antes do commit,
  cancelam e deixam recursos ocupados.
- [x] Testar atualização online e offline, falta de rede, falta de espaço,
  falha de escrita, queda durante staging e rollback após boot não saudável.
- [x] Confirmar fallback de vídeo, teclado, rede, áudio e armazenamento sem
  travamento ou tela sem prompt.
- [x] Registrar diferenças entre cobertura validada e cobertura complementar.

A implementacao da auditoria RLS4 foi adicionada em
`tools/rls4_supported_matrix.py`, sem novo marcador no guest e sem alteracao
persistente da imagem. O agregador prepara as 57 sessoes primarias (dez
perfis, Simple/Classic, tres iteracoes, com `no-vesa/Classic` como
`NOT_APPLICABLE`) e 30 execucoes complementares de update/recovery e falhas em
fixtures isoladas. O schema e
`zephyros-rls4-supported-matrix-v1`, com relatorio em
`build/test-results/rls4-supported-matrix/rls4-supported-matrix.json`.

Os testes host, o catalogo, os gates `q3check`/build e a integracao dos
Makefiles passaram. A matriz QEMU final foi executada com quatro workers no
run `run-20260915T235713Z-984900`, sobre a imagem de 268435456 bytes com SHA-256
`611de8f50b45a021dd7040066ed9e98875fc30498c14d5c71e029ee02310ed60`, e
terminou `PASS`: 57/57 sessoes primarias e 30/30 execucoes complementares
foram aprovadas. A correcao ficou restrita ao testador: perfis USB interativos
usam fallback PS/2 explicito, a fixture de falta de espaco nao combina
`readonly` com snapshot, e os limites de boot, pacing de entrada e fechamento
do updater Simple foram ajustados. Nao houve alteracao de ABI, syscalls,
scheduler, bootloader ou comportamento produtivo. RLS5 permanece como a
proxima etapa; nenhuma divida tecnica e quitada nesta fase.

### RLS5 — Candidata de release

- [ ] Congelar a lista de mudanças permitidas após o início da validação final.
- [ ] Atualizar documentação, contratos, índice, roadmap geral e registro de
  validações com evidência e horário real.
- [ ] Definir procedimento de rollback para a última imagem aprovada.
- [ ] Definir o marcador de versão saudável, o limite de tentativas de boot e
  o procedimento de recuperação da candidata.
- [ ] Publicar a matriz de suporte e as limitações aceitas da 1.0.0.

A implementacao da auditoria RLS5 foi adicionada em
`tools/rls5_release_candidate.py`, com o schema
`zephyros-rls5-release-candidate-v1` e o relatorio em
`build/test-results/rls5-release/rls5-release.json`. O auditor preserva
`build_version=0.1.0`, `target_version=1.0.0`,
`candidate_state=DOCUMENTAL_ONLY` e o rotulo operacional `v0.1.0-rc1`.
Ele reutiliza as evidencias RLS1/RLS4 e a auditoria offline do updater, sem
criar tag, assinatura, publicacao ou alterar a imagem persistente.

Enquanto a validacao final nao for executada em um commit limpo, a RLS5 fica
`PENDING_VALIDATION`. Ausencia de RLS1/RLS4, imagem ou updater e publicada
como `BLOCKED`; inconsistencia de versao, hash, layout, worktree ou
evidencia e `FAIL`. O contrato de boot saudavel usa
`update_system_slots_boot_confirm()`, estado confirmado `boot_state=NONE` sem
slot pendente, limite de duas tentativas e rollback pelas fixtures existentes.

## Contratos e invariantes

- App API, syscalls, layouts binários, `taskmanager.h`, bootloader e
  `stage2.asm` permanecem inalterados, salvo roadmap específico aprovado.
- Nenhum comando pode deixar o terminal sem estado observável ou sem uma
  forma documentada de cancelamento/retorno.
- Falhas de recursos opcionais devem publicar degradação; falhas reais devem
  retornar erro e registrar contexto suficiente.
- A versão não será considerada reproduzível por funcionar apenas em uma
  configuração local do desenvolvedor.

## Critérios de saída

- Os gates de qualidade e build definidos pelo projeto passam na mesma versão
  que será levada ao QEMU.
- Nenhum cenário validado deixa o Shell sem `zephyr>` após o término da
  operação.
- A matriz suportada termina com diagnósticos sem falhas novas, sem recursos
  residuais e com os fallbacks documentados.
- A identidade mínima, as permissões, o supervisor de serviços e os limites
  por processo estão validados nos perfis suportados.
- A atualização do sistema possui versão candidata, confirmação de boot e
  rollback reproduzível sem destruir a única imagem inicializável.
- A imagem pode ser identificada, reproduzida e revertida.
- Todas as pendências aceitas estão associadas a uma dívida técnica ou a um
  backlog pós-1.0, nunca ocultas em um percentual agregado.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário executará os gates
operacionais do projeto e, depois, a matriz definida em RLS4, incluindo
repetição do comando que deixa a tela sem prompt. Cada resultado deve ser
registrado em `docs/qualidade/registro-validacoes.md`.

## Fora do escopo

Rust começa somente após esta linha de base. Recursos de produto ainda não
necessários para a confiabilidade da base ficam nos roadmaps pós-1.0.
