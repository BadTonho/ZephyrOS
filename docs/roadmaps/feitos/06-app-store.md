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

- **Classic e a interface grafica principal durante AS1-AS2 e MV0.**
- **Modern passara a ser a matriz grafica de AS3 somente depois de MV1-MV3
  implementarem o renderer futuro.**
- O Shell oferece fallback operacional completo e diagnostico reproduzivel.
- Simple permanece como fallback secundario congelado; seu smoke test e
  complementar e nao substitui a validacao do Classic/Modern e do Shell.
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

## AS2 - Ciclo de vida local com confirmacao (validado no QEMU)

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
AS1/AS2, `q3check`, `git diff --check` e build limpo passaram. A matriz QEMU
confirmou preflights sem escrita, motivos e bloqueadores estaveis, confirmacao
explicita, execucao instalada de `WAITAPP` com argumentos e cancelamento por
`F12`, bloqueio reverso `BASE`/`DEPEND` e remocao segura. `health summary`,
`pkgcheck`, `appcheck`, `memcheck` e `regcheck full` concluiram; a memoria
permaneceu em `20680 KB`, sem processo, zumbi ou pacote instalado residual.
AS2 esta validado e libera o inicio oficial de MV0-MV3.

## AS3 - Aplicativo nativo App Store

Esta fase comeca somente depois de AS1-AS2 e de MV0-MV3 do Roadmap 07 estarem
aprovados.

### Integracao

- [x] Criar o modulo em `src/appstore/appstore.c`.
- [x] Criar header autocontido em `src/include/ui/appstore.h`.
- [x] Registrar objeto no Makefile.
- [x] Adicionar `IPC_APP_OPEN_APP_STORE` ao final da enumeracao.
- [x] Adicionar `App Store` ao menu Iniciar, sem icone no Desktop no MVP.
- [x] Registrar uma janela singleton hospedada pelo Window Manager.
- [x] Fazer o comando `store` sem argumentos abrir o aplicativo.

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

### Estado

Implementacao e validacao host/QEMU concluidas pelo usuario. O modulo usa um
worker cooperativo para refresh, verificacao, preflight, instalacao, remocao e
abertura; a interface usa somente os contratos AS1/AS2. Atualizar permanece
visivel, mas desativado com a indicacao de que pertence ao AS4. O modo Simple
entrega as tres abas e as mesmas operacoes; o retorno de foco de ZAPPs
permanece no Shell.

### Fallback

- [x] O Shell preserva todas as operacoes e diagnosticos.
- [x] Simple oferece uma TUI funcional quando a hospedagem Modern falhar.
- [x] Classic, Shell e o smoke test complementar Simple foram validados.

### Criterio de saida

O usuario consegue listar, inspecionar, instalar, abrir e remover `VALID.ZPK`
pelo Modern. A janela permanece responsiva, as confirmacoes nao se confundem
com selecoes antigas e todos os erros continuam reproduziveis pelo Shell.

A matriz AS3 foi aprovada pelo usuario: fontes validas e invalidas, bloqueio
de dependencia, confirmacao, instalacao/remocao, `WAITAPP` com `F12`, retorno
de foco, singleton pelo Menu Iniciar/taskbar, fallback Simple e diagnosticos
finais concluiram sem residuos.

## AS4 - Atualizacao local e dependencias (concluida e validada)

Esta fase comeca somente depois do MVP AS1-AS3 e da validacao MV4 compartilhada
com o Roadmap 07.

- [x] Comparar `MAJOR.MINOR.PATCH` sem conversao ambigua ou overflow.
- [x] Permitir somente atualizacao para versao superior por padrao.
- [x] Exigir confirmacao separada para downgrade diagnostico.
- [x] Planejar dependencias disponiveis no catalogo em ordem topologica.
- [x] Recusar ciclos, conflitos, dependencias duplicadas e plano incompleto.
- [x] Exibir todo o plano antes de qualquer gravacao.
- [x] Definir staging, commit e recuperacao para troca de `APP.ZAP` e
  `META.DAT` sem perder a versao instalada em falha de I/O.
- [x] Manter uma versao anterior recuperavel por aplicativo atualizado.
- [x] Adicionar historico compacto de instalacao, remocao, atualizacao,
  rollback e recuperacao.
- [x] Expor `store update`, `store rollback`, `store history` e failpoint AS4.
- [x] Adicionar fixtures seed/update e alvos `store-as4-*`.

### Validacao concluida

