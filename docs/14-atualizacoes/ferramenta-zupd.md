# Ferramenta Host ZUPD v1

## Escopo

`tools/updater.py` gera chaves Ed25519, monta artefatos deterministas, verifica
ZUPD v1, produz fixtures publicos e sincroniza a raiz publica usada pelo
kernel. A ferramenta host usa `cryptography==50.0.0`, fixado em
`tools/requirements-updater.txt`.

A atualizacao da dependencia e a restricao explicita do servidor de fixtures
ao TLS 1.2 foram implementadas em: 2026-08-22 15:03 (America/Sao_Paulo).

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

Para a EP6.2, `fixtures-github` cria somente os tres assets publicos da
Release (`release.json`, `release.zum` e `update.zephyrosupd`) e um manifesto
de tamanhos/hashes. A chave Ed25519 privada continua sendo lida de um caminho
externo e nao e copiada para a saida:

```text
python tools/updater.py fixtures-github --private <chave-fora-do-repo> --public config/update-release-public.json --tag ep62-fixture --output-dir <diretorio-vazio>
```

`serve-github` publica a rota compativel com a API oficial e os downloads em
HTTPS usando certificado e chave TLS fornecidos externamente:

```text
python tools/updater.py serve-github --root <diretorio-vazio> --cert <cert-fora-do-repo> --key <chave-tls-fora-do-repo> --tag ep62-fixture --public-host 10.0.2.2 --port 8443
```

O servidor de fixture aceita somente TLS 1.2, alinhado ao cliente HTTPS do
kernel; ele nao e um endpoint de producao.

As variantes `missing-asset`, `tag-divergent`, `invalid-json`, `bad-digest`,
`draft` e `prerelease` sao selecionadas com `--variant`. A fixture deve usar
um certificado emitido para o host configurado e encadeado a um trust anchor
presente no build de teste. Para testar redirects, configure temporariamente
`github_api_url` para `https://<host>:8443/fixtures/redirect-http` ou
`https://<host>:8443/fixtures/redirect-https`. Nenhum certificado ou chave
privada de teste deve ser salvo no repositorio.

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

Para EP6.2, depois de `make q3check` e `make clean && make`, a matriz QEMU
inclui:

```text
health
clock status
clock check
tls status
tls check
regcheck full
memcheck
update remote enable
update github check --tag <tag>
update github fetch --tag <tag> --confirm
```

O check nao grava; o fetch publica somente no cache U5 e nao instala. A matriz
de fixtures deve repetir assets ausentes, tag divergente, JSON invalido,
digest divergente, draft/prerelease e redirects HTTP/HTTPS, confirmando que
falha TLS ou redirect preserva o slot ativo. Um smoke separado pode consultar
anonimamente uma tag existente de `BadTonho/ZephyrOS`.

## EP6.3: runtime v2 e fixtures

O manifesto de build `ZUM2 v2` usa os campos `generation`, `release_tag`,
`release_id`, `target_version`, `target_epoch`, `base_versions` e `files`.
Cada arquivo declara `replace`, `create`, `replace_or_create` ou `delete` e
um `source` relativo; a remoção usa `source: null`. O catálogo deve conter
exatamente `EXPLORER.BMP`, `SHELL.BMP` e `TASKMGR.BMP`.

```text
python tools/updater.py runtime-build --manifest <runtime.json> --private <chave-fora-do-repo> --public config/update-release-public.json --output-dir <diretorio-novo>
python tools/updater.py runtime-verify --manifest <diretorio-novo>/runtime.zum2 --package <diretorio-novo>/runtime.zephyrosupd --public config/update-release-public.json
```

`runtime-build` recusa sobrescrita e publica `runtime.zum2`,
`runtime.zephyrosupd`, assets individuais e `release.json`. `runtime-verify`
não grava e pode receber `--system-version`/`--system-epoch` para verificar
uma base específica. O manifesto assinado e o pacote completo são validados
independentemente do descritor JSON.

Fixtures EP6.3 e servidores locais:

```text
python tools/updater.py fixtures-runtime --private <chave-fora-do-repo> --public config/update-release-public.json --output-dir <diretorio-vazio> --changed-assets
python tools/updater.py serve-runtime --root <diretorio-das-fixtures> --tag ep63-runtime --variant valid
python tools/updater.py serve-github-runtime --root <diretorio-das-fixtures> --cert <certificado-fora-do-repo> --key <chave-tls-fora-do-repo> --tag ep63-runtime --variant valid
```

