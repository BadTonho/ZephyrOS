# Roadmap 18 — Estabilização e release da versão 1.0.0

## Estado

Planejado. Esta frente prepara uma linha de base reproduzível e suportada para
a versão 1.0.0. Ela não adiciona uma nova API, syscall, formato binário ou
driver; organiza a correção das falhas que impediriam declarar o sistema
estável.

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

- Roadmaps 01–17 mantidos como base funcional;
- [Dívidas técnicas da v1.0.0](../qualidade/dividas-tecnicas-v1.0.0.md).

## Fases

### RLS1 — Linha de base e artefatos

- [ ] Fixar a identificação da versão 1.0.0, a origem dos fontes e o conjunto
  de ferramentas aceito.
- [ ] Registrar tamanho, checksum, layout de LBAs, símbolos e seções da
  imagem gerada.
- [ ] Confirmar que `boot.bin` continua com 512 bytes e que kernel, recovery
  loader e FAT32 não se sobrepõem.
- [ ] Reproduzir o build limpo em uma configuração documentada, sem caminhos
  pessoais ou ferramentas implícitas.
- [ ] Definir os artefatos que podem ser distribuídos e os que são apenas
  intermediários de validação.

### RLS2 — Liveness do Shell e dos jobs

- [ ] Mapear cada comando que cria job, abre cena, bloqueia entrada ou altera o
  foco.
- [ ] Garantir retorno único ao prompt após sucesso, erro, cancelamento,
  timeout, processo encerrado e recurso indisponível.
- [ ] Reproduzir o caso de tela vazia sem prompt e registrar em qual camada a
  execução terminou: dispatcher, job, cena, foco, vídeo ou entrada.
- [ ] Validar que erros não deixam o Shell esperando um callback, descritor,
  processo ou evento que já não existe.
- [ ] Validar reentrada, `F12`, `Ctrl+C`, fechamento de cenas e comandos
  inválidos sem prompt duplicado ou prompt ausente.

### RLS3 — Limpeza e invariantes

- [ ] Auditar ownership de buffers, snapshots, descritores, jobs, filas e
  referências de processo.
- [ ] Garantir que todos os caminhos de erro liberem ou transfiram seus
  recursos exatamente uma vez.
- [ ] Confirmar que locks e interrupções são restaurados em todos os retornos.
- [ ] Integrar falhas relevantes ao log da camada que possui o contexto, sem
  produzir logging pesado em IRQ ou hot path.
- [ ] Manter `health`, `regcheck`, `memcheck`, `schedcheck`, `proccheck` e os
  diagnósticos de rede/ACPI coerentes depois de ciclos repetidos.

### RLS4 — Regressão da matriz suportada

- [ ] Executar a matriz Simple/Classic com ACPI, sem ACPI, com NIC, sem NIC,
  com USB HID, sem USB, com Storage e sem volumes adicionais.
- [ ] Repetir abertura e fechamento de Shell, Explorer, Settings, Task Manager,
  Desktop, WM e Updater.
- [ ] Testar comandos que terminam normalmente, falham antes do commit,
  cancelam e deixam recursos ocupados.
- [ ] Confirmar fallback de vídeo, teclado, rede, áudio e armazenamento sem
  travamento ou tela sem prompt.
- [ ] Registrar diferenças entre cobertura validada e cobertura complementar.

### RLS5 — Candidata de release

- [ ] Congelar a lista de mudanças permitidas após o início da validação final.
- [ ] Atualizar documentação, contratos, índice, roadmap geral e registro de
  validações com evidência e horário real.
- [ ] Definir procedimento de rollback para a última imagem aprovada.
- [ ] Publicar a matriz de suporte e as limitações aceitas da 1.0.0.

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
