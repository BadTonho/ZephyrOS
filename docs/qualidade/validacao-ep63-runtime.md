# Procedimento operacional — validação EP6.3 Runtime v2

Este documento contém o roteiro operacional detalhado da EP6.3. Não registrar
chaves, senhas, tokens ou caminhos pessoais: `<SECRETS>` representa um diretório
externo com a chave privada e o JSON público da Release.

## Gate de código

Executar somente quando houver alteração de código ou do header de configuração
remota. Depois do gate, as fixtures não exigem novo build do kernel.

```text
make q3check
make clean && make
make run
```

## Host: selftest e fixture padrão

```text
python tools/updater.py selftest
python tools/updater.py fixtures-runtime --private "<SECRETS>\\release-private-ep63.pem" --public "<SECRETS>\\release-public-ep63.json" --output-dir "<SECRETS>\\ep63-runtime-fixtures-changed" --changed-assets
python tools/updater.py runtime-verify --manifest "<SECRETS>\\ep63-runtime-fixtures-changed\\valid\\runtime.zum2" --package "<SECRETS>\\ep63-runtime-fixtures-changed\\valid\\runtime.zephyrosupd" --public "<SECRETS>\\release-public-ep63.json" --system-version 0.1.0 --system-epoch 0
python tools/updater.py serve-runtime --root "<SECRETS>\\ep63-runtime-fixtures-changed" --tag ep63-runtime --bind 0.0.0.0 --port 8000
```

O último comando fica em um terminal separado e deve permanecer aberto.

Se o `selftest` retornar `manifesto U2 nao corresponde a raiz publica`, os
fixtures legados U2/U3/U5 ainda estão assinados com uma chave anterior. Não
ignorar esse erro e não repetir o QEMU: regenerar os três conjuntos na ordem
abaixo com a mesma chave usada em `config/update-release-public.json`.

```powershell
$S = "<SECRETS>"
$U2 = "$S\ep63-fixtures-regenerated-u2"
$U3 = "$S\ep63-fixtures-regenerated-u3"
$U5 = "$S\ep63-fixtures-regenerated-u5"

if ((Test-Path -LiteralPath $U2) -or (Test-Path -LiteralPath $U3) -or (Test-Path -LiteralPath $U5)) {
    throw "Um dos diretorios de regeneracao ja existe; use novos nomes."
}

python tools/updater.py fixtures --private "$S\release-private-ep63.pem" --public "$S\release-public-ep63.json" --output-dir $U2
python tools/updater.py fixtures-u3 --private "$S\release-private-ep63.pem" --public "$S\release-public-ep63.json" --output-dir $U3

Copy-Item -Path "$U2\*" -Destination "docs/fixtures/updates/u2" -Force
Copy-Item -Path "$U3\*" -Destination "docs/fixtures/updates/u3" -Force

python tools/updater.py fixtures-u5 --private "$S\release-private-ep63.pem" --public "$S\release-public-ep63.json" --output-dir $U5
Copy-Item -Path "$U5\*" -Destination "docs/fixtures/updates/u5" -Force

python tools/updater.py selftest
```

O `fixtures-u5` deve ser executado somente depois de copiar U2 e U3 novos,
porque ele incorpora `APPLY.ZUP` e `BADHASH.ZUP` desses diretórios. As
fixtures regeneradas são públicas; a chave privada permanece fora do
repositório.

## QEMU: matriz padrão

Ela cobre cache seletivo, três substituições reais, failpoint, recuperação,
aplicação e rollback de arquivos substituídos.

```text
update remote enable
update runtime clear --confirm
update runtime check --tag ep63-runtime
update runtime fetch --tag ep63-runtime --confirm
update runtime verify --cached
update runtime test fail-after 1
update runtime apply --confirm
reboot
update runtime status
update runtime apply --confirm
reboot
update runtime status
update runtime rollback --confirm
reboot
update runtime status
health check
memcheck
regcheck full
update status
update history
```

Esperado: após o primeiro reboot, `0.1.0/e0` com `journal=CLEAN`; após o
segundo, `0.1.1/e0` com `rollback=READY`; após o terceiro, `0.1.0/e0` com
`rollback=DISABLED`.

## Host: Releases A e B para replace/create/delete

A Release A substitui `EXPLORER.BMP`, remove `SHELL.BMP` e substitui
`TASKMGR.BMP`. A Release B substitui `EXPLORER.BMP`, cria `SHELL.BMP` e
remove `TASKMGR.BMP`. Os manifestos devem ser gravados fora do repositório,
ao lado dos payloads indicados.

