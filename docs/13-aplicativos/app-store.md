# App Store - Catalogo local, repositorio remoto e interface AS5

## Resumo de progresso

O AS1 implementa o catalogo local somente-leitura sobre `ZPKG v1` e o servico
`PKG`; ele esta concluido e validado. O AS2 acrescenta preflight, confirmacao,
instalacao, remocao e execucao pelo Shell. O AS2 esta concluido e validado no
host e no QEMU. O AS3 implementa a interface nativa hospedada, com fallback
Simple completo, e foi validado no host e no QEMU pelo usuario.

A interface AS4 usa a aparencia Modern Dark dentro do renderer Classic VESA;
`guimode modern` continua reservado. AS4 acrescenta atualizacao local FAT12,
plano topologico de dependencias, rollback manual e historico compacto, sem
rede, assinatura, alteracao do ZPKG, App API ou loader. A fase foi validada no
host e no QEMU pelo usuario em 01/08/2026.

AS5 acrescenta um repositorio HTTP manual autenticado por Ed25519, catalogo
binario `ZAC1`, hash SHA-256 por pacote e cache FAT12 A/B. O codigo esta
implementado; build, auditorias host e matriz QEMU permanecem pendentes de
validacao pelo usuario. HTTP e somente transporte: consulta, download,
instalacao e atualizacao exigem acoes explicitas e o remoto inicia desabilitado
em toda sessao.

## Fontes e snapshot

O snapshot combina:

```text
Fontes:     arquivos *.ZPK na raiz do volume
Instalados: APPS/<ID>/META.DAT
Executavel: APPS/<ID>/APP.ZAP
```

O catalogo ignora diretorios e arquivos hidden/system. Ele percorre toda a
raiz e preserva deterministicamente os 16 menores aliases `.ZPK` em ordem
lexica. Ate 32 entradas combinadas ficam em memoria estatica; nenhum pacote
completo permanece retido depois do refresh.

Os pacotes sao validados por `app_package_verify_file()`. Assim, o catalogo
nao duplica o parser de header, manifesto, CRC32 ou ZAPP. Um pacote valido
cujo ID nao corresponda exatamente ao alias `ID.ZPK` permanece visivel como
`INVALID / ALIAS_MISMATCH`.

Pacotes instalados sem fonte local aparecem como `INSTALLED`. A comparacao de
`MAJOR.MINOR.PATCH` remove zeros a esquerda e compara o comprimento e os
digitos de cada componente, sem converter para inteiros sujeitos a overflow.

## Contrato publico

`src/include/core/app_catalog.h` define limites, estados, motivos, capacidades
e consultas por copia.

### Estados

| Estado | Significado |
|---|---|
| `AVAILABLE` | Fonte valida sem versao instalada e sem dependencia ausente. |
| `INSTALLED` | Registro instalado sem fonte `.ZPK` correspondente. |
| `UPDATE_AVAILABLE` | A fonte possui versao superior a instalada. |
| `SAME_VERSION` | Fonte e registro instalado possuem a mesma versao. |
| `DOWNGRADE` | A fonte possui versao inferior a instalada. |
| `BLOCKED` | Alguma dependencia declarada nao esta instalada. |
| `INVALID` | Pacote, leitura ou correspondencia entre alias e ID falhou. |

Dependencia ausente tem prioridade sobre a relacao de versao. Quando ja existe
uma versao instalada, as capacidades `RUN` e `REMOVE` continuam visiveis mesmo
com a fonte bloqueada.

### Motivos

Os valores sao append-only e seus conversores retornam tokens estaveis:

```text
NONE
PACKAGE_INVALID
ALIAS_MISMATCH
DEPENDENCY_MISSING
INSUFFICIENT_SPACE
SOURCE_LIMIT
ENTRY_LIMIT
READ_ERROR
FILESYSTEM_UNAVAILABLE
LOADER_UNAVAILABLE
PACKAGE_SERVICE_UNAVAILABLE
```

`INSUFFICIENT_SPACE` e calculado pelo preflight AS2. As capacidades do catalogo
continuam informativas; toda mutacao rele o pacote e o estado instalado.

