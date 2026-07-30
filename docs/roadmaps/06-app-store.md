# Roadmap 06 - App Store

## Objetivo

Entregar uma App Store nativa para descobrir, inspecionar, instalar, executar
e remover aplicativos externos sem duplicar o loader ZAPP nem o servico de
pacotes existente.

A primeira entrega sera um catalogo **local e manual**. Ela usara os pacotes
`.zephyrosapp`/`ZPKG v1` ja transportados como `ID.ZPK` na raiz do volume.
Rede, assinatura de publicador, atualizacao automatica e repositorio remoto
ficam fora do MVP.

## Base ja validada

- [x] App API `0.3`, processos ring 3 e isolamento de falhas.
- [x] Loader de imagens `ZAPP` i386 com argumentos, foco e cancelamento.
- [x] Container `.zephyrosapp`/`ZPKG v1` com manifesto, CRC32 e uma imagem
  `APP.ZAP`.
- [x] Empacotador host `tools/packager.py` e aliases FAT12 `ID.ZPK`.
- [x] Instalacao em `APPS/<ID>/APP.ZAP` e `APPS/<ID>/META.DAT`.
- [x] Listagem de instalados, verificacao, dependencias e remocao protegida.
- [x] Comandos `pkg list|info|verify|install|remove` e diagnostico `pkgcheck`.

Essas capacidades continuam sendo a fonte de verdade. A App Store sera uma
camada de catalogo, politica, confirmacao e interface sobre elas.

## Decisoes de produto

- **Modern e a interface principal e a matriz obrigatoria de aceitacao.**
- O Shell oferece fallback operacional completo e diagnostico reproduzivel.
- Classic permanece planejado como fallback secundario; sua regressao visual
  e complementar e nao bloqueia uma fase ja aprovada no Modern e no Shell.
- Aplicativos nativos do kernel nao podem ser instalados ou removidos pela
  loja.
- Nenhuma acao ocorre no boot e nenhuma instalacao e silenciosa.
- Verificar, listar, atualizar a tela e executar preflight nunca gravam.
- Instalar e remover exigem confirmacao explicita e repetem o preflight.
- O arquivo fonte `ID.ZPK` nao e apagado pela instalacao ou remocao.
- O MVP nao altera `ZPKG v1`, App API `0.3`, boot, stage2 ou setores crus.
- CRC32 detecta corrupcao acidental, mas nao autentica autoria. A interface
  deve exibir `LOCAL / NAO ASSINADO` para todo pacote ZPKG v1.

## Sequencia integrada com o Roadmap 07

Os numeros 06 e 07 identificam os documentos, mas nao exigem a conclusao
integral de um antes do inicio do outro. Para evitar construir a App Store com
o visual antigo e refazer sua interface logo depois, a execucao oficial sera:

1. **AS1 e AS2**: catalogo, observabilidade e ciclo de vida pelo servico e
   pelo Shell.
2. **MV0 a MV3 do Roadmap 07**: metricas, primitivas, tema e moldura Modern.
3. **AS3**: aplicativo App Store ja construido sobre a fundacao visual nova.
4. **MV4 do Roadmap 07**: modernizacao e medicao dos aplicativos, incluindo a
   App Store.
5. **AS4 e AS5**: evolucoes locais e remotas posteriores ao MVP.

AS1 e AS2 nao dependem do redesenho. AS3 depende de MV0-MV3 para nao duplicar
trabalho de layout, controles, cores e molduras.

## Modelo do catalogo local

O catalogo do MVP nao tera banco de dados proprio. Ele sera reconstruido a
partir de duas fontes existentes:

```text
Disponiveis: arquivos *.ZPK validos na raiz do volume
Instalados:  APPS/<ID>/META.DAT
Executavel:  APPS/<ID>/APP.ZAP
```

O catalogo enumerara no maximo 16 fontes `.ZPK`, ignorando diretorios e
arquivos hidden/system, e ordenara por alias FAT. Cada entrada combinara:

- alias fonte e manifesto validado;
- ID, nome, versao, App API e dependencias;
- versao instalada, quando existir;
- estado `AVAILABLE`, `INSTALLED`, `UPDATE_AVAILABLE`, `SAME_VERSION`,
  `DOWNGRADE`, `BLOCKED` ou `INVALID`;
- motivo estavel para erro estrutural, dependencia, espaco, servico ou versao;
- capacidades de verificar, instalar, executar, remover ou atualizar.

Um manifesto cujo `id` nao corresponda ao alias `ID.ZPK` sera recusado. A
listagem usa workspace estatico e nao mantem pacotes inteiros em memoria.

## AS1 - Catalogo local e observabilidade (validado no QEMU)

### Implementacao

- [x] Criar o contrato canonico em `docs/13-aplicativos/app-store.md`.
- [x] Criar o servico em `src/core/app_catalog.c` e o header autocontido
  `src/include/core/app_catalog.h`.
- [x] Definir estados, motivos e consultas estaveis no header publico.
- [x] Registrar o novo objeto no Makefile e o contrato no catalogo de headers.
- [x] Enumerar e ordenar ate 16 arquivos `.ZPK` da raiz.
- [x] Combinar fontes locais com os registros instalados em `APPS/`.
- [x] Validar alias, header, manifesto, CRC32, ZAPP, versao e dependencias.
- [x] Expor consultas somente-leitura para contagem, entrada e status geral.
- [x] Adicionar `store status`, `store list` e `store info <ID|alias.ZPK>`.
- [x] Mostrar separadamente pacote invalido e servico indisponivel.
- [x] Adicionar `RECOVERY_COMPONENT_APP_STORE` ao final da enumeracao:
  - `READY`: catalogo local e servico `PKG` disponiveis;
  - `DEGRADED`: catalogo parcial com alguma fonte invalida;
  - `DISABLED`: filesystem, loader ou servico `PKG` indisponivel.
- [x] Mostrar App Store no `health` completo e no `health summary`.

### Fixtures

Reutilizar `DEMO.ZPK` e acrescentar fixtures publicos deterministas:

- `VALID.ZPK`: disponivel e instalavel;
- `BADCRC.ZPK`: conteudo corrompido;
- `BADAPI.ZPK`: App API incompativel;
- `BADALIAS.ZPK`: manifesto valido com ID diferente do alias;
- `NEEDSDEP.ZPK`: dependencia local ausente;
- `SAMEVER.ZPK`: mesma versao de um pacote instalado.

Nenhuma chave ou senha e necessaria nesta fase.

`tools/packager.py` sera estendido com `fixtures-store` e `audit-store`.
Os alvos host `store-test` e `store-demo` executarao os autotestes e injetarao
os aliases apenas para a matriz da App Store; o build normal nao dependera
desses fixtures.

### Criterio de saida

Listagem e consultas produzem sempre a mesma ordem e os mesmos motivos, nao
gravam no disco e nao deixam alocacoes, handles ou processos residuais.

### Estado

AS1 concluido e validado no host e no QEMU. `packager.py selftest`,
`audit-store`, `q3check`, build limpo e os seis fixtures passaram. A matriz
confirmou ordem e motivos deterministas, `AVAILABLE -> SAME_VERSION ->
AVAILABLE`, memoria estavel em `20680 KB`, recovery `DEGRADED`, `appcheck`,
`memcheck` e `regcheck full` em `OK`, sem processos ou arquivos instalados
residuais.

## AS2 - Ciclo de vida local com confirmacao (aguardando QEMU)

### Implementacao

