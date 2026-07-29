# Contrato de Atualizacao ZUPD v1

## Escopo

O ZUPD v1 e o container autenticado de atualizacoes de arquivos do sistema.
No host, o artefato usa a extensao `.zephyrosupd`; em volumes FAT ele usa um
alias 8.3 com extensao `.ZUP`. O magic interno e `ZUPD`.

Este contrato e separado do ZPKG v1 de aplicativos. O ZUPD v1 nao altera a
App API, nao transporta pacotes `.zephyrosapp` e nao reutiliza o servico
`PKG`.

A primeira allowlist contem somente:

- `EXPLORER.BMP`;
- `SHELL.BMP`;
- `TASKMGR.BMP`.

Cada alvo deve existir e somente pode ser substituido. Criacao, exclusao,
renomeacao, setores crus, boot, stage2 e kernel ficam fora do formato v1.

## Codificacao e limites

Todos os inteiros usam little-endian. Estruturas sao empacotadas, sem padding
implicito. Bytes reservados devem ser zero.

| Item | Regra |
|---|---|
| Arquitetura | i386 (`1`) |
| Tamanho total | 321 a 131072 bytes (128 KiB) |
| Entradas | 1 a 16 |
| Tamanho de entrada | 128 bytes |
| Tamanho individual | 1 a 65536 bytes (64 KiB) |
| Caminho | ASCII, NUL-terminated em 64 bytes |
| Compressao | nenhuma (`0`) |
| Operacao | substituir arquivo existente (`1`) |
| Classe de alvo | arquivo regular do sistema (`1`) |

O layout exato e:

```text
[header de 128 bytes]
[tabela ordenada com N entradas de 128 bytes]
[payloads contiguos na mesma ordem]
[assinatura Ed25519 de 64 bytes]
```

Nao existem lacunas entre as regioes. A tabela e ordenada pelo valor ASCII do
caminho. Caminhos repetidos, offsets sobrepostos ou dados fora da regiao
declarada invalidam o artefato.

## Header de 128 bytes

| Offset | Campo | Tipo | Regra |
|---:|---|---|---|
| 0 | `magic` | `char[4]` | `ZUPD` |
| 4 | `format_version` | `uint16_t` | `1` |
| 6 | `header_size` | `uint16_t` | `128` |
| 8 | `architecture` | `uint32_t` | i386 (`1`) |
| 12 | `flags` | `uint32_t` | zero |
| 16 | `total_size` | `uint32_t` | tamanho exato do artefato |
| 20 | `manifest_offset` | `uint32_t` | `128` |
| 24 | `manifest_size` | `uint32_t` | `entry_count * 128` |
| 28 | `payload_offset` | `uint32_t` | `128 + manifest_size` |
| 32 | `payload_size` | `uint32_t` | soma dos payloads |
| 36 | `signature_offset` | `uint32_t` | `payload_offset + payload_size` |
| 40 | `signature_size` | `uint16_t` | `64` |
| 42 | `signature_algorithm` | `uint16_t` | Ed25519 (`1`) |
| 44 | `content_hash_algorithm` | `uint16_t` | SHA-256 (`1`) |
| 46 | `entry_count` | `uint16_t` | 1 a 16 |
| 48 | `entry_size` | `uint16_t` | `128` |
| 50 | `reserved0` | `uint16_t` | zero |
| 52 | `base_major` | `uint16_t` | versao base |
| 54 | `base_minor` | `uint16_t` | versao base |
| 56 | `base_patch` | `uint16_t` | versao base |
| 58 | `target_major` | `uint16_t` | versao alvo |
| 60 | `target_minor` | `uint16_t` | versao alvo |
| 62 | `target_patch` | `uint16_t` | versao alvo |
| 64 | `base_epoch` | `uint32_t` | epoch exigido |
| 68 | `target_epoch` | `uint32_t` | epoch apos aplicacao |
| 72 | `key_id` | `uint8_t[16]` | primeiros 16 bytes de SHA-256 da chave publica |
| 88 | `content_sha256` | `uint8_t[32]` | SHA-256 da tabela mais payloads |
| 120 | `reserved` | `uint8_t[8]` | zero |

O baseline do sistema atual e `0.1.0`, epoch `0`. O sistema so aceita um
artefato quando a versao e o epoch atuais coincidem exatamente com os campos
base. A versao alvo deve ser lexicograficamente maior que a base e o epoch
alvo deve ser maior ou igual ao epoch base.

## Entrada de 128 bytes

| Offset | Campo | Tipo | Regra |
|---:|---|---|---|
| 0 | `path` | `char[64]` | nome da allowlist, seguido de NUL e zeros |
| 64 | `payload_offset` | `uint32_t` | offset absoluto no artefato |
| 68 | `payload_size` | `uint32_t` | 1 a 65536 |
| 72 | `installed_size` | `uint32_t` | igual a `payload_size` no v1 |
| 76 | `operation` | `uint16_t` | substituir (`1`) |
| 78 | `compression` | `uint16_t` | nenhuma (`0`) |
| 80 | `target_class` | `uint16_t` | arquivo do sistema (`1`) |
| 82 | `flags` | `uint16_t` | zero |
| 84 | `payload_sha256` | `uint8_t[32]` | SHA-256 do payload desta entrada |
| 116 | `reserved` | `uint8_t[12]` | zero |

