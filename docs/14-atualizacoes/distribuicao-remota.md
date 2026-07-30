# Distribuicao remota ZUPD v1

## Escopo

A U5 acrescenta um transporte HTTP manual ao servico Update. A rede nao e
uma raiz de confianca: o manifesto remoto e assinado pela mesma chave publica
Ed25519 de release e todo pacote baixado passa novamente pelo verificador ZUPD
v1 antes de ser publicado no cache.

A U5 esta concluida com Shell e System Updater Classic como matriz obrigatoria.
O modo Simple permanece implementado como fallback, mas sua regressao visual
e cobertura complementar e nao bloqueia a fase.

O servico e inicializado no boot apenas para recuperar o cache FAT12. A
habilitacao remota volta a `DISABLED` em todo boot. Separadamente, o Network
Manager inicia DHCP em background quando encontra uma NIC ativa com link. Isso
nao habilita o remoto: a inicializacao da U5 nao abre conexoes, nao consulta
manifestos e nao instala pacotes.

O canal Stable de desenvolvimento fica em `config/update-remote.json`. O
header derivado `src/include/core/update_remote_config.h` fixa:

```text
http://10.0.2.2:8000/zephyros/stable.zum
```

Um override existe somente para uma consulta ou download explicitamente
solicitado por `update fetch --url`.

## Manifesto ZUM1

O manifesto possui exatamente 256 bytes, inteiros little-endian e nenhuma
extensao opcional:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic `ZUM1` |
| 4 | 2 | versao do formato, `1` |
| 6 | 2 | tamanho, `256` |
| 8 | 2 | arquitetura i386, `1` |
| 10 | 2 | canal Stable, `1` |
| 12 | 4 | geracao informativa |
| 16 | 6 | versao base `MAJOR.MINOR.PATCH` |
| 22 | 6 | versao alvo `MAJOR.MINOR.PATCH` |
| 28 | 4 | epoch base |
| 32 | 4 | epoch alvo |
| 36 | 4 | tamanho exato do ZUPD, de 1 a 128 KiB |
| 40 | 32 | SHA-256 do ZUPD |
| 72 | 16 | `key_id` da raiz publica de release |
| 88 | 4 | flags reservadas, zero |
| 92 | 100 | caminho HTTP relativo, NUL e zero-padding |
| 192 | 64 | assinatura Ed25519 |

A assinatura cobre:

```text
ZEPHYROS-REMOTE-V1\0 || bytes[0..191]
```

O caminho deve comecar por `/`, conter somente ASCII alfanumerico, `/`, `.`,
`-` ou `_`, terminar em NUL e manter todo o restante zerado. `..`, barra
invertida, URL absoluta e caminho vazio sao recusados.

O `key_id`, a assinatura, a arquitetura, o canal, o tamanho, a versao base
instalada e a progressao de versao/epoch sao validados antes de qualquer
escrita. A geracao nao substitui a politica anti-replay do ZUPD; ela e apenas
um identificador informativo autenticado.

## HTTP

O pacote permanece obrigatoriamente no mesmo esquema, host e porta do
manifesto. O servico monta a URL usando somente a origem do manifesto e o
caminho relativo autenticado.

Manifesto e pacote exigem:

- HTTP simples e status `200`;
- `Content-Length` presente e exato;
- no maximo 256 bytes para o manifesto e 128 KiB para o pacote;
- `Connection: close`;
- ausencia de `Transfer-Encoding`, chunked e compressao.

HTTPS, redirects, URL absoluta interna, POST e troca de origem nao fazem parte
do contrato. Timeout ou falha transitoria permitem uma unica repeticao
integral do pacote desde o byte zero. Cancelamento, HTTP estruturalmente
invalido, assinatura, hash, tamanho e pacote incompativel nao geram retry.

`http_get_start()` preserva o GET bufferizado de ate 16 KiB. A U5 usa
`http_get_stream_start()`, que entrega cada bloco a um callback e aplica o
limite fornecido pelo chamador sem armazenar o corpo completo em RAM.

## Cache FAT12

Downloads usam dois slots hidden/system/archive:

```text
ZUR0.ZUP
ZUR1.ZUP
```

O estado redundante usa `ZUR0.STA` e `ZUR1.STA`, cada um com 512 bytes:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic `ZUR1` |
| 4 | 2 | versao `1` |
| 6 | 2 | tamanho `512` |
| 8 | 4 | sequencia monotonicamente crescente |
| 12 | 1 | fase: `CLEAN` ou `DOWNLOADING` |
| 13 | 1 | slot ativo ou `0xFF` |
| 14 | 1 | slot pendente ou `0xFF` |
| 15 | 1 | reservado zero |
| 16 | 4 | geracao do manifesto |
| 20 | 4 | tamanho do pacote |
| 24 | 32 | SHA-256 do pacote |
| 56 | 32 | SHA-256 do manifesto |
| 88 | 6 | versao base |
| 94 | 6 | versao alvo |
| 100 | 4 | epoch base |
| 104 | 4 | epoch alvo |
| 108 | 372 | reservados zerados |
| 480 | 32 | SHA-256 de `bytes[0..479]` |

