# Pacotes locais `.zephyrosapp`

## Escopo da Fase 7

O primeiro formato de distribuicao local do ZephyrOS transporta exatamente uma
imagem ZAPP e seu manifesto. O artefato no host usa a extensao
`.zephyrosapp`; dentro da imagem FAT12 ele e gravado com o alias 8.3
`ID.ZPK`. Os dois arquivos contem os mesmos bytes.

O AS1 nao adicionou syscall, nao alterou a App API `0.3` nem definiu
assinatura, rede, atualizacao, rollback, permissoes, GUI/App Store, icones ou
multiplos arquivos por pacote. AS4 acrescenta apenas atualizacao local FAT12,
rollback e historico; o container e a App API continuam inalterados.

## Container ZPKG v1

O arquivo usa a sequencia exata abaixo:

```text
[header de 32 bytes][manifesto ASCII][payload ZAPP]
```

O header e little-endian e contem:

| Offset | Campo | Tipo | Regra |
|---|---|---|---|
| 0 | magic | 4 bytes | `ZPKG` |
| 4 | version | `uint16_t` | `1` |
| 6 | header_size | `uint16_t` | `32` |
| 8 | architecture | `uint32_t` | i386 (`1`) |
| 12 | manifest_size | `uint32_t` | 1 a 512 bytes |
| 16 | payload_size | `uint32_t` | imagem ZAPP valida, ate 8236 bytes |
| 20 | content_crc32 | `uint32_t` | CRC32 de manifesto mais payload |
| 24 | flags | `uint32_t` | zero nesta versao |
| 28 | reserved | `uint32_t` | zero nesta versao |

O CRC32 detecta corrupcao acidental. Ele nao autentica o autor e nao substitui
uma assinatura criptografica.

O manifesto e ASCII, com uma linha `chave=valor` por campo, nesta ordem:

```text
id=DEMO
name=Demo local
version=1.0.0
api=0.9
entry=APP.ZAP
dependencies=
```

Os campos obrigatorios sao `id`, `name`, `version`, `api`, `entry` e
`dependencies`. O `id` tem 1 a 8 caracteres em `A-Z`, `0-9` ou `_`; ele define
o diretorio instalado e o alias FAT. `version` usa `MAJOR.MINOR.PATCH`,
`api` usa `0.9` em pacotes novos; `0.3`, `0.4`, `0.5`, `0.6`, `0.7` e `0.8`
continuam aceitos por compatibilidade append-only. `entry` e sempre `APP.ZAP`. `dependencies` e vazio ou
lista ate quatro IDs separados por virgula, sem versoes, repeticoes ou
auto-dependencia.

Antes de qualquer escrita, o kernel valida header, tamanhos, arquitetura,
manifesto, CRC32 e o payload pelo validador ZAPP existente.

## Container ZPKG v2 e confiança SEC4

O ZPKG v2 mantém o manifesto e o payload ZAPP, mas acrescenta autenticação
individual Ed25519. O header é fixo em 128 bytes e o layout é little-endian:

| Offset | Campo | Regra |
|---|---|---|
| 0 | `magic` | `ZPKG` |
| 4 | `version` | `2` |
| 6 | `header_size` | `128` |
| 8 | `architecture` | i386 (`1`) |
| 12 | `manifest_size` | 1 a 512 bytes |
| 16 | `payload_size` | ZAPP válido, até 8236 bytes |
| 20 | `content_crc32` | CRC32 de manifesto mais payload |
| 24 | `flags` | somente `SIGNED` (`1`) |
| 28 | `reserved` | zero |
| 32 | `signature_offset` | imediatamente após manifesto e payload |
| 36 | `signature_size` | `64` |
| 38 | `signature_algorithm` | Ed25519 (`1`) |
| 40 | `key_id` | primeiros 16 bytes de SHA-256 da chave pública |
| 56 | `content_sha256` | SHA-256 de manifesto mais payload |
| 88 | `reserved_tail` | 40 bytes obrigatoriamente zero |