### Capacidades e estruturas

As capacidades sao `VERIFY`, `INSTALL`, `RUN`, `REMOVE` e `UPDATE`. Cada
`app_catalog_entry_t` contem:

- alias e tamanho da fonte;
- manifestos fonte e instalado;
- indicadores `has_source` e `has_installed`;
- estado, motivo e capacidades;
- mascara de dependencias ausentes, usando a ordem do manifesto fonte.

`app_catalog_status_t` agrega fontes retidas, validas e invalidas, pacotes
instalados, entradas e os limites excedidos.

As operacoes publicas sao:

```text
app_catalog_init()
app_catalog_refresh()
app_catalog_is_ready()
app_catalog_get_status()
app_catalog_get_count()
app_catalog_get_entry()
app_catalog_find_entry()
app_catalog_build_install_plan()
app_catalog_build_update_plan()
app_catalog_state_name()
app_catalog_reason_name()
```

`get_status()` continua disponivel depois de uma inicializacao desabilitada
para explicar a dependencia ausente. Contagem, entrada e busca exigem um
snapshot consultavel.

## Ciclo de vida AS2

O servico `PKG` expoe preflights separados para instalar e remover. Eles
validam novamente fonte, alias, dependencias, espaco, ID instalado e
dependentes reversos, mas nunca escrevem. A operacao confirmada adquire o gate
global de mutacao e repete integralmente o preflight antes de alterar `APPS/`.

Somente o token final exato `--confirm` autoriza instalacao ou remocao. Nao ha
confirmacao armazenada entre comandos. Dependencias ausentes e dependentes
reversos sao mostrados por ID em ordem lexical; o MVP nao tenta instala-los ou
remove-los automaticamente.

Todas as interfaces de mutacao, inclusive `pkg`, compartilham o mesmo gate.
Mutacoes sao bloqueadas enquanto um ZAPP externo estiver em primeiro plano.
`store run` aceita somente um ID instalado e executa
`APPS/<ID>/APP.ZAP` pelo loader existente.

## Transacoes AS4

`app_package_compare_versions()` valida exatamente tres componentes numericos
e compara cada um como texto normalizado, sem conversao numerica. O planejador
gera no maximo 16 entradas: dependencias locais ausentes em ordem topologica
lexica e, por ultimo, o alvo. Dependencias instaladas nunca recebem update
implicito; fonte ausente/invalida, ciclo ou conflito recusam o plano antes de
qualquer escrita.

No FAT12, o servico cria staging privado, copia a versao anterior do alvo e
persiste journal redundante antes de trocar `APP.ZAP` e `META.DAT` por escrita
copy-on-write. No boot, `app_package_init()` recupera journal `PREPARED` ou
`REPLACING` antes de o catalogo ser lido. Journal invalido ou recuperacao
incompleta bloqueiam mutacoes, mas mantem as consultas. FAT32 informa suporte
transacional indisponivel e nao grava AS4.

As APIs publicas incluem `app_package_preflight_plan()`,
`app_package_apply_plan_confirmed()`, `app_package_preflight_rollback()`,
`app_package_rollback_confirmed()`, `app_package_get_status()` e leitura de
historico. Cada app atualizado mantem uma copia anterior recuperavel; o
rollback consome somente a copia do app selecionado. A fonte `.ZPK` local
permanece intacta, por isso a Store pode voltar a indicar update disponivel.

## Repositorio autenticado AS5

`app_remote` e inicializado depois de `app_package_init()` e antes do catalogo
local. Seu contrato publico fica em `core/app_remote.h`; configuracao e
confianca de teste ficam em `core/app_remote_config.h` e
`core/app_remote_trust.h`. A tabela de confianca e exclusiva da App Store e
nao reutiliza a raiz ZUPD.

O catalogo `ZAC1` possui header fixo de 128 bytes, ate 16 entradas de 256
bytes em ordem lexical por ID e assinatura Ed25519 de 64 bytes sobre dominio
proprio, header e entradas. Header e entradas registram geracao monotona,
canal, key ID, SHA-256 do bloco, manifesto resumido, tamanho, SHA-256 e caminho
HTTP de cada `ZPKG v1`. Campos reservados, caminhos inseguros, duplicacoes,
conflitos, chave desconhecida/revogada, assinatura invalida e replay sao
recusados antes de qualquer publicacao.