O primeiro `payload_offset` deve ser igual ao `payload_offset` do header.
Cada offset seguinte deve ser exatamente o fim do payload anterior. A soma
deve terminar em `signature_offset`.

## Assinatura e raiz de confianca

O ZUPD v1 usa Ed25519 conforme o RFC 8032. A mensagem assinada e a
concatenacao:

```text
"ZEPHYROS-UPDATE-V1\0" || artefato[0:signature_offset]
```

O separador de dominio possui 19 bytes:

```text
5A45504859524F532D5550444154452D563100
```

O campo `key_id` seleciona uma chave publica confiavel por comparacao exata.
A chave publica nao e transportada pelo artefato. O kernel nunca recebe nem
armazena a chave privada.

A U1 usa somente chaves publicas de teste do RFC 8032. A chave de producao
sera gerada offline pelo mantenedor na U2; apenas sua chave publica e seu
`key_id` poderao entrar no repositorio.

O v1 possui uma unica chave de release e nao permite rotacao ou revogacao
automatica. Se essa chave for comprometida, a recuperacao exige uma nova
imagem confiavel instalada manualmente, com nova chave publica e epoch
incrementado.

## Ordem de validacao

O verificador da U2 devera usar esta ordem para produzir resultados
deterministicos:

1. validar magic, versao do formato, algoritmos, limites, tamanhos, campos
   reservados, aritmetica de offsets, ordenacao, caminhos e sobreposicoes;
2. validar `content_sha256` e o SHA-256 individual de cada payload;
3. localizar a chave pelo `key_id` e validar a assinatura Ed25519;
4. validar arquitetura, versoes, epochs e allowlist do sistema atual.

Nenhum dado e gravado durante a verificacao.

## Motivos de rejeicao

Os motivos abaixo formam o diagnostico publico planejado para U2. Eles nao
adicionam constantes a `errors.h` durante a U1.

| Valor | Motivo | Retorno generico futuro |
|---:|---|---|
| 0 | `ZUPD_REASON_NONE` | `OK` |
| 1 | `ZUPD_REASON_FORMAT` | `ERR_INVALID` |
| 2 | `ZUPD_REASON_SIZE` | `ERR_OVERFLOW` |
| 3 | `ZUPD_REASON_HASH` | `ERR_INVALID` |
| 4 | `ZUPD_REASON_UNKNOWN_KEY` | `ERR_INVALID` |
| 5 | `ZUPD_REASON_SIGNATURE` | `ERR_INVALID` |
| 6 | `ZUPD_REASON_ARCHITECTURE` | `ERR_STATE` |
| 7 | `ZUPD_REASON_BASE_VERSION` | `ERR_STATE` |
| 8 | `ZUPD_REASON_DOWNGRADE` | `ERR_STATE` |
| 9 | `ZUPD_REASON_PATH_POLICY` | `ERR_INVALID` |
| 10 | `ZUPD_REASON_DUPLICATE_TARGET` | `ERR_INVALID` |
| 11 | `ZUPD_REASON_UNSUPPORTED` | `ERR_UNAVAILABLE` |

Falhas de abertura ou leitura continuam usando `ERR_NOT_FOUND` ou `ERR_DISK`
fora do validador estrutural.

## Estado futuro no `health`

A U1 nao adiciona um componente ao kernel. A partir da U2, o componente
`Update` devera observar:

- `READY`: verificador local, autoteste criptografico e chave publica prontos;
- `DEGRADED`: verificacao local utilizavel, mas uma capacidade auxiliar
  implementada registrou falha;
- `DISABLED`: chave ausente/invalida ou autoteste criptografico falhou.

Capacidades ainda nao implementadas nao degradam o componente. Verificacao,
aplicacao, rollback e acesso remoto devem aparecer separadamente. O remoto
permanece `DISABLED` por padrao e nao altera a disponibilidade local.

## Politica de seguranca

O contrato cobre:

- corrupcao ou adulteracao do artefato;
- artefatos sem a chave privada confiavel;
- chave desconhecida, arquitetura errada e versao incompativel;
- downgrade apresentado ao sistema em execucao;
- truncamento, overflow, sobreposicao, duplicacao e confusao de caminhos.

O contrato nao cobre:

- imagem inicial, kernel em execucao ou chave privada comprometidos;
- rollback offline do disco inteiro;
- Secure Boot ou autenticacao do bootloader;
- confidencialidade dos payloads;
- negacao de servico por remocao ou indisponibilidade do arquivo.

A fonte do arquivo nao concede confianca. U1 a U4 sao locais; na U5, a rede
sera apenas transporte opcional e o mesmo contrato criptografico continuara
obrigatorio.

## Vetores publicos

Os vetores imutaveis ficam em `docs/fixtures/updates/v1/`. O manifesto desse
diretorio registra a chave publica TEST ONLY, o SHA-256 de cada artefato e o
motivo esperado. Nenhuma seed ou chave privada e versionada.

## Referencias

- [RFC 8032 - Ed25519](https://www.rfc-editor.org/info/rfc8032/)
- [FIPS 180-4 - SHA-256](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- [`docs/13-aplicativos/pacotes.md`](../13-aplicativos/pacotes.md) - ZPKG v1
- [`docs/melhorias futuras/atualizacoes.md`](../melhorias%20futuras/atualiza%C3%A7%C3%B5es.md) - roteiro U1-U5