O arquivo é `[header][manifesto][payload][assinatura]`. A assinatura cobre
`APP_PACKAGE_TRUST_DOMAIN || header || manifesto || payload`; o único trecho
excluído é a própria assinatura. CRC32 e SHA-256 são verificados antes da
assinatura para vincular o envelope ao conteúdo exato.

A raiz de confiança é exclusiva de pacotes e está declarada em
`src/include/core/app_package_trust.h`. A SEC4 aceita somente Ed25519, uma
chave ativa estática e uma lista estática de `key_id` revogados. A chave
privada nunca é versionada: o empacotador recebe-a por `--private` durante a
geração de distribuição.

O parser v1 continua disponível para inspeção. Um v1 é reportado como
`UNSIGNED` e pode ser listado ou removido quando já estiver instalado, mas não
pode ser instalado, atualizado, executado, usado em rollback ou aplicado em
plano local/remoto. Os estados públicos de confiança são `UNSIGNED`,
`TRUSTED`, `UNKNOWN_KEY`, `REVOKED_KEY`, `INVALID_SIGNATURE`,
`HASH_MISMATCH` e `INVALID`. Os motivos correspondentes são append-only:
`PACKAGE_UNAUTHORIZED`, `UNKNOWN_KEY`, `REVOKED_KEY`, `SIGNATURE_INVALID` e
`HASH_MISMATCH`.

## Fluxo no host

`tools/packager.py` usa apenas a biblioteca padrao do Python:

```text
python tools\packager.py build --manifest app.json --zapp APP.ZAP --output DEMO.zephyrosapp
python tools\packager.py build --manifest app.json --zapp APP.ZAP --private operador-ed25519.pem --output DEMO.zephyrosapp
python tools\packager.py build --manifest app.json --zapp APP.ZAP --legacy --output DEMO-v1.zephyrosapp
python tools\packager.py verify DEMO.zephyrosapp
python tools\packager.py inject --package DEMO.zephyrosapp --image build\zephyros.img
python tools\packager.py inject --package DEMO.zephyrosapp --image build\zephyros.img --replace
```

O `build` exige uma chave privada externa para gerar ZPKG v2. `--legacy` é a
única forma explícita de gerar ZPKG v1 para compatibilidade. O `app.json` precisa de `id`, `name` e `version`; `api` assume `0.9`, aceita
explicitamente `0.3`, `0.4`, `0.5`, `0.6`, `0.7` e `0.8` para pacotes legados e
`dependencies` assume lista vazia quando omitidos. O `inject` deriva o alias
`ID.ZPK`, recusa alias invalido, arquivo ja existente, diretorio raiz cheio,
imagem FAT12 invalida e falta de clusters. Ele somente inicializa os bytes FAT
necessarios quando a FAT estiver vazia. A substituicao de um alias existente
exige `--replace`; nesse caso a cadeia FAT anterior e liberada antes da nova
gravacao.

Os atalhos de desenvolvimento sao:

```text
make package-test
make package-demo
```

`package-test` executa o autoteste com criacao, compatibilidade das APIs 0.3,
0.4, 0.5, 0.6, 0.7, 0.8 e 0.9, corrupcao de CRC32 e injecao em uma imagem temporaria. `package-demo`
depende da imagem ja construida,
gera `build\DEMO.zephyrosapp` e injeta `DEMO.ZPK`; ele nao faz parte de
`make` normal.

## Instalacao no ZephyrOS

O servico interno `PKG` e inicializado somente quando filesystem e loader ZAPP
estao prontos. O `health` mostra `Pacotes: READY` ou `DISABLED`.

Os diretorios instalados sao o registro persistente, sem expor a estrutura FAT
ao Shell:

```text
APPS/<ID>/APP.ZAP
APPS/<ID>/META.DAT
APPS/<ID>/AUTH.DAT
```

`META.DAT` preserva o manifesto validado. `AUTH.DAT` preserva o envelope v2
necessário para revalidar a assinatura, o hash, o CRC, o `key_id` e os limites
do pacote. Antes de executar, `APP.ZAP`, `META.DAT` e `AUTH.DAT` são validados
conjuntamente; qualquer alteração, ausência, arquivo inesperado ou corrupção
bloqueia a execução. A autorização é uma cópia revalidável da assinatura
aprovada na instalação, não um bypass.