O planejador remoto inclui somente dependencias instaladas ou presentes no
mesmo catalogo autenticado. Fontes locais nao assinadas nunca entram
implicitamente. O plano completo e baixado no slot inativo `ASCACHE0` ou
`ASCACHE1`; `PLAN.CAT` e cada `<ID>.ZPK` sao revalidados antes de publicar os
registros redundantes `ASR0.STA`/`ASR1.STA`. Um slot pendente e descartado no
boot sem tocar no ativo. A maior geracao publicada sobrevive a limpeza do
cache.

Instalacao e update a partir do cache usam as APIs append-only de diretorio do
`app_package` e, portanto, a mesma transacao, gate, rollback e historico AS4.
A procedencia autenticada usa controle redundante separado; se ele nao puder
ser lido ou persistido, a interface informa confianca `N/D`. FAT32 permite
consulta autenticada em RAM, mas recusa cache e mutacao AS5 sem escrita.

## Recovery e health

`RECOVERY_COMPONENT_APP_STORE` foi anexado ao fim da enumeracao:

- `READY`: snapshot completo, inclusive quando o catalogo esta vazio;
- `DEGRADED`: fonte invalida, leitura parcial ou limite excedido;
- `DISABLED`: filesystem, loader ZAPP ou servico `PKG` indisponivel.

O `health` completo lista o componente. `health summary` sempre mostra a App
Store, inclusive quando `READY`, junto das contagens do snapshot.

Fontes invalidas degradam somente a observabilidade: entradas validas
continuam consultaveis e `app_catalog_refresh()` retorna `OK`.

## Comandos

| Comando | Acao |
|---|---|
| `store` | Abre a App Store hospedada; usa TUI completa no fallback Simple. |
| `store status` | Atualiza e mostra recovery, contagens, limites e motivo geral. |
| `store list` | Atualiza e lista entradas em ordem deterministica. |
| `store info <ID\|alias.ZPK>` | Mostra manifesto, versoes, confianca, dependencias e capacidades. |
| `store install <alias.ZPK>` | Executa preflight sem escrita e mostra o comando de confirmacao. |
| `store install <alias.ZPK> --confirm` | Repete o preflight e instala o pacote. |
| `store update <ID\|alias.ZPK> [--downgrade] [--confirm]` | Mostra/aplica o plano local; downgrade exige os dois tokens. |
| `store rollback <ID> [--confirm]` | Restaura e consome a versao anterior recuperavel. |
| `store history [ID]` | Lista o historico compacto, opcionalmente filtrado. |
| `store test fail-after <1..32>` | Configura failpoint AS4 apenas para validacao QEMU. |
| `store remove <ID>` | Executa preflight de remocao sem escrita. |
| `store remove <ID> --confirm` | Repete o preflight e remove o pacote. |
| `store run <ID> [args]` | Executa somente o ZAPP instalado; F12 cancela. |
| `store remote status` | Mostra opt-in, rede, geracao, autenticacao, cache ativo e publicacao pendente. |
| `store remote enable\|disable` | Habilita ou desabilita o remoto somente na sessao. |
| `store remote check [--url URL]` | Consulta e autentica o `ZAC1` sem gravar. |
| `store remote list` | Lista o catalogo remoto autenticado em memoria. |
| `store remote info <ID>` | Mostra manifesto remoto, hash, caminho e cache. |
| `store remote fetch <ID> [--url URL] [--confirm]` | Mostra ou baixa o plano completo no slot inativo. |
| `store remote install <ID> [--confirm]` | Mostra ou instala offline o plano autenticado em cache. |
| `store remote update <ID> [--downgrade] [--confirm]` | Atualiza do cache; downgrade exige os dois sinais. |
| `store remote clear [--confirm]` | Mostra ou limpa somente os slots de cache. |
| `store remote test fail-after <1..16>` | Configura failpoint de publicacao do cache. |

