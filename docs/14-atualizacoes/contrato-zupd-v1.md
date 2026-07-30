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

Os fixtures imutaveis da U1 usam somente chaves publicas TEST ONLY dos vetores
do RFC 8032. Na U2, o mantenedor provisionou offline a unica raiz de release
do v1. O repositorio contem apenas `config/update-release-public.json` e o
header derivado `src/include/core/update_trust.h`; seed, senha e chave privada
nao sao versionadas.

A sincronizacao e conferida por `tools/updater.py check-trust`. O `key_id` da
raiz de release e `d4926d816d8373a412e7458cc9f14379`.

O v1 possui uma unica chave de release e nao permite rotacao ou revogacao
automatica. Se essa chave for comprometida, a recuperacao exige uma nova
imagem confiavel instalada manualmente, com nova chave publica e epoch
incrementado.

## Ordem de validacao

O verificador usa esta ordem para produzir resultados deterministicos:

1. validar magic, versao do formato, algoritmos, limites, tamanhos, campos
   reservados, aritmetica de offsets, ordenacao, caminhos e sobreposicoes;
2. validar `content_sha256` e o SHA-256 individual de cada payload;
3. localizar a chave pelo `key_id` e validar a assinatura Ed25519;
4. validar arquitetura, versoes, epochs e allowlist do sistema atual.

Nenhum dado e gravado durante a verificacao.

## Motivos de rejeicao

Os motivos abaixo formam o diagnostico publico exposto por
`src/include/core/update.h`. Eles permanecem separados dos codigos genericos
de `errors.h`.

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

## Verificador U2 e `health`

`update_init()` valida a raiz publica e executa autotestes SHA-256, SHA-512 e
Ed25519. O parser decodifica inteiros little-endian manualmente, usa workspace
estatico, le payloads em blocos e nao chama APIs de escrita. Uma trava recusa
verificacoes concorrentes.

O header publico oferece `update_init()`, `update_is_ready()`,
`update_verify_file()`, `update_get_capabilities()` e
`zupd_reason_name()`. A versao compartilhada vem de
`src/include/core/version.h`.

O componente `Update` no `health` observa:

- `READY`: verificador local, autoteste criptografico e chave publica prontos;
- `DEGRADED`: criptografia pronta, mas o filesystem local esta indisponivel;
- `DISABLED`: chave ausente/invalida ou autoteste criptografico falhou.

Capacidades ainda nao implementadas nao degradam o componente. Verificacao,
aplicacao, rollback e acesso remoto devem aparecer separadamente. O remoto
permanece `DISABLED` por padrao e nao altera a disponibilidade local.

O unico comando da U2 e `update verify <arquivo.ZUP>`. Ele mostra o motivo
estavel, versoes, epochs, quantidade de arquivos e confirma explicitamente que
nenhuma gravacao ocorreu.

## U3: aplicacao FAT12 e versao instalada

A U3 mantem a versao de build do kernel em `0.1.0`, epoch `0`, definida por
`version.h`. A compatibilidade de novos pacotes passa a usar a versao de
conteudo instalada e persistida pelo updater. Na ausencia completa dos quatro
arquivos de controle, essa versao instalada e o baseline `0.1.0`, epoch `0`.

`update verify` continua disponivel em FAT12 e FAT32. Aplicacao, recuperacao e
rollback existem somente no volume FAT12 atual e somente para arquivos 8.3 no
diretorio raiz. FAT32 e caminhos com diretorios retornam `ERR_UNAVAILABLE`.

O filesystem serializa suas operacoes publicas durante cada leitura ou
mutacao. A escrita atomica FAT12 segue:

1. gravar o novo conteudo em clusters ainda livres;
2. encadear os clusters e persistir todas as copias da FAT;
3. trocar a entrada do diretorio raiz escrevendo somente seu setor;
4. depois do ponto de troca, liberar a cadeia antiga e persistir as FATs.

A exclusao atomica marca e persiste primeiro a entrada como removida; somente
depois libera a cadeia. As APIs publicas relacionadas sao
`fs_get_root_file_info()`, `fs_atomic_write_root()` e
`fs_atomic_delete_root()`. A escrita aceita create-or-replace para arquivos
internos do updater e replace-only para os tres alvos do ZUPD.

## Registros persistentes U3

Cada registro possui exatamente 512 bytes, inteiros little-endian, uma
sequencia `uint32_t` monotonicamente crescente, campos reservados zerados e
SHA-256 nos bytes `480..511`, calculado sobre os bytes `0..479`.

As duas copias alternadas de estado usam `ZUPD0.STA` e `ZUPD1.STA`:

| Offset | Campo | Regra |
|---:|---|---|
| 0 | `magic[4]` | `ZUST` |
| 4 | `version` | `uint16_t`, valor `1` |
| 6 | `record_size` | `uint16_t`, valor `512` |
| 8 | `sequence` | `uint32_t` |
| 12 | `installed_version` | tres `uint16_t` |
| 18 | reservado | 2 bytes zero |
| 20 | `installed_epoch` | `uint32_t` |
| 24 | `rollback_available` | `0` ou `1` |
| 25 | `rollback_slot` | `0` ou `1` |
| 26 | `rollback_entry_count` | `0` a `3` |
| 27 | reservado | 1 byte zero |
| 28 | `previous_version` | tres `uint16_t` |
| 34 | reservado | 2 bytes zero |
| 36 | `previous_epoch` | `uint32_t` |
| 40 | `current[3]` | tres descritores de 40 bytes |
| 160 | `rollback[3]` | tres descritores de 40 bytes |
| 280 | reservado | 200 bytes zero |
| 480 | `record_sha256` | 32 bytes |

Cada descritor de arquivo contem tamanho `uint32_t` no offset `0`, SHA-256 no
offset `4`, flag `present` no offset `36` e tres bytes reservados. Os IDs sao
fixos: `0=EXPLORER.BMP`, `1=SHELL.BMP`, `2=TASKMGR.BMP`. Um descritor ausente
deve estar totalmente zerado.

As duas copias alternadas do journal usam `ZUPD0.JRN` e `ZUPD1.JRN`:

| Offset | Campo | Regra |
|---:|---|---|
| 0 | `magic[4]` | `ZUJ1` |
| 4 | `version` | `uint16_t`, valor `1` |
| 6 | `record_size` | `uint16_t`, valor `512` |
| 8 | `sequence` | `uint32_t` |
| 12 | `kind` | `NONE=0`, `APPLY=1`, `ROLLBACK=2` |
| 13 | `phase` | `NONE=0`, `PREPARED=1`, `REPLACING=2`, `COMMITTED=3` |
| 14 | `slot` | `0` ou `1` |
| 15 | `entry_count` | `0` a `3` |
| 16 | `progress` | alvos ja trocados |
| 17 | reservado | 3 bytes zero |
| 20 | `base_state_sequence` | `uint32_t` |
| 24 | `base_version` | tres `uint16_t` |
| 30 | `target_version` | tres `uint16_t` |
| 36 | `base_epoch` | `uint32_t` |
| 40 | `target_epoch` | `uint32_t` |
| 44 | reservado | 4 bytes zero |
| 48 | `entries[3]` | tres entradas de 76 bytes |
| 276 | reservado | 204 bytes zero |
| 480 | `record_sha256` | 32 bytes |

Uma entrada ativa do journal possui `target_id`, flags `old_present` e
`new_present` nos offsets `0..2`, reservado zero no offset `3`, tamanhos
antigo/novo nos offsets `4` e `8`, SHA-256 antigo no offset `12` e novo no
offset `44`. Entradas nao usadas ficam zeradas. Um journal `NONE` tem todos os
bytes `12..479` zerados.

Se alguma copia de controle existir, mas nenhuma copia valida do mesmo tipo
puder ser selecionada, o estado e inconsistente: novas gravacoes sao
bloqueadas e `Update` fica `DEGRADED`.

## Slots, commit e recuperacao

O slot A usa backups `ZBA0.BAK` a `ZBA2.BAK` e staging `ZSA0.NEW` a
`ZSA2.NEW`. O slot B usa `ZBB0.BAK` a `ZBB2.BAK` e `ZSB0.NEW` a
`ZSB2.NEW`. Esses nomes sao internos; um pacote ZUPD nao pode cria-los,
remove-los ou endereca-los.

A aplicacao repete toda a verificacao U2 e o preflight de espaco. Em seguida:

1. copia e valida todos os payloads no staging;
2. copia e valida os arquivos instalados no slot de backup inativo;
3. persiste o journal `PREPARED`;
4. substitui e valida cada alvo, persistindo `REPLACING` e `progress`;
5. persiste `COMMITTED`, grava a nova versao/epoch e os hashes atuais;
6. limpa o staging, o rollback mais antigo e, por ultimo, o journal.

Antes de `COMMITTED`, uma aplicacao interrompida restaura todos os arquivos
anteriores. Depois de `COMMITTED`, o boot valida o estado novo e termina o
commit. Um rollback interrompido continua ate restaurar toda a geracao. Apenas
uma geracao concluida de rollback e preservada.

Os BMPs em memoria nao sao recarregados durante a transacao. Aplicacao e
rollback concluidos informam `reboot_required=1`.

## API e motivos de acao U3