A instalacao recusa ID ja instalado,
dependencia ausente, aplicativo em primeiro plano, servicos indisponiveis ou
espaco insuficiente. Em falha de escrita, tenta remover `APP.ZAP`, `META.DAT`,
`AUTH.DAT` e o diretorio parcial. Staging, journal, backup, rollback, limpeza
parcial e remoção incluem os três arquivos. O failpoint AS4 usa até 48 pontos
de troca para refletir esse terceiro arquivo. A remocao e bloqueada se outro pacote instalado
depender do ID; o arquivo-fonte `ID.ZPK` no diretorio raiz nunca e apagado.

O comando `app run APPS/<ID>/APP.ZAP` é reconhecido como pacote instalado e
encaminhado a `app_package_run_installed()`, sem bypass do controle de
confiança. Imagens ZAPP internas dos serviços nativos continuam usando
`app_loader_run_image()`. Assim uma instalacao pode ser executada com:

```text
app run APPS/DEMO/APP.ZAP
```

## Ciclo de vida publico AS2

O AS2 acrescenta preflight e confirmacao sem alterar `ZPKG v1`. As consultas
`app_package_preflight_install()` e `app_package_preflight_remove()` releem o
estado real, preenchem `app_package_action_result_t` e nunca gravam.
`app_package_install_confirmed()` e `app_package_remove_confirmed()` adquirem
um gate global de mutacao e repetem o mesmo preflight antes da primeira
escrita. As operacoes administrativas antigas continuam disponiveis e tambem
passam pelo gate.

O resultado contem manifesto, clusters necessarios/livres e ate 32 IDs
bloqueadores em ordem lexical. Dependencias ausentes tem prioridade sobre
`ALREADY_INSTALLED`; dependentes reversos impedem a remocao. Os motivos
append-only sao:

```text
NONE
INVALID_ARGUMENT
SOURCE_NOT_FOUND
PACKAGE_INVALID
ALIAS_MISMATCH
DEPENDENCY_MISSING
INSUFFICIENT_SPACE
ALREADY_INSTALLED
NOT_INSTALLED
DEPENDENT_INSTALLED
FILESYSTEM_UNAVAILABLE
LOADER_UNAVAILABLE
PACKAGE_SERVICE_UNAVAILABLE
LOADER_BUSY
MUTATION_BUSY
READ_ERROR
WRITE_ERROR
PACKAGE_UNAUTHORIZED
UNKNOWN_KEY
REVOKED_KEY
SIGNATURE_INVALID
HASH_MISMATCH
```

`app_package_run_installed()` aceita somente um ID instalado, monta
`APPS/<ID>/APP.ZAP` internamente e entrega os argumentos ao loader existente.
Mutacoes sao recusadas enquanto um ZAPP externo estiver em primeiro plano;
execucao tambem e recusada durante uma mutacao.

As consultas públicas de confiança são `app_package_verify_file()`,
`app_package_verify_installed()` e `app_package_trust_name()`. A primeira
classifica a fonte sem gravar; a segunda valida o trio persistido antes da
execução. Uma instalação v2 só publica o estado `TRUSTED`; uma instalação v1
legada permanece `UNSIGNED` e somente pode ser consultada/removida.

## Atualizacao transacional AS4

AS4 preserva a leitura do container `ZPKG v1` e centraliza a comparacao de versao em
`app_package_compare_versions()`. Planos possuem ate 16 entradas e sao
revalidados integralmente no preflight e na confirmacao. Uma instalacao pode
incluir dependencias locais transitivas em ordem topologica; uma atualizacao
altera somente o alvo e nao atualiza dependencias ja instaladas.

No FAT12, staging, backup, journal, estado e historico usam aliases privados
hidden/system. `APP.ZAP`, `META.DAT` e `AUTH.DAT` sao escritos copy-on-write dentro de
`APPS/<ID>`. O journal e recuperado em `app_package_init()` antes do catalogo;
falha de journal bloqueia mutacoes e preserva consultas. FAT32 recusa a
mutacao AS4 sem escrita. Cada app atualizado conserva uma unica versao
anterior recuperavel; `rollback` restaura e consome somente a copia daquele
app. A tabela persistente comporta os 32 IDs expostos pelo catalogo.