Todo pacote fonte e apresentado como `LOCAL / NAO ASSINADO`. Os subcomandos
preservam o diagnostico reproduzivel pelo Shell.

## Interface AS3 a AS5

`src/include/ui/appstore.h` define o ciclo de vida da interface:

```text
appstore_init/open/close/draw
appstore_handle_key/appstore_handle_mouse
appstore_is_open/appstore_get_mode
```

O modulo possui uma janela singleton hospedada pelo Window Manager e um worker
cooperativo. Refresh, verificacao, preflight, instalacao, atualizacao,
rollback, remocao e abertura de ZAPPs sao executados fora dos callbacks de
desenho e entrada. O worker usa somente `app_catalog_*`, `app_package_*` e o
loader existente; nao duplica validacao de ZPKG, CRC, dependencias ou
serializacao de mutacoes.

As abas locais sao **Catalogo**, **Instalados** e **Detalhes**. `Tab`, setas, `F5`,
`V`, `I`, `U`, `A`, `R`, `B` e Enter equivalem aos botoes Verificar,
Instalar, Atualizar, Abrir, Remover e Reverter. A confirmacao mostra a ordem
topologica e identifica downgrade diagnostico. A confirmacao modal e vinculada ao ID/alias e a
selecao atual; trocar aba, selecao ou atualizar o catalogo a cancela.

No Classic, AS5 acrescenta a aba **Remoto**, separada do catalogo local. Ela
mostra `REMOTO / AUTENTICADO (TESTE)` e oferece Habilitar, Consultar, Baixar,
Instalar, Atualizar, Abrir e Reverter. Os atalhos sao `E`, `F5`, `D`, `I`, `U`,
`A` e `B`; em largura minima os sete botoes ocupam duas linhas. Rede e
filesystem continuam no worker, e Esc/F12 solicitam cancelamento cooperativo
do download. O modo Simple permanece congelado; o Shell e o fallback remoto
completo.

No modo Classic, Esc cancela somente o contexto atual e a janela fecha por X
ou Alt+F4. No fallback Simple, Esc tambem fecha a TUI quando nao ha
confirmacao pendente. Abrir um app instalado fecha a janela da Store para
deixar o ZAPP em primeiro plano; o loader devolve o foco ao Shell apos o fim
ou cancelamento por F12.

## Fixtures e atalhos host

Os fixtures publicos ficam em `docs/fixtures/apps/store/`:

| Alias | Resultado inicial |
|---|---|
| `VALID.ZPK` | `AVAILABLE / NONE` |
| `BADCRC.ZPK` | `INVALID / PACKAGE_INVALID` |
| `BADAPI.ZPK` | `INVALID / PACKAGE_INVALID` |
| `BADALIAS.ZPK` | `INVALID / ALIAS_MISMATCH` |
| `NEEDSDEP.ZPK` | `BLOCKED / DEPENDENCY_MISSING` |
| `SAMEVER.ZPK` | `AVAILABLE`; depois de instalado, `SAME_VERSION` |

Geracao e auditoria:

```text
python tools/packager.py fixtures-store --output-dir docs/fixtures/apps/store
python tools/packager.py audit-store --fixtures-dir docs/fixtures/apps/store
python tools/packager.py audit-store --fixtures-dir docs/fixtures/apps/store --image build/zephyros.img
```

`fixtures.json` fixa formato, IDs, estados, motivos, tamanhos e SHA-256. O
auditor tambem regenera os bytes esperados e, quando recebe `--image`, compara
cada alias da raiz FAT12. Nenhuma chave privada e usada.

Os atalhos sao:

```text
make store-test
make store-demo
make store-as2-test
make store-as2-demo
```

`store-test` executa o autoteste do empacotador e audita os fixtures
versionados. `store-demo` injeta somente a matriz AS1 com substituicao
idempotente. Os alvos `store-as2-*` usam uma matriz separada e nao alteram os
seis aliases ou hashes canonicos do AS1. Durante a validacao AS3, o build
normal tambem injeta as matrizes AS1 e AS2 na imagem, para que `make clean &&
make` sempre produza um catalogo testavel. Os alvos `store-*-demo` continuam
disponiveis para reinjetar uma matriz na imagem ja criada.