- [x] Adicionar preflight separado para instalar e remover.
- [x] Repetir integralmente o preflight depois da confirmacao.
- [x] Serializar mutacoes para impedir instalacao/remocao concorrente.
- [x] Bloquear mutacoes enquanto um ZAPP externo estiver em primeiro plano.
- [x] Preservar as regras atuais de espaco e dependencias.
- [x] Adicionar `store install <alias.ZPK>` como preflight sem escrita.
- [x] Exigir `store install <alias.ZPK> --confirm` para instalar.
- [x] Adicionar `store remove <ID>` como preflight sem escrita.
- [x] Exigir `store remove <ID> --confirm` para remover.
- [x] Adicionar `store run <ID> [args]` sobre o loader existente.
- [x] Atualizar o catalogo somente depois do encerramento da operacao.
- [x] Manter `pkg` como interface administrativa compativel.

O MVP nao resolvera dependencias automaticamente. Se uma dependencia estiver
ausente, o preflight informa os IDs bloqueadores e nao grava.

Os seis fixtures AS1 permanecem imutaveis. A matriz separada AS2 acrescenta
`WAITAPP.ZPK`, `BASE.ZPK` e `DEPEND.ZPK`, com geracao/auditoria deterministica
e alvos `store-as2-test`/`store-as2-demo`.

### Criterio de saida

Somente `--confirm` pode alterar `APPS/`. Instalacao, execucao e remocao do
fixture valido passam; pacote invalido, dependencia ausente, falta de espaco,
ID instalado e dependente reverso falham sem deixar diretorio parcial.

### Estado

Implementacao e fixtures concluidos no repositorio. Autoteste, auditorias
AS1/AS2, `q3check` e `git diff --check` passaram. AS2 permanece pendente de
build limpo e da matriz QEMU do usuario; nao esta validado e ainda nao libera
o inicio oficial de MV0-MV3.

## AS3 - Aplicativo nativo App Store

Esta fase comeca somente depois de AS1-AS2 e de MV0-MV3 do Roadmap 07 estarem
aprovados.

### Integracao

- [ ] Criar o modulo em `src/appstore/appstore.c`.
- [ ] Criar header autocontido em `src/include/ui/appstore.h`.
- [ ] Registrar objeto no Makefile.
- [ ] Adicionar `IPC_APP_OPEN_APP_STORE` ao final da enumeracao.
- [ ] Adicionar `App Store` ao menu Iniciar, sem icone no Desktop no MVP.
- [ ] Registrar uma janela singleton hospedada pelo Window Manager.
- [ ] Fazer o comando `store` sem argumentos abrir o aplicativo.

### Interface Modern

A janela tera tres abas:

- **Catalogo**: fontes `.ZPK`, estado, versao e confianca local;
- **Instalados**: aplicativos em `APPS/`, versao e dependentes;
- **Detalhes**: manifesto, dependencias, motivo e operacoes permitidas.

Os botoes serao `Atualizar`, `Verificar`, `Instalar`, `Abrir` e `Remover`.
Instalacao e remocao usam dialogo modal com confirmacao explicita. Alterar aba,
selecao ou atualizar a lista cancela qualquer confirmacao pendente.

O teclado usara Tab, setas, F5, `V`, `I`, `A`, `R` e Enter. `Esc` cancela o
contexto atual, mas nao fecha uma janela Modern ociosa; o fechamento usa o
botao `X` ou `Alt+F4`.

Operacoes de filesystem devem rodar fora do callback de desenho/entrada. Um
worker cooperativo manterá Window Manager, rede, mouse e Shell responsivos.

### Fallback

- [ ] O Shell preserva todas as operacoes e diagnosticos.
- [ ] Classic oferece uma TUI funcional quando a hospedagem Modern falhar.
- [ ] A validacao obrigatoria usa Shell e Modern; Classic e cobertura
  complementar.

### Criterio de saida

O usuario consegue listar, inspecionar, instalar, abrir e remover `VALID.ZPK`
pelo Modern. A janela permanece responsiva, as confirmacoes nao se confundem
com selecoes antigas e todos os erros continuam reproduziveis pelo Shell.

## AS4 - Atualizacao local e dependencias

Esta fase comeca somente depois do MVP AS1-AS3 e da validacao MV4 compartilhada
com o Roadmap 07.

