# Pacotes locais `.zephyrosapp`

## Escopo da Fase 7

O primeiro formato de distribuicao local do ZephyrOS transporta exatamente uma
imagem ZAPP e seu manifesto. O artefato no host usa a extensao
`.zephyrosapp`; dentro da imagem FAT12 ele e gravado com o alias 8.3
`ID.ZPK`. Os dois arquivos contem os mesmos bytes.

Esta fase nao adiciona syscall, nao altera a App API `0.3` e nao define
assinatura, rede, atualizacao, rollback, permissoes, GUI/App Store, icones ou
multiplos arquivos por pacote.

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
api=0.3
entry=APP.ZAP
dependencies=
```

Os campos obrigatorios sao `id`, `name`, `version`, `api`, `entry` e
`dependencies`. O `id` tem 1 a 8 caracteres em `A-Z`, `0-9` ou `_`; ele define
o diretorio instalado e o alias FAT. `version` usa `MAJOR.MINOR.PATCH`,
`api` e sempre `0.3`, e `entry` e sempre `APP.ZAP`. `dependencies` e vazio ou
lista ate quatro IDs separados por virgula, sem versoes, repeticoes ou
auto-dependencia.

Antes de qualquer escrita, o kernel valida header, tamanhos, arquitetura,
manifesto, CRC32 e o payload pelo validador ZAPP existente.

## Fluxo no host

`tools/packager.py` usa apenas a biblioteca padrao do Python:

```text
python tools\packager.py build --manifest app.json --zapp APP.ZAP --output DEMO.zephyrosapp
python tools\packager.py verify DEMO.zephyrosapp
python tools\packager.py inject --package DEMO.zephyrosapp --image build\zephyros.img
python tools\packager.py inject --package DEMO.zephyrosapp --image build\zephyros.img --replace
```

O `app.json` precisa de `id`, `name` e `version`; `api` assume `0.3` e
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

`package-test` executa o autoteste com criacao, corrupcao de CRC32 e injecao
em uma imagem temporaria. `package-demo` depende da imagem ja construida,
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
```

`META.DAT` preserva o manifesto validado. A instalacao recusa ID ja instalado,
dependencia ausente, aplicativo em primeiro plano, servicos indisponiveis ou
espaco insuficiente. Em falha de escrita, tenta remover `APP.ZAP`, `META.DAT`
e o diretorio parcial. A remocao e bloqueada se outro pacote instalado
depender do ID; o arquivo-fonte `ID.ZPK` no diretorio raiz nunca e apagado.

O loader aceita caminhos; assim uma instalacao pode ser executada com:

```text
app run APPS/DEMO/APP.ZAP
```

## Comandos

| Comando | Acao |
|---|---|
| `pkg list` | Lista pacotes instalados a partir dos diretorios `APPS/<ID>`. |
| `pkg info <ID|arquivo.ZPK>` | Mostra manifesto instalado ou valida o arquivo informado antes de exibir seus dados. |
| `pkg verify <arquivo.ZPK>` | Valida o pacote fonte sem gravar. |
| `pkg install <arquivo.ZPK>` | Valida e instala uma unica imagem ZAPP. |
| `pkg remove <ID>` | Remove os arquivos instalados depois de verificar dependentes. |
| `pkgcheck` | Diagnostico sem escrita para pacote invalido, dependencia ausente e espaco insuficiente. |

`pkgcheck` nao substitui `appcheck`; ele cobre apenas as pre-validacoes locais
do servico de pacotes.

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
modos `guimode classic` e `guimode modern`. Ao fim, `DEMO.ZPK` deve continuar
no diretorio raiz, enquanto `pkg list` volta a ficar vazio e nao ha processo
ring 3 ou zumbi residual.

## Validacao registrada

A Fase 7 foi validada no host com `q3check`, `package-test`, build limpo e
`package-demo`; no QEMU foram confirmados `health`, verificacao, instalacao,
execucao por caminho, remocao, `pkgcheck`, `appcheck`, `q2check`, `F12`,
`procs` e os modos classico e moderno. Nenhum ZAPP ou zumbi permaneceu apos os
fluxos cobertos.