`src/include/core/update.h` acrescenta
`update_get_installed_version()`, `update_apply_file()`,
`update_rollback()`, `update_test_fail_after()` e
`update_action_reason_name()`. O resultado informa versoes/epochs, quantidade
total e processada, motivo da verificacao, necessidade de reboot e recuperacao
pendente.

Os motivos de acao sao separados dos motivos criptograficos:

| Valor | Motivo |
|---:|---|
| 0 | `UPDATE_ACTION_NONE` |
| 1 | `UPDATE_ACTION_VERIFY` |
| 2 | `UPDATE_ACTION_UNSUPPORTED_FS` |
| 3 | `UPDATE_ACTION_STATE` |
| 4 | `UPDATE_ACTION_SPACE` |
| 5 | `UPDATE_ACTION_IO` |
| 6 | `UPDATE_ACTION_CANCELLED` |
| 7 | `UPDATE_ACTION_NO_ROLLBACK` |
| 8 | `UPDATE_ACTION_RECOVERY_PENDING` |

O callback cooperativo consulta Esc/F12 entre staging, backups e trocas. O
failpoint one-shot `update test fail-after <N>` existe somente para diagnostico
e deixa deliberadamente o journal para recuperacao no boot.

No `health`, verificacao local permanece `READY`; aplicacao fica `READY`
somente em FAT12 com estado integro; rollback fica `READY` somente com backup
valido; remoto permanece `DISABLED (U5)`. Journal pendente ou controles
invalidos tornam o componente `Update` `DEGRADED`.

## U4: status e historico persistente

A U4 nao altera o container ZUPD nem a ordem transacional da U3. Ela acrescenta
consultas somente-leitura e um historico local redundante. `update status`,
`update history`, a abertura do aplicativo e o preflight nao criam nem
modificam arquivos. Somente uma aplicacao ou um rollback confirmados, ou a
conclusao de uma recuperacao no boot, podem acrescentar eventos.

`update_store_state_t` usa os estados `UNAVAILABLE`, `EMPTY`, `VALID` e
`INVALID`. `update_get_status()` informa versoes de build, instalada e de
rollback, epochs, integridade separada dos controles e dos arquivos atuais,
integridade do historico, journal pendente, capacidades e o ultimo evento.
`update_get_history_count()` e
`update_get_history_entry()` expoem no maximo oito eventos, sempre do mais
recente para o mais antigo.

O historico usa as copias alternadas `ZUPD0.HIS` e `ZUPD1.HIS`. Cada arquivo
possui atributos FAT hidden, system e archive e segue o mesmo envelope de
512 bytes, little-endian e SHA-256 dos controles U3:

| Offset | Campo | Regra |
|---:|---|---|
| 0 | `magic[4]` | `ZUH1` |
| 4 | `version` | `uint16_t`, valor `1` |
| 6 | `record_size` | `uint16_t`, valor `512` |
| 8 | `sequence` | `uint32_t` monotonicamente crescente |
| 12 | `count` | `0` a `8` |
| 13 | `next_index` | proximo slot do ring, `0` a `7` |
| 14 | reservado | 18 bytes zero |
| 32 | `entries[8]` | oito entradas de 56 bytes |
| 480 | `record_sha256` | SHA-256 dos bytes `0..479` |

Quando `count < 8`, `next_index` deve ser igual a `count` e todos os slots
restantes ficam zerados. Com oito eventos, `next_index` identifica o slot que
sera substituido pela proxima gravacao. A ordem logica do slot mais antigo ao
mais novo deve possuir sequencias consecutivas e terminar na sequencia do
registro.

Cada entrada possui:

| Offset | Campo | Regra |
|---:|---|---|
| 0 | `sequence` | `uint32_t`, nao zero e no maximo a sequencia do registro |
| 4 | `operation` | `APPLY=1`, `ROLLBACK=2`, `RECOVERY_APPLY=3`, `RECOVERY_ROLLBACK=4` |
| 5 | `outcome` | `SUCCESS=1`, `FAILED=2`, `CANCELLED=3`, `RECOVERED=4` |
| 6 | `action_reason` | motivo U3 de `0` a `8` |
| 7 | `verification_reason` | motivo ZUPD de `0` a `11` |
| 8 | `from_version` | tres `uint16_t` |
| 14 | `to_version` | tres `uint16_t` |
| 20 | `from_epoch` | `uint32_t` |
| 24 | `to_epoch` | `uint32_t` |
| 28 | `entry_count` | `uint16_t` |
| 30 | `completed_entries` | `uint16_t`, no maximo `entry_count` |
| 32 | `flags` | bit 0 indica reboot; demais bits zero |
| 33 | `package_alias[13]` | ASCII imprimivel terminado em NUL, sem separadores |
| 46 | reservado | 10 bytes zero |

