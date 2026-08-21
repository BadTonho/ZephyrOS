# Ferramenta Host ZUPD v1

## Escopo

`tools/updater.py` gera chaves Ed25519, monta artefatos deterministas, verifica
ZUPD v1, produz fixtures publicos e sincroniza a raiz publica usada pelo
kernel. A ferramenta host usa `cryptography==49.0.0`, fixado em
`tools/requirements-updater.txt`.

O fluxo U2 apenas verifica. A U3 acrescenta geracao do fixture transacional e
auditoria offline da imagem; a U4 estende essa auditoria ao historico
redundante `ZUH1`. A U5 gera e serve manifestos remotos `ZUM1` e audita o
cache `ZUR1`. A aplicacao em si acontece somente no kernel FAT12. Nenhum
comando host altera boot, stage2 ou setores de kernel.

## Preparacao

Instale a dependencia em um ambiente Python dedicado:

```text
python -m pip install -r tools/requirements-updater.txt
```

Todos os comandos abaixo devem ser executados na raiz do repositorio.

## Chave de release

A chave privada deve ficar fora do repositorio e possuir backup offline. A
senha deve ter pelo menos 16 caracteres e nao e exibida. O comando recusa
arquivo existente, caminho privado dentro do repositorio e senha divergente:

```text
python tools/updater.py keygen --private <arquivo-privado-fora-do-repo> --public config/update-release-public.json
```

Somente o JSON publico e o header derivado podem ser versionados:

```text
python tools/updater.py sync-trust --public config/update-release-public.json --output src/include/core/update_trust.h
python tools/updater.py check-trust --public config/update-release-public.json --header src/include/core/update_trust.h
```

Perder a chave privada ou sua senha impede novos releases sob a raiz atual.
Comprometimento da chave exige uma nova imagem confiavel instalada manualmente,
com nova chave publica e epoch incrementado. O ZUPD v1 nao possui rotacao ou
revogacao automatica.

## Manifesto e build

O manifesto JSON possui ordem e campos exatos:

```json
{
  "format": "ZUPD v1",
  "architecture": "i386",
  "base_version": "0.1.0",
  "target_version": "0.1.1",
  "base_epoch": 0,
  "target_epoch": 0,
  "files": [
    {
      "path": "SHELL.BMP",
      "source": "payloads/SHELL.BMP"
    }
  ]
}
```

O empacotador ordena os alvos e recusa duplicacao, caminho fora da allowlist,
compressao, arquivo vazio, limite individual acima de 64 KiB, artefato acima
de 128 KiB e sobrescrita da saida:

```text
python tools/updater.py build --manifest <manifesto.json> --private <arquivo-privado-fora-do-repo> --output <release.zephyrosupd>
```

O mesmo artefato pode receber um alias FAT 8.3 com extensao `.ZUP`.

## Verificacao host

```text
python tools/updater.py verify --artifact <release.zephyrosupd> --public config/update-release-public.json
python tools/updater.py selftest
```

`verify` usa por padrao a versao e o epoch de
`src/include/core/version.h`. As opcoes `--system-version` e
`--system-epoch` existem para testes controlados.

## Fixtures U2

Os sete vetores publicados ficam em `docs/fixtures/updates/u2/`. Eles foram
gerados uma vez com a raiz de release e nao devem ser sobrescritos. O
`fixtures.json` publica a chave, tamanho, SHA-256 e motivo esperado:

| Arquivo | Resultado |
|---|---|
| `VALID.ZUP` | `NONE` |
| `TRUNC.ZUP` | `SIZE` |
| `BADHASH.ZUP` | `HASH` |
| `BADSIG.ZUP` | `SIGNATURE` |
| `BADVER.ZUP` | `BASE_VERSION` |
| `BADFMT.ZUP` | `FORMAT` |
| `UNKKEY.ZUP` | `UNKNOWN_KEY` |

O subcomando `fixtures` exige a chave privada correspondente ao JSON publico e
recusa um diretorio de saida nao vazio. Nenhuma seed, senha ou chave privada e
gravada junto aos vetores.

## Fixture U3

`fixtures-u3` usa a mesma chave externa para gerar deterministicamente
`docs/fixtures/updates/u3/APPLY.ZUP`. O artefato autentica a transicao
`0.1.0 -> 0.1.1`, epoch `0`, e inclui os tres alvos permitidos.

```text
python tools/updater.py fixtures-u3 --private <arquivo-privado-fora-do-repo> --public config/update-release-public.json --output-dir docs/fixtures/updates/u3
```

A senha e solicitada sem eco. O diretorio deve estar ausente ou vazio. A
ferramenta grava apenas:

- `APPLY.ZUP`;
- `EXPLORER.BMP`, `SHELL.BMP` e `TASKMGR.BMP` publicos;
- `fixtures.json` com chave publica, tamanhos e SHA-256.

