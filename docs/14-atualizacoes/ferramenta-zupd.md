# Ferramenta Host ZUPD v1

## Escopo

`tools/updater.py` gera chaves Ed25519, monta artefatos deterministas, verifica
ZUPD v1, produz fixtures publicos e sincroniza a raiz publica usada pelo
kernel. A ferramenta host usa `cryptography==49.0.0`, fixado em
`tools/requirements-updater.txt`.

O fluxo U2 apenas verifica. A U3 acrescenta geracao do fixture transacional e
auditoria offline da imagem; a aplicacao em si acontece somente no kernel
FAT12. Nenhum comando host acessa a rede ou altera boot, stage2 ou setores de
kernel.

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
staging e ausencia de journal pendente:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.0 --expect-rollback unavailable
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.1 --expect-rollback available
```

`--expect-rollback any` omite essa expectativa. `--allow-pending` existe
somente para diagnosticar uma imagem deliberadamente interrompida; sem essa
opcao, qualquer transacao pendente e uma falha.

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

## Referencias

- [cryptography 49.0.0](https://cryptography.io/_/downloads/en/49.0.0/pdf/)
- [Ed25519 no cryptography](https://cryptography.io/en/49.0.0/hazmat/primitives/asymmetric/ed25519/)
- [Serializacao de chaves](https://cryptography.io/en/49.0.0/hazmat/primitives/asymmetric/serialization/)