### Fixtures AS2

Os artefatos de ciclo de vida ficam em `docs/fixtures/apps/store-as2/`:

| Alias | Cobertura |
|---|---|
| `WAITAPP.ZPK` | Instalacao, argumentos, execucao persistente e F12. |
| `BASE.ZPK` | Dependencia instalavel sem requisitos. |
| `DEPEND.ZPK` | Depende de `BASE` e bloqueia sua remocao. |

`fixtures-store-as2` gera a matriz; `audit-store-as2` confere nomes, hashes,
manifestos, payload de espera, dependencia e, opcionalmente, os bytes FAT12.

### Fixtures AS4

Os perfis ficam em `docs/fixtures/apps/store-as4-seed/` e
`docs/fixtures/apps/store-as4-update/`. O seed possui `UPTARGET 1.0.0`; o
update possui `UPTARGET 1.1.0 -> UPDEPA -> UPDEPB`. Ambos tambem cobrem
`BROKEN` (fonte de dependencia ausente) e `CYCLEA`/`CYCLEB` (ciclo).

```text
make store-as4-test
make store-as4-seed-demo
make store-as4-update-demo
```

O build normal injeta o perfil `update`; as seis fontes AS4, junto das nove
AS1/AS2, mantem o catalogo abaixo do limite de 16 fontes.

### Fixtures AS5

Os artefatos publicos assinados ficam em `docs/fixtures/apps/store-as5/`.
`seed` e `update` cobrem `RMTARGET`, `RMDEPA` e `RMDEPB`; `invalid` cobre plano
incompleto, ciclo, pacote divergente, hash incorreto, assinatura adulterada,
chave desconhecida/revogada e replay. Os arquivos binarios sao versionados em
Base64 para nao injetar pacotes remotos na imagem normal.

```text
make store-as5-test
make store-as5-seed-demo
make store-as5-serve
```

`store-as5-test` audita chave publica, key ID, assinatura, hashes, manifestos
e cenarios negativos. Os outros dois alvos servem os perfis seed e update em
`10.0.2.2:8000`. A chave privada de teste nao pertence ao repositorio; o
comando host `sign-store-as5` exige que ela seja fornecida externamente.

O servidor tambem publica os catalogos negativos em
`/zephyros/apps/invalid/<nome>.zac` e os catalogos de replay em
`/zephyros/apps/seed/stable.zac` e `/zephyros/apps/update/stable.zac`. Assim,
`store remote check --url URL` exercita a matriz criptografica no QEMU sem
copiar esses artefatos para a imagem.

## Validacao concluida

O AS1 foi validado no host e no QEMU em 29/07/2026:

1. Autotestes, auditoria deterministica dos fixtures, `q3check` e
   `git diff --check` passaram.
2. Os seis aliases apareceram em ordem lexical, com tres fontes validas, tres
   invalidas e os estados e motivos previstos.
3. `SAMEVER` percorreu `AVAILABLE -> SAME_VERSION -> AVAILABLE` durante
   instalacao e remocao, sem alterar o arquivo `.ZPK` fonte.
4. Quatro refreshes com o catalogo populado mantiveram a memoria usada em
   `20680 KB`.
5. `health summary`, `pkgcheck`, `appcheck`, `memcheck` e `regcheck full`
   concluiram em `OK`, inclusive com o recovery da App Store em `DEGRADED`.
6. A regressao final confirmou ausencia de processos ring 3, zumbis,
   diretorios parciais e pacotes instalados residuais.

Durante a matriz, o `appcheck` revelou pressao de stack causada por copias
aninhadas de `app_launch_info_t`. O loader, o processo e o Shell passaram a
usar buffers internos serializados; depois da correcao, o demonstrativo ZAPP,
o retorno de foco e as migracoes de `uptime` e `mem` concluiram normalmente.

## Validacao AS2

O AS2 foi validado no host e no QEMU em 30/07/2026. O codigo usa workspace
estatico no Shell para o resultado de acao e `app_launch_info_t`. Autoteste,
auditorias AS1/AS2, `q3check`, `git diff --check` e build limpo passaram.