As variantes HTTP cobrem asset ausente, tag divergente, JSON inválido,
digest divergente, manifesto adulterado e pacote adulterado. A API GitHub
runtime acrescenta draft/prerelease e digest divergente. `selftest` cobre
round-trip de assinatura, bases múltiplas, substituição, criação, remoção,
hash e adulteração sem escrever no repositório.

`audit-image` também confere `ZRV0/ZRV1`, manifesto, pacote, assets, atributos
hidden/system/archive e slots inativos. Para o runtime instalado, decodifica
os controles redundantes `ZTV0/ZTV1` (`ZRT2`), o journal `ZRTJ`, os três
arquivos do catálogo e os aliases `ZTS/ZTB`, incluindo hashes dos backups de
rollback. Exemplos:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-runtime-cache valid --expect-runtime-alias ZRV0.MAN --expect-runtime-pending clean
python tools/updater.py audit-image --image build/zephyros.img --expect-runtime-cache empty
```

Depois de um `update runtime apply --confirm` ou rollback, a auditoria deve
ser executada somente após o reboot solicitado pelo sistema. Uma imagem com
transaction journal v2, staging ou backup inesperado falha na auditoria; use
`--allow-pending` apenas para inspecionar deliberadamente uma interrupcao.

### Matriz final de validacao EP6.3

Os gates de codigo e a execucao QEMU desta matriz pertencem ao usuario:

```text
make q3check
make clean && make
make run
```

Para exercitar o fallback GitHub HTTPS com a fixture local, gere os artefatos
runtime e inicie o servidor com um certificado cujo SAN corresponda a
`10.0.2.2`:

```text
python tools/updater.py fixtures-runtime --private <chave-fora-do-repo> --public config/update-release-public.json --output-dir <diretorio-vazio>
python tools/updater.py serve-github-runtime --root <diretorio-das-fixtures> --cert <certificado-fora-do-repo> --key <chave-tls-fora-do-repo> --tag ep63-runtime --public-host 10.0.2.2 --port 8443
```

Em uma configuracao de validacao temporaria, aponte `github_api_url` para
`https://10.0.2.2:8443`, regenere `src/include/core/update_remote_config.h`
com `sync-remote` e mantenha o endpoint HTTP runtime indisponivel. No QEMU:

```text
update runtime status
update runtime check --tag ep63-runtime
update runtime fetch --tag ep63-runtime --confirm
update runtime verify --cached
```

O caminho deve consultar a API por HTTPS, validar a tag exata, o manifesto e
os hashes, e publicar somente o cache. Depois, valide o failpoint:

O argumento `--changed-assets` faz a fixture inverter os pixels dos tres BMPs,
garantindo que a aplicacao tenha tres substituicoes reais. Sem esse argumento,
os assets sao iguais aos arquivos ja injetados na imagem base e o preflight
normalmente mostra `reutilizados=3`.

```text
update runtime test fail-after 1
update runtime apply --confirm
reboot
update runtime status
```

O reboot deve recuperar o estado anterior com `journal=CLEAN`, sem staging ou
backup temporario pendente. Para o rollback real, aplique uma Release valida,
reinicie, confirme `rollback=READY`, execute `update runtime rollback --confirm`
e reinicie novamente. A verificacao final deve mostrar a versao anterior,
`rollback=DISABLED`, `pending=NO` e os aliases `ZTS/ZTB` limpos.

Para cobrir as tres operacoes em uma mesma transacao, use dois manifestos
customizados: a Release A deve substituir `EXPLORER.BMP`, remover
`SHELL.BMP` e manter/substituir `TASKMGR.BMP`; apos o reboot, a Release B deve
substituir `EXPLORER.BMP`, criar `SHELL.BMP` e remover `TASKMGR.BMP`. O rollback
da Release B deve restaurar simultaneamente replace, create e delete. Em cada
fase, execute `health`, `memcheck`, `regcheck full` e `update runtime status`,
e depois rode `audit-image` com `--expect-runtime-pending clean`.

Ao terminar a fixture local, restaure os valores versionados de
`config/update-remote.json` e regenere o header original com `sync-remote`
antes de qualquer commit.

## EP9.0A — ZSYS v1 e Release combinada