Ausencia das duas copias significa historico vazio e integro. A copia valida
de maior sequencia vence. Se ambas forem validas, tiverem a mesma sequencia e
bytes diferentes, o historico e invalido. Uma copia corrompida e reparada
naturalmente na proxima gravacao alternada. Se nenhuma copia existente for
valida, somente o diagnostico de historico fica degradado; verificacao,
aplicacao e rollback continuam disponiveis. O proximo evento confirmado inicia
uma nova geracao e registra a perda no log.

Aplicacao e rollback confirmados registram sucesso, falha ou cancelamento.
Uma transacao que deixa journal pendente adia a gravacao. Depois da recuperacao
no boot, o servico registra primeiro o encerramento interrompido e depois
`RECOVERY_APPLY/RECOVERED` ou `RECOVERY_ROLLBACK/RECOVERED`. Falha em gravar o
historico nunca desfaz um commit U3 ja concluido.

O alias fica vazio em rollback e em recuperacoes para as quais o journal U3
nao preserva o nome do pacote. O campo continua terminado em NUL e totalmente
zerado nesses casos.

`update_capabilities_t` acrescenta `history_available`. O `health` completo e
compacto mostram `historico=READY`, `DEGRADED` ou `DISABLED` sem confundir
essa disponibilidade com a capacidade remota U5.

## System Updater U4

O aplicativo nativo `System Updater` usa somente as APIs publicas do servico.
Ele enumera ate 16 arquivos `.ZUP` da raiz, ignora diretorios e arquivos
hidden/system, ordena os aliases e informa excesso sem alocacao dinamica.
Simple e Classic compartilham as abas Pacotes, Estado e Historico e os mesmos
fluxos de verificacao, preflight, confirmacao, aplicacao e rollback.

Toda confirmacao e cancelada ao mudar aba, selecao ou lista. A confirmacao
repete integralmente verificacao e preflight antes de gravar. Durante uma
mutacao, somente Esc ou F12 participa do cancelamento cooperativo. Os BMPs
continuam sendo recarregados apenas no reboot.

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

A fonte do arquivo nao concede confianca. Na U5, a rede e apenas transporte
opcional: o manifesto ZUM1 e autenticado com a raiz de release e o pacote
baixado passa novamente por este contrato completo antes do cache. O formato,
allowlist, ordem de validacao e politica de versao ZUPD nao mudam. O contrato
remoto fica em [`distribuicao-remota.md`](distribuicao-remota.md).

## Vetores publicos

Os vetores imutaveis ficam em `docs/fixtures/updates/v1/`. O manifesto desse
diretorio registra a chave publica TEST ONLY, o SHA-256 de cada artefato e o
motivo esperado. Nenhuma seed ou chave privada e versionada.

A matriz U2 fica em `docs/fixtures/updates/u2/` e e assinada pela raiz publica
de release. Ela contem `VALID.ZUP`, `TRUNC.ZUP`, `BADHASH.ZUP`, `BADSIG.ZUP`,
`BADVER.ZUP`, `BADFMT.ZUP` e `UNKKEY.ZUP`, com resultados esperados `NONE`,
`SIZE`, `HASH`, `SIGNATURE`, `BASE_VERSION`, `FORMAT` e `UNKNOWN_KEY`. O
manifesto `fixtures.json` publica tamanho e SHA-256 de cada arquivo.

O fixture U3 fica em `docs/fixtures/updates/u3/`. `APPLY.ZUP` autentica a
transicao `0.1.0 -> 0.1.1`, epoch `0`, com os tres BMPs. Os payloads sao
copias publicas deterministicas em que somente os bytes RGB 24-bit foram
invertidos; headers, dimensoes, formato e padding permanecem inalterados. O
manifesto publica os hashes dos assets de origem, payloads e artefato.

## Referencias

- [RFC 8032 - Ed25519](https://www.rfc-editor.org/info/rfc8032/)
- [FIPS 180-4 - SHA-256](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- [Monocypher 4.0.3](https://github.com/LoupVaillant/Monocypher/releases/tag/4.0.3)
- [Manual Ed25519 do Monocypher](https://monocypher.org/manual/ed25519)
- [`docs/13-aplicativos/pacotes.md`](../13-aplicativos/pacotes.md) - ZPKG v1
- [`ferramenta-zupd.md`](ferramenta-zupd.md) - chave, build, verificacao e fixtures
- [`distribuicao-remota.md`](distribuicao-remota.md) - manifesto e cache U5
- [`docs/melhorias futuras/atualizacoes.md`](../melhorias%20futuras/atualiza%C3%A7%C3%B5es.md) - roteiro U1-U5