Os payloads invertem somente os bytes RGB dos BMPs 24-bit. Headers, dimensoes,
compressao, padding e tamanho permanecem iguais aos assets de origem.

## Auditoria offline da imagem

`audit-image` deve ser executado somente depois de encerrar o QEMU. Ele compara
as copias da FAT12, percorre cadeias e alocacoes, seleciona os controles
redundantes de maior sequencia, valida SHA-256, arquivos atuais, backups,
staging, ausencia de journal pendente e o ring de historico U4:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.0 --expect-rollback unavailable
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.1 --expect-rollback available
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.0 --expect-rollback unavailable --expect-history-count 4 --expect-last-event recovery-apply
```

`--expect-rollback any` omite essa expectativa. `--allow-pending` existe
somente para diagnosticar uma imagem deliberadamente interrompida; sem essa
opcao, qualquer transacao pendente e uma falha. `--expect-history-count`
compara a quantidade de eventos validos. `--expect-last-event` aceita
`apply`, `rollback`, `recovery-apply` ou `recovery-rollback`.

O decoder host confere magic `ZUH1`, versao, tamanho, campos reservados,
SHA-256, sequencias, enums, alias, slots inativos, wrap em oito eventos e
empate divergente entre as duas copias. Ausencia das duas copias e reportada
como historico vazio e integro.

Para U5, os argumentos adicionais sao:

```text
--expect-remote-cache any|empty|valid
--expect-remote-alias ZUR0.ZUP|ZUR1.ZUP
--expect-remote-pending any|clean|pending
```

O auditor valida magic `ZUR1`, versao, sequencia, fase, slots, reservados,
SHA-256 do registro, cadeia FAT do pacote, tamanho e SHA-256 do ZUPD. Em
estado limpo, somente o slot ativo pode existir. Ausencia dos dois registros
e cache vazio e integro.

## Canal e fixtures U5

O canal publico versionado pode ser conferido contra o header derivado:

```text
python tools/updater.py check-remote --config config/update-remote.json --header src/include/core/update_remote_config.h
```

`sync-remote` cria um header novo e recusa sobrescrita, seguindo a mesma
politica defensiva de `sync-trust`.

Os manifestos assinados exigem a chave privada externa:

```text
python tools/updater.py fixtures-u5 --private <arquivo-privado-fora-do-repo> --public config/update-release-public.json --output-dir docs/fixtures/updates/u5
```

A senha e solicitada sem eco. O diretorio deve estar ausente ou vazio e recebe
somente artefatos publicos:

| Arquivo | Resultado esperado |
|---|---|
| `stable.zum` | manifesto valido, geracao 1 |
| `stable2.zum` | manifesto valido, geracao 2 |
| `tampered.zum` | `MANIFEST_SIGNATURE` |
| `badpkg.zum` | manifesto valido para ZUPD com `HASH` invalido |
| `truncated.zum` | `HTTP`, corpo com tamanho inexato |
| `ep6-stable.json` | Release EP6.0 valida, tag `ep6-stable` |
| `ep6-alt.json` | Release EP6.0 valida, tag distinta `ep6-alt` |
| `ep6-*.json` | descritores EP6.0 invalidos para a matriz de falhas |
| `fixtures.json` | chave publica, tamanhos e SHA-256 |

`stable.zum` e `stable2.zum` reutilizam o `APPLY.ZUP` publico da U3.
`badpkg.zum` reutiliza `BADHASH.ZUP` da U2. Nenhuma seed, senha ou chave
privada e copiada.

O servidor controlado para o QEMU e:

```text
python tools/updater.py serve-u5 --bind 0.0.0.0 --port 8000 --root docs/fixtures/updates/u5
```

Ele publica os manifestos e pacotes em `/zephyros/`. As rotas adicionais
`error.zum` e `slow.zum` exercitam erro HTTP e timeout; `truncated.zum`
exercita truncamento. O servidor e apenas um fixture de desenvolvimento e
deve ser encerrado com Ctrl+C.

As fixtures EP6.0 incluem `ep6-stable.json` e `ep6-alt.json`, duas Releases
validas com tags distintas, alem de descritores determinísticos para JSON
invalido, tag ausente/divergente, asset ausente, hash divergente e
`version_lock` divergente. O servidor publica todas essas rotas para a matriz
QEMU; nenhuma delas escolhe automaticamente a maior tag.

## Releases oficiais EP5

A Release e a unidade de distribuicao no host. Seu nome, identificador e tag
auxiliar nao definem a versao do ZephyrOS. A trava oficial permanece nos
campos assinados `base_version`, `target_version`, `base_epoch` e
`target_epoch` do ZUPD e do ZUM1.

`release-build` recebe um manifesto ZUPD, a chave privada externa, a raiz
publica, a geracao ZUM1 e o commit de origem. A saida deve ser um diretorio
novo e recebe exatamente `update.zephyrosupd`, `release.zum` e `release.json`:

```text
python tools/updater.py release-build --release <identificador> --manifest <manifesto-zupd.json> --private <chave-fora-do-repositorio> --public config/update-release-public.json --generation <numero> --source-commit <sha-ou-ref> --output-dir <diretorio-novo>
```

Uma tag pode ser registrada somente como marcador auxiliar:

```text
python tools/updater.py release-build --release <identificador> --manifest <manifesto-zupd.json> --private <chave-fora-do-repositorio> --public config/update-release-public.json --generation <numero> --source-commit <sha-ou-ref> --tag <tag> --output-dir <diretorio-novo>
```

Quando `--tag` e usado, a tag deve existir localmente e apontar para o mesmo
commit. Seu texto nao precisa conter uma versao. Sem `--tag`, a Release
continua completa e verificavel.

O descritor `zephyros-release-v1` registra identidade, canal Stable, commit,
tag opcional, trava de versao e inventario SHA-256. Ele nao e uma nova raiz de
confianca: `release-check` autentica novamente ZUPD e ZUM1 e exige que todos os
campos redundantes coincidam com os dados assinados:

```text
python tools/updater.py release-check --release <diretorio/release.json> --public config/update-release-public.json
```

A verificacao e inteiramente offline. Ela recusa commit ou tag inexistente,
tag apontando para outro commit, asset ausente, tamanho ou hash divergente,
assinatura invalida e qualquer diferenca entre a trava, o ZUPD e o ZUM1.

Depois de `release-check`, o mantenedor publica manualmente os tres arquivos
sem altera-los. Uma Release publicada e imutavel; qualquer correcao gera uma
nova Release. GitHub continua sendo somente origem de distribuicao e seus
titulos, descricoes e tags nao substituem as assinaturas Ed25519.

O `selftest` cobre Releases validas com e sem tag, asset ausente, manifesto
adulterado, pacote invalido, trava divergente, commit divergente e tag
divergente.

O `selftest` tambem confere as duas fixtures EP6.0, seus hashes, assets,
travas assinadas e o manifesto publico dos descritores invalidos.

## Validacao no sistema

O Makefile injeta os sete aliases U2 e `APPLY.ZUP` na imagem FAT. Depois do
gate host, o mantenedor executa o build e, no QEMU, usa:

```text
health
mem
update verify VALID.ZUP
update verify TRUNC.ZUP
update verify BADHASH.ZUP
update verify BADSIG.ZUP
update verify BADVER.ZUP
update verify BADFMT.ZUP
update verify UNKKEY.ZUP
mem
regcheck full
```

O SHA-256 de `build/zephyros.img` deve ser comparado antes e depois da sessao.
Somente `VALID.ZUP` pode ser aceito e toda resposta deve confirmar que nenhuma
gravacao foi realizada.

Para U3, em uma imagem limpa:

```text
health
update apply APPLY.ZUP
update apply APPLY.ZUP --confirm
```

Depois do reboot, a versao instalada deve ser `0.1.1`, os BMPs devem estar
visualmente invertidos, rollback deve estar `READY` e
`update verify APPLY.ZUP` deve retornar `BASE_VERSION`.

O fluxo inverso usa:

```text
update rollback
update rollback --confirm
```

Depois de outro reboot, a versao deve voltar a `0.1.0` e rollback deve ficar
`DISABLED`. O cenario de recuperacao usa `update test fail-after 1`, aplicacao
confirmada e reboot; o boot deve restaurar `0.1.0`. Cada cenario termina com
`mem`, `regcheck full` e `audit-image`.

Para U4, `update status`, `update history`, abertura do aplicativo e preflight
nao podem alterar a contagem do historico. Simple e Classic devem mostrar as
mesmas tres abas e permitir aplicar e restaurar. Depois do failpoint e do boot,
o historico deve conter o encerramento pendente seguido de
`RECOVERY_APPLY/RECOVERED`.

Para U5, aguarde o DHCP automatico, habilite remoto por sessao, consulte o
manifesto e confirme o download. Consultas e downloads nao alteram o historico
U4, e aplicar o alias `ZUR0.ZUP` ou `ZUR1.ZUP` continua sendo uma acao local
separada. Shell e System Updater Classic formam a matriz obrigatoria; o Simple
permanece como fallback com cobertura complementar. Depois de encerrar o QEMU:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-remote-cache valid --expect-remote-alias ZUR0.ZUP --expect-remote-pending clean
```

## Referencias

- [cryptography 49.0.0](https://cryptography.io/_/downloads/en/49.0.0/pdf/)
- [Ed25519 no cryptography](https://cryptography.io/en/49.0.0/hazmat/primitives/asymmetric/ed25519/)
- [Serializacao de chaves](https://cryptography.io/en/49.0.0/hazmat/primitives/asymmetric/serialization/)
- [Distribuicao remota ZUPD v1](distribuicao-remota.md)
