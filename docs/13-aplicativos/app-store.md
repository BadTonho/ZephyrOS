# App Store - Catalogo local AS1

## Resumo de progresso

O AS1 implementa o catalogo local somente-leitura da App Store sobre o
container `ZPKG v1` e o servico `PKG` existentes. O codigo, os comandos Shell
e os fixtures host foram validados no host e no QEMU. A fase esta
**concluida e validada**.

Esta etapa nao instala, remove, atualiza nem executa pacotes pelo comando
`store`. Essas mutacoes pertencem ao AS2. A interface nativa Classic/Modern
pertence ao AS3.

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

`INSUFFICIENT_SPACE` esta reservado para o preflight AS2. No AS1, as
capacidades `INSTALL` e `UPDATE` descrevem apenas a classificacao do catalogo;
qualquer mutacao futura repetira o preflight completo.

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
```

`store-test` executa o autoteste do empacotador e audita os fixtures
versionados. `store-demo` injeta somente a matriz AS1 com substituicao
idempotente e nao participa do build normal.

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

## Limitacoes

- sem instalacao, remocao, execucao ou atualizacao por `store`;
- sem resolucao automatica de dependencias;
- sem assinatura, repositorio remoto, conta ou telemetria;
- sem banco de dados proprio ou persistencia do snapshot;
- sem interface App Store Classic/Modern;
- sem mudanca de ZPKG v1, App API `0.3`, loader ou boot.

## Referencias

- [Pacotes locais ZPKG v1](pacotes.md)
- [Roadmap 06 - App Store](../roadmaps/06-app-store.md)
- [Comandos do Shell](../09-shell/comandos.md)
- [Catalogo de contratos publicos](../qualidade/contratos-publicos.md)