```powershell
$S = "<SECRETS>"
$BASE = "$S\ep63-runtime-fixtures-qa-2\valid"
$CHANGED = "$S\ep63-runtime-fixtures-qa\valid"
$A = "$S\ep63-runtime-create-delete-A"
$B = "$S\ep63-runtime-create-delete-B"

if ((Test-Path -LiteralPath $A) -or (Test-Path -LiteralPath $B)) {
    throw "Os diretorios A ou B ja existem; use novos nomes."
}

New-Item -ItemType Directory -Path $A, $B | Out-Null
Copy-Item -LiteralPath "$CHANGED\EXPLORER.BMP" -Destination "$A\EXPLORER.BMP"
Copy-Item -LiteralPath "$CHANGED\TASKMGR.BMP" -Destination "$A\TASKMGR.BMP"
Copy-Item -LiteralPath "$BASE\EXPLORER.BMP" -Destination "$B\EXPLORER.BMP"
Copy-Item -LiteralPath "$BASE\SHELL.BMP" -Destination "$B\SHELL.BMP"

$manifestA = [ordered]@{
    format = "ZUM2 v2"
    generation = 6302
    release_tag = "ep63-runtime-a"
    release_id = "ep63-runtime-a"
    target_version = "0.1.1"
    target_epoch = 0
    base_versions = @(
        [ordered]@{ version = "0.1.0"; epoch = 0 }
        [ordered]@{ version = "0.0.9"; epoch = 0 }
    )
    files = @(
        [ordered]@{ path = "EXPLORER.BMP"; operation = "replace_or_create"; source = "EXPLORER.BMP" }
        [ordered]@{ path = "SHELL.BMP"; operation = "delete"; source = $null }
        [ordered]@{ path = "TASKMGR.BMP"; operation = "replace_or_create"; source = "TASKMGR.BMP" }
    )
}
$manifestA | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$A\runtime.json" -Encoding ascii

$manifestB = [ordered]@{
    format = "ZUM2 v2"
    generation = 6303
    release_tag = "ep63-runtime-b"
    release_id = "ep63-runtime-b"
    target_version = "0.1.2"
    target_epoch = 0
    base_versions = @(
        [ordered]@{ version = "0.1.1"; epoch = 0 }
        [ordered]@{ version = "0.1.0"; epoch = 0 }
    )
    files = @(
        [ordered]@{ path = "EXPLORER.BMP"; operation = "replace_or_create"; source = "EXPLORER.BMP" }
        [ordered]@{ path = "SHELL.BMP"; operation = "create"; source = "SHELL.BMP" }
        [ordered]@{ path = "TASKMGR.BMP"; operation = "delete"; source = $null }
    )
}
$manifestB | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$B\runtime.json" -Encoding ascii

python tools/updater.py runtime-build --manifest "$A\runtime.json" --private "$S\release-private-ep63.pem" --public "$S\release-public-ep63.json" --output-dir "$A\valid"
python tools/updater.py runtime-verify --manifest "$A\valid\runtime.zum2" --package "$A\valid\runtime.zephyrosupd" --public "$S\release-public-ep63.json" --system-version 0.1.0 --system-epoch 0

python tools/updater.py runtime-build --manifest "$B\runtime.json" --private "$S\release-private-ep63.pem" --public "$S\release-public-ep63.json" --output-dir "$B\valid"
python tools/updater.py runtime-verify --manifest "$B\valid\runtime.zum2" --package "$B\valid\runtime.zephyrosupd" --public "$S\release-public-ep63.json" --system-version 0.1.1 --system-epoch 0
```

## Host: servidor da Release A

```text
python tools/updater.py serve-runtime --root "<SECRETS>\\ep63-runtime-create-delete-A" --tag ep63-runtime-a --bind 0.0.0.0 --port 8000
```

## QEMU: aplicar Release A

```text
update remote enable
update runtime clear --confirm
update runtime check --tag ep63-runtime-a
update runtime fetch --tag ep63-runtime-a --confirm
update runtime verify --cached
update runtime status
update runtime apply --confirm
reboot
update runtime status
ls
health check
memcheck
regcheck full
```

Esperado: `EXPLORER.BMP` e `TASKMGR.BMP` presentes, `SHELL.BMP` ausente,
`0.1.1/e0`, `journal=CLEAN` e `rollback=READY`.

## Host: servidor da Release B

Pare o servidor A com `Ctrl+C` e inicie:

```text
python tools/updater.py serve-runtime --root "<SECRETS>\\ep63-runtime-create-delete-B" --tag ep63-runtime-b --bind 0.0.0.0 --port 8000
```

## QEMU: aplicar Release B e testar rollback

```text
update remote enable
update runtime clear --confirm
update runtime check --tag ep63-runtime-b
update runtime fetch --tag ep63-runtime-b --confirm
update runtime verify --cached
update runtime status
update runtime apply --confirm
reboot
update runtime status
ls
health check
memcheck
regcheck full
update runtime rollback --confirm
reboot
update runtime status
ls
health check
memcheck
regcheck full
update status
update history
```

Esperado após B: `EXPLORER.BMP` e `SHELL.BMP` presentes e `TASKMGR.BMP`
ausente, com `0.1.2/e0` e `rollback=READY`. Após o rollback:
`EXPLORER.BMP` e `TASKMGR.BMP` presentes, `SHELL.BMP` ausente, com
`0.1.1/e0`, `journal=CLEAN` e `rollback=DISABLED`.

## Auditoria offline após o último reboot

Encerre o QEMU antes da auditoria:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.1 --expect-rollback unavailable --expect-runtime-cache valid --expect-runtime-pending clean
```

Esperado: `Audit image: OK`.

## GitHub Runtime HTTPS

O resultado da validação contra Releases públicas por tag exata está em
[`registro-validacoes.md`](registro-validacoes.md#ep63--github-runtime-v2-via-https).

EP6.0 continua exercitável com o servidor de fixtures U5. EP6.2 mantém o
transporte HTTPS BearSSL configurado para a API GitHub e a regressão HTTP U5;
EP6.3 acrescenta o canal runtime v2 sem reinterpretar os caches `ZUR0/ZUR1`.
Nenhuma consulta ocorre no boot; boot, stage2, kernel e imagem completa
continuam reservados para a EP9.