A ferramenta também constrói e verifica o envelope da imagem completa do
sistema. O manifesto de entrada declara identidade, versão/epoch alvo,
origens suportadas, updater mínimo, ABI de boot, schema de dados, canal e rota.
O comando system-build recebe build/zephyros.img, build/boot.bin,
build/stage2.bin e build/kernel.bin e recusa componentes que não sejam o
prefixo correspondente da imagem.

    python tools/updater.py system-build --manifest system.json --image build/zephyros.img --boot build/boot.bin --stage2 build/stage2.bin --kernel build/kernel.bin --private <chave-fora-do-repo> --public config/update-release-public.json --output system.zsys
    python tools/updater.py system-verify --package system.zsys --public config/update-release-public.json

release-v2-build publica um release.json de transporte com os namespaces
legacy, runtime e system. A verificação revalida ZUPD v1/ZUM1, ZUM2/ZUPD v2 e
ZSYS e compara a compatibilidade redundante do JSON com a compatibilidade
autenticada do ZSYS.

    python tools/updater.py release-v2-build --release <id> --release-name <nome> --legacy-dir <legacy> --runtime-dir <runtime> --system-dir <system> --private <chave-fora-do-repo> --public config/update-release-public.json --source-commit HEAD --tag <tag> --output-dir <diretorio-novo>
    python tools/updater.py release-v2-check --release <diretorio>/release.json --public config/update-release-public.json

fixtures-system gera vetores válidos, truncados, desalinhados, excessivos,
adulterados, incompatíveis e com divergência de hash. O selftest host também
cobre a correlação entre os namespaces da Release v2 e regressões dos
descritores legados e runtime.

    python tools/updater.py fixtures-system --manifest system.json --image build/zephyros.img --boot build/boot.bin --stage2 build/stage2.bin --kernel build/kernel.bin --private <chave-fora-do-repo> --public config/update-release-public.json --output-dir <diretorio-vazio>

### EP9.4A — volume híbrido FAT32

O empacotador prepara `build\zephyros.img` como uma imagem híbrida de 64 MiB:
o payload FAT12 legado permanece no início e a partição FAT32 `ZEPHYROS`
começa no LBA 4096. O MBR é alterado somente no artefato gerado; `boot.asm`
e `stage2` não são modificados.

    python tools/packager.py prepare-hybrid-image --image build\zephyros.img --disk-bytes 67108864 --fat32-start-lba 4096 --label ZEPHYROS
    python tools/packager.py inject-file-fat32 --file build\system-fixtures\valid.zsys --image build\system-fixture-images\VALID.img --path VALID.ZSYS --fat32-start-lba 4096 --replace

Os alvos de imagem principal, pacotes, ícones, atualizações e fixtures usam
`inject-file-fat32`. `inject-file` continua preservado para regressão explícita
das imagens FAT12 antigas. O runtime monta automaticamente exatamente um
volume FAT32 rotulado `ZEPHYROS`, com leitura/escrita para o volume de sistema;
volumes externos continuam somente leitura por padrão.

O backend FAT32 valida BPB, FSInfo, backup, cópias da FAT, cadeias, diretórios,
LFN UTF-16LE e checksum antes de expor a leitura. As escritas reservam
clusters, sincronizam as duas FATs e publicam a entrada LFN/8.3 por último.
`storage check <id>` é somente leitura. Journaling, filesystem nativo e boot
direto pelo FAT32 permanecem etapas posteriores.

### Layout vigente da imagem hibrida

A imagem principal atual permanece com 256 MiB e usa o FAT12 legado no inicio,
o kernel legado no LBA 64, o recovery loader no LBA 6144 e o FAT32 `ZEPHYROS`
no LBA 8192. O espaco entre as janelas evita que o crescimento do kernel ou
do loader sobrescreva outra regiao; os exemplos EP9.4A acima permanecem
referencia historica para imagens antigas com FAT32 no LBA 4096.

## Referencias

- [cryptography 49.0.0](https://cryptography.io/_/downloads/en/49.0.0/pdf/)
- [Ed25519 no cryptography](https://cryptography.io/en/49.0.0/hazmat/primitives/asymmetric/ed25519/)
- [Serializacao de chaves](https://cryptography.io/en/49.0.0/hazmat/primitives/asymmetric/serialization/)
- [Distribuicao remota ZUPD v1](distribuicao-remota.md)
- [Contrato ZUM2/ZUPD v2](contrato-zupd-v2.md)
- [BearSSL 0.6](https://bearssl.org/)
- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases?apiVersion=latest)