Ausencia dos dois registros representa cache vazio e integro. A copia valida
de maior sequencia vence; empate divergente e invalido. Uma copia corrompida
e reparada naturalmente na proxima gravacao alternada. Duas copias invalidas
degradam somente o remoto e podem ser reinicializadas por um download
confirmado.

Antes da transferencia, o slot inativo e marcado como pendente. O filesystem
grava o arquivo sequencialmente em setores e clusters, com limite de 128 KiB.
Depois do ultimo byte, o servico exige:

1. tamanho HTTP exato;
2. SHA-256 igual ao manifesto;
3. verificacao ZUPD completa da U2;
4. arquitetura, base/alvo, epochs e politica compativeis com o manifesto.

Somente entao o registro publica o novo slot como ativo. O pacote anterior e
removido depois desse commit. Interrupcao remove apenas o pendente; o ativo
anterior permanece utilizavel. No boot, uma fase `DOWNLOADING` e revertida
localmente sem acesso a rede.

FAT32 permite consultar e autenticar o manifesto, mas nao cria cache. Os
arquivos internos nao sao alvos permitidos de um pacote ZUPD.

## API publica

`src/include/core/update_remote.h` define estados, motivos, candidato,
progresso, estado do cache e:

- `update_remote_init()`;
- `update_remote_enable()` e `update_remote_disable()`;
- `update_remote_check()`;
- `update_remote_fetch()`;
- `update_remote_clear()`;
- `update_remote_get_status()`;
- `update_remote_get_cached_alias()`;
- conversores estaveis de estado, motivo e armazenamento.

`update_remote_check()` recebe as mesmas opcoes cooperativas do download. A
consulta continua somente-leitura, mas Shell e System Updater podem apresentar
progresso e cancelar HTTP com `Esc`/`F12` antes de qualquer gravacao.
No System Updater, consulta e download rodam em um processo cooperativo
dedicado para que o processo de sistema continue atendendo rede, mouse e
Window Manager durante toda a espera HTTP.

Os motivos publicos preservam estes valores:

| Valor | Motivo |
|---:|---|
| 0 | `NONE` |
| 1 | `DISABLED` |
| 2 | `NETWORK` |
| 3 | `HTTP` |
| 4 | `TIMEOUT` |
| 5 | `MANIFEST_FORMAT` |
| 6 | `UNKNOWN_KEY` |
| 7 | `MANIFEST_SIGNATURE` |
| 8 | `VERSION` |
| 9 | `SIZE` |
| 10 | `SPACE` |
| 11 | `IO` |
| 12 | `CANCELLED` |
| 13 | `PACKAGE_HASH` |
| 14 | `PACKAGE_VERIFY` |
| 15 | `PACKAGE_MISMATCH` |
| 16 | `CACHE` |

As operacoes confirmadas aceitam callback cooperativo. Durante o download,
somente Esc ou F12 e interpretado como cancelamento.

Falha remota nunca degrada verificacao, aplicacao, rollback ou historico
local. O `health` informa `remoto=DISABLED`, `READY` ou `DEGRADED` de forma
independente.

## Comandos

```text
update remote status
update remote enable
update remote disable
update remote clear
update remote clear --confirm
update fetch
update fetch --confirm
update fetch --url http://host:porta/caminho.zum
update fetch --url http://host:porta/caminho.zum --confirm
```

`update fetch` consulta e apresenta o candidato sem gravar. A confirmacao
repete a consulta e exige exatamente o mesmo manifesto antes de iniciar a
transferencia. O pacote armazenado aparece na aba Pacotes como `ZUR0.ZUP` ou
`ZUR1.ZUP`, mas nunca e aplicado automaticamente.

## Limites de seguranca

Ed25519 e SHA-256 protegem autenticidade e integridade mesmo sobre HTTP.
HTTP simples nao protege confidencialidade, disponibilidade, metadados ou
observacao do trafego. Sem Secure Boot e armazenamento protegido, adulteracao
offline do sistema ou rollback integral do disco permanecem fora do modelo.

Nao existem TLS, atualizacao silenciosa, consulta remota no boot, telemetria,
instalacao direta pelo download ou varios candidatos em um mesmo manifesto.
O DHCP automatico pertence ao Network Manager, usa somente RAM e nunca dispara
HTTP ou habilita a distribuicao remota.

## Referencias

- [Contrato ZUPD v1](contrato-zupd-v1.md)
- [System Updater](system-updater.md)
- [Ferramenta host](ferramenta-zupd.md)
- [Comandos do Shell](../09-shell/comandos.md)
