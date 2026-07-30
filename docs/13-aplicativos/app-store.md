# App Store - Catalogo AS1 e ciclo de vida AS2

## Resumo de progresso

O AS1 implementa o catalogo local somente-leitura sobre `ZPKG v1` e o servico
`PKG`; ele esta concluido e validado. O AS2 acrescenta preflight, confirmacao,
instalacao, remocao e execucao pelo Shell. O AS2 esta concluido e validado no
host e no QEMU.

A interface nativa Simple/Classic/Modern pertence ao AS3. Atualizacao, downgrade e
resolucao automatica de dependencias continuam fora desta fase.

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
| `store status` | Atualiza e mostra recovery, contagens, limites e motivo geral. |
| `store list` | Atualiza e lista entradas em ordem deterministica. |
| `store info <ID\|alias.ZPK>` | Mostra manifesto, versoes, confianca, dependencias e capacidades. |
| `store install <alias.ZPK>` | Executa preflight sem escrita e mostra o comando de confirmacao. |
| `store install <alias.ZPK> --confirm` | Repete o preflight e instala o pacote. |
| `store remove <ID>` | Executa preflight de remocao sem escrita. |
| `store remove <ID> --confirm` | Repete o preflight e remove o pacote. |
| `store run <ID> [args]` | Executa somente o ZAPP instalado; F12 cancela. |

Todo pacote fonte e apresentado como `LOCAL / NAO ASSINADO`. O comando sem
subcomando mostra somente o uso; abrir a interface grafica fica para AS3.

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
seis aliases ou hashes canonicos do AS1. Nenhum alvo participa do build normal.

### Fixtures AS2

Os artefatos de ciclo de vida ficam em `docs/fixtures/apps/store-as2/`:

| Alias | Cobertura |
|---|---|
| `WAITAPP.ZPK` | Instalacao, argumentos, execucao persistente e F12. |
| `BASE.ZPK` | Dependencia instalavel sem requisitos. |
| `DEPEND.ZPK` | Depende de `BASE` e bloqueia sua remocao. |

`fixtures-store-as2` gera a matriz; `audit-store-as2` confere nomes, hashes,
manifestos, payload de espera, dependencia e, opcionalmente, os bytes FAT12.

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

Com AS1, AS2, MV0, MV0.1, MV1 e MV2 aprovados, o proximo passo oficial e o
MV3 do Roadmap 07 antes do AS3.

## Limitacoes

- sem atualizacao ou downgrade por `store`;
- sem resolucao automatica de dependencias;
- sem assinatura, repositorio remoto, conta ou telemetria;
- sem banco de dados proprio ou persistencia do snapshot;
- sem interface App Store Simple/Classic/Modern;
- sem mudanca de ZPKG v1, App API `0.3`, loader ou boot.

## Referencias

- [Pacotes locais ZPKG v1](pacotes.md)
- [Roadmap 06 - App Store](../roadmaps/06-app-store.md)
- [Comandos do Shell](../09-shell/comandos.md)
- [Catalogo de contratos publicos](../qualidade/contratos-publicos.md)