O usuario executou `make q3check`, build limpo, os alvos AS4 e a matriz QEMU.
Foram validados o plano `UPDEPB -> UPDEPA -> UPTARGET`, update, downgrade com
dupla confirmacao, rollback consumivel, ciclo, plano incompleto, historico e
failpoints apos a primeira e a quinta troca. Os dois reboots recuperaram o
estado anterior com journal limpo e heap integro. A interface Classic repetiu
update e rollback e preservou a selecao por alias/ID depois de `F5`.

### Criterio de saida

Uma atualizacao local conclui na versao antiga ou nova integra, nunca em estado
parcial. Dependencias sao resolvidas antes da escrita e falhas continuam
recuperaveis depois do reboot.

## AS5 - Repositorio remoto autenticado (concluida e validada)

Esta fase usa confianca exclusiva da App Store e nao reutiliza a raiz ZUPD.

- [x] Criar o contrato publico `app_remote` e inicializa-lo antes do catalogo.
- [x] Definir o catalogo binario `ZAC1`, geracao monotona e assinatura
  Ed25519 sobre dominio proprio.
- [x] Preservar `ZPKG v1` e autenticar cada pacote por SHA-256.
- [x] Adicionar chave publica ativa de teste e IDs revogados sem versionar
  material privado.
- [x] Recusar assinatura, chave, replay, caminho, duplicacao e metadados
  conflitantes antes de publicar cache.
- [x] Resolver apenas dependencias instaladas ou presentes no catalogo remoto
  autenticado, sem misturar fontes locais implicitamente.
- [x] Implementar cache FAT12 A/B, registros redundantes, limpeza de slot
  pendente e preservacao do slot ativo.
- [x] Reusar transacao, rollback, historico e gate AS4 para planos vindos de
  um diretorio de cache.
- [x] Registrar procedencia separadamente e usar confianca `N/D` em falha.
- [x] Adicionar comandos `store remote`, opt-in por sessao, cancelamento e
  failpoint de cache.
- [x] Adicionar a aba Remoto ao Classic e manter o Simple congelado.
- [x] Adicionar fixtures assinados, auditoria, Q3Check e alvos host AS5.
- [x] Validar `q3check`, build limpo, alvos AS5 e matriz QEMU pelo usuario.

### Criterio de saida

Nenhum pacote remoto aparece como instalavel antes de autenticar catalogo e
artefato. Rede ausente ou host malicioso nao degrada instalacao e execucao
locais.

### Estado

Implementacao, contratos, fixtures e documentacao estao concluidos. A validacao
do usuario cobriu o plano `RMDEPB -> RMDEPA -> RMTARGET`, cache/offline,
integracao transacional AS4, vetores criptograficos, recuperacao, Classic,
smoke Simple e diagnosticos finais. A rotacao A/B seed/update terminou com a
geracao 2 valida e sem slot pendente.

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

## Validacao do usuario para AS5

```text
make package-test
make store-test
make store-as2-test
make store-as4-test
make store-as5-test
make q3check
make clean
make
```

Em um terminal separado, manter o repositorio de teste ativo:

```text
make store-as5-seed-demo
```

Depois executar `make run` em outro terminal. No QEMU:

1. Confirmar remoto desabilitado no boot e o catalogo local intacto.
2. Habilitar, consultar e validar o plano `RMDEPB -> RMDEPA -> RMTARGET`.
3. Confirmar que o preflight de fetch nao grava e que o fetch confirmado
   publica o cache completo.
4. Desabilitar a rede e instalar o plano autenticado a partir do cache.
5. Servir o perfil update, testar update, recusa/confirmacao de downgrade,
   rollback e historico AS4.
6. Validar assinatura, hash, chave, replay, ciclo e plano incompleto com os
   fixtures negativos.
7. Validar timeout, F12/Esc, espaco, mutacao concorrente e failpoint com reboot,
   preservando o slot ativo.
8. Repetir as acoes pela aba Remoto no Classic e fazer apenas o smoke Simple.
9. Executar `health summary`, `pkgcheck`, `appcheck`, `memcheck` e
   `regcheck full`, verificando que nao restaram arquivos pendentes.

## Fora do MVP AS1-AS3

- multiplos repositorios ou raiz de confianca oficial;
- atualizacao automatica ou consulta no boot;
- pagamento, contas, telemetria, avaliacao ou recomendacao;
- API grafica para aplicativos externos;
- icones, screenshots ou multiplos arquivos em ZPKG;
- ELF, bibliotecas dinamicas e permissoes complexas;
- migracao de Explorer, Settings, Task Manager, Desktop ou Window Manager;
- remocao de aplicativos nativos.

## Proximo passo

Preservar a matriz AS5 como regressao para futuras alteracoes no filesystem,
HTTP, criptografia, motor transacional ou App Store.