- [ ] Comparar `MAJOR.MINOR.PATCH` sem conversao ambigua ou overflow.
- [ ] Permitir somente atualizacao para versao superior por padrao.
- [ ] Exigir confirmacao separada para downgrade diagnostico.
- [ ] Planejar dependencias disponiveis no catalogo em ordem topologica.
- [ ] Recusar ciclos, conflitos, dependencias duplicadas e plano incompleto.
- [ ] Exibir todo o plano antes de qualquer gravacao.
- [ ] Definir staging, commit e recuperacao para troca de `APP.ZAP` e
  `META.DAT` sem perder a versao instalada em falha de I/O.
- [ ] Manter no maximo uma versao anterior recuperavel por aplicativo.
- [ ] Adicionar historico compacto de instalacao, remocao e atualizacao.

### Criterio de saida

Uma atualizacao local conclui na versao antiga ou nova integra, nunca em estado
parcial. Dependencias sao resolvidas antes da escrita e falhas continuam
recuperaveis depois do reboot.

## AS5 - Repositorio remoto assinado

Esta fase e futura e nao reutilizara implicitamente a confianca do ZUPD.

- [ ] Definir ameacas, chave de publicador e politica de revogacao.
- [ ] Definir catalogo remoto assinado e versionado.
- [ ] Escolher entre assinatura destacada para ZPKG v1 ou um ZPKG v2.
- [ ] Manter HTTP apenas como transporte, com autenticidade criptografica.
- [ ] Exigir opt-in por sessao e consulta manual.
- [ ] Baixar para cache antes de oferecer instalacao.
- [ ] Nunca instalar, atualizar ou executar automaticamente.
- [ ] Preservar catalogo/cache anterior em timeout, cancelamento ou fraude.
- [ ] Separar claramente `LOCAL / NAO ASSINADO` de `REMOTO / AUTENTICADO`.

### Criterio de saida

Nenhum pacote remoto aparece como instalavel antes de autenticar catalogo e
artefato. Rede ausente ou host malicioso nao degrada instalacao e execucao
locais.

## Validacao do agente

Sem executar build:

```text
python tools/packager.py selftest
python tools/q3check.py
git diff --check
git status --short
```

Cada fase tambem exige revisao restrita aos arquivos alterados, incluindo
ausencia de chaves privadas, credenciais, caminhos locais e artefatos de build.

## Validacao do usuario para o MVP AS1-AS3

```text
make package-test
make store-test
make store-as2-test
make q3check
make clean
make
make store-demo
make store-as2-demo
make run
```

No QEMU:

1. Confirmar `store status`, `store list` e os motivos dos fixtures.
2. Confirmar que consultas nao alteram a imagem nem `APPS/`.
3. Fazer preflight e instalar `VALID.ZPK` com confirmacao.
4. Abrir o aplicativo instalado e cancelar com F12.
5. Tentar os pacotes invalidos e a dependencia ausente.
6. Remover o aplicativo com confirmacao.
7. Repetir o fluxo no App Store Modern.
8. Executar `health summary`, `mem`, `pkgcheck`, `appcheck` e `regcheck full`.
9. Confirmar ausencia de processos ring 3, diretorios parciais e vazamento.

## Fora do MVP AS1-AS3

- assinatura de pacotes e repositorio remoto;
- atualizacao automatica ou consulta no boot;
- pagamento, contas, telemetria, avaliacao ou recomendacao;
- API grafica para aplicativos externos;
- icones, screenshots ou multiplos arquivos em ZPKG;
- ELF, bibliotecas dinamicas e permissoes complexas;
- migracao de Explorer, Settings, Task Manager, Desktop ou Window Manager;
- remocao de aplicativos nativos.

## Proximo passo

Validar **AS2 - Ciclo de vida local com confirmacao** no build e no QEMU.
Somente depois da aprovacao, a execucao passa para MV0-MV3 do Roadmap 07 antes
de iniciar AS3.