A matriz manual confirmou:

1. preflights sem escrita e motivos esperados para pacote invalido, alias
   divergente e dependencia ausente;
2. instalacao somente com `--confirm` e recusa de uma segunda instalacao;
3. execucao instalada de `WAITAPP` com argumentos, cancelamento por `F12` e
   devolucao de foco ao Shell sem processo ring 3 ou zumbi;
4. preflight de remocao sem alteracao e remocao confirmada;
5. bloqueio da remocao de `BASE` enquanto `DEPEND` estava instalado, inclusive
   com `--confirm`, seguido da remocao segura na ordem inversa;
6. `health summary`, `pkgcheck`, `appcheck`, `memcheck` e `regcheck full`
   concluidos, com memoria estavel em `20680 KB`;
7. estado final sem pacotes instalados, diretorios parciais ou processos
   residuais.

## Validacao AS3

O AS3 foi validado pelo usuario no host e no QEMU em 01/08/2026. A imagem de
teste conteve as nove fontes AS1/AS2, com seis validas e tres invalidas. A
matriz confirmou catalogo, detalhes, `F5`, verificacao de `VALID` e recusa de
`BADCRC`, preflight de dependencia, confirmacao, instalacao e remocao sem
diretorios parciais.

`WAITAPP` foi aberto pela Store, a janela foi fechada para deixar o ZAPP em
primeiro plano e `F12` o cancelou com retorno de foco ao Shell. A confirmacao
contextual, a reabertura singleton pelo Menu Iniciar/taskbar e o smoke test
em `guimode simple` tambem passaram. Ao final, `health summary`, `pkgcheck`,
`appcheck`, `memcheck` e `regcheck full` confirmaram catalogo degradado apenas
pelos fixtures invalidos, sem pacote instalado, processo ring 3, zumbi ou
vazamento residual.

## Validacao AS4

O AS4 foi validado pelo usuario no host e no QEMU em 01/08/2026. `q3check`,
build limpo e os alvos AS4 passaram. A matriz confirmou instalacao inicial,
o plano topologico `UPDEPB -> UPDEPA -> UPTARGET`, atualizacao para `1.1.0`,
rollback para `1.0.0` e downgrade diagnostico somente com `--downgrade` e
`--confirm`.

`BROKEN.ZPK` foi recusado como `PLAN_INCOMPLETE` e `CYCLEA.ZPK` como
`PLAN_CYCLE`. Failpoints apos a primeira e a quinta troca deixaram journal
pendente; depois do reboot, a recuperacao restaurou o estado anterior,
registrou `RECOVERY`, limpou o journal e manteve o heap integro. A interface
Classic repetiu update e rollback, habilitou `Reverter` somente quando havia
backup e preservou `UPTARGET` selecionado depois do refresh por `F5`.

## Validacao AS5 pendente

O codigo, fixtures e documentacao estao prontos para os gates host e a matriz
QEMU. A fase somente sera marcada como validada depois de `q3check`, build
limpo, alvos AS5, consulta/download/cancelamento, instalacao offline, update,
rollback, falhas criptograficas, replay, failpoint com reboot e diagnosticos
finais executados pelo usuario.

## Limitacoes

- um unico publicador e chave publica de teste; ainda sem raiz oficial;
- HTTP sem TLS; autenticidade e integridade dependem de Ed25519 e SHA-256;
- sem consulta, download, instalacao ou atualizacao automatica;
- sem conta, pagamento, recomendacao ou telemetria;
- dependencias ja instaladas nao recebem atualizacao implicita;
- apenas uma versao anterior por aplicativo atualizado fica recuperavel;
- cache/mutacoes AS5 e transacoes AS4 ficam indisponiveis no FAT32;
- sem banco de dados proprio ou persistencia do snapshot;
- sem mudanca de ZPKG v1, App API `0.3`, loader ou boot.

## Referencias

- [Pacotes locais ZPKG v1](pacotes.md)
- [Roadmap 06 - App Store](../roadmaps/06-app-store.md)
- [Comandos do Shell](../09-shell/comandos.md)
- [Catalogo de contratos publicos](../qualidade/contratos-publicos.md)