Os novos motivos append-only incluem `PLAN_INCOMPLETE`, `PLAN_CYCLE`,
`PLAN_CONFLICT`, `DOWNGRADE_REQUIRES_CONFIRM`, `TRANSACTION_UNAVAILABLE`,
`TRANSACTION_PENDING`, `ROLLBACK_UNAVAILABLE`, `RECOVERY_FAILED` e
`HISTORY_UNAVAILABLE`. `app_package_status_t` informa suporte, journal, a
tabela compacta de rollbacks por app e o ultimo registro de historico.

No ciclo de jobs da Fase 5, verificacao, instalacao e remocao continuam
usando essas APIs sincrônicas como wrappers compatíveis. O Shell so publica
`CANCELLED` depois que o ponto seguro da transacao retorna e o journal,
backup ou rollback pendente foi drenado; falha de recovery permanece
`FAILED`. `app_package_status_t.operation_generation` identifica a mutacao
mais recente e evita associar status de uma transacao antiga ao job seguinte.

## Fonte de plano em diretorio AS5

AS5 preserva a leitura de `ZPKG v1` e acrescenta somente duas APIs publicas
append-only:

```text
app_package_preflight_plan_from_directory()
app_package_apply_plan_from_directory_confirmed()
```

Elas recebem um plano ja construido e o alias de um diretorio FAT12 privado.
Cada entrada ainda passa pelo parser ZPKG, CRC32, manifesto, ID, versao,
dependencias, ZAPP, espaco, loader e gate de mutacao do servico. As APIs AS1 a
AS4 continuam chamando o mesmo motor com a raiz como fonte. O servico remoto
usa essas operacoes somente depois de validar assinatura do catalogo,
SHA-256 publicado e assinatura individual ZPKG v2 dos pacotes em cache;
`app_package` conhece somente a raiz de pacotes, não HTTP nem a tabela de
confiança AS5.

## Comandos

| Comando | Acao |
|---|---|
| `pkg list` | Lista pacotes instalados a partir dos diretorios `APPS/<ID>`. |
| `pkg info <ID|arquivo.ZPK>` | Mostra manifesto instalado ou valida o arquivo informado antes de exibir seus dados. |
| `pkg verify <arquivo.ZPK>` | Valida o pacote fonte sem gravar. |
| `pkg install <arquivo.ZPK>` | Valida e instala uma unica imagem ZAPP. |
| `pkg remove <ID>` | Remove os arquivos instalados depois de verificar dependentes. |
| `pkgcheck` | Diagnostico sem escrita para pacote invalido, dependencia ausente, espaco insuficiente e serializacao. |

`pkgcheck` nao substitui `appcheck`; ele cobre apenas as pre-validacoes locais
do servico de pacotes. O caso de espaco usa o mesmo calculo do preflight AS2
com geometria sintetica, e o teste do gate nao cria ou remove arquivos.

O catalogo somente-leitura construido sobre este servico e documentado em
[`app-store.md`](app-store.md).

## Validacao manual

Depois de `make package-demo`, execute no QEMU:

```text
health
pkg info DEMO.ZPK
pkg verify DEMO.ZPK
pkg install DEMO.ZPK
pkg list
app run APPS/DEMO/APP.ZAP
pkg remove DEMO
pkg list
pkgcheck
```

Em seguida confirme `appcheck`, `q2check`, `procs`, o cancelamento `F12` e os
modos `guimode simple` e `guimode classic`. Ao fim, `DEMO.ZPK` deve continuar
no diretorio raiz, enquanto `pkg list` volta a ficar vazio e nao ha processo
ring 3 ou zumbi residual.

## Validacao registrada

A Fase 7 foi validada no host com `q3check`, `package-test`, build limpo e
`package-demo`; no QEMU foram confirmados `health`, verificacao, instalacao,
execucao por caminho, remocao, `pkgcheck`, `appcheck`, `q2check`, `F12`,
`procs` e os modos Simple e Classic. Nenhum ZAPP ou zumbi permaneceu apos os
fluxos cobertos.
