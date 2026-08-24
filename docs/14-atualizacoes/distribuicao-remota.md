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
http://10.0.2.2:8000/zephyros/{tag}.json
```

Desde a EP6.2, `config/update-remote.json` usa o formato
`zephyros-update-remote-v2`. Alem do canal U5 HTTP, ele versiona a origem
GitHub HTTPS, proprietario, repositorio, template
`/repos/{owner}/{repo}/releases/tags/{tag}`, versao da API e os nomes exatos
de `release.json`, `release.zum` e `update.zephyrosupd`. O header derivado
contendo essas macros deve permanecer byte a byte sincronizado; o kernel nao
aceita token, conta ou credencial GitHub.

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

O canal U5 legado (`update fetch` e o servidor de fixtures) exige:

- HTTP simples e status `200`;
- `Content-Length` presente e exato;
- no maximo 256 bytes para o manifesto e 128 KiB para o pacote;
- `Connection: close`;
- ausencia de `Transfer-Encoding`, chunked e compressao.

HTTPS e redirects nao fazem parte do contrato legado U5. O fluxo GitHub da
EP6.2 usa a camada HTTPS descrita adiante e nao altera o comportamento desse
canal. URL absoluta interna, POST e troca de origem continuam proibidos.
Timeout ou falha transitoria permitem uma unica repeticao
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
progresso, estado do cache, `update_remote_release_t` e:

- `update_remote_init()`;
- `update_remote_enable()` e `update_remote_disable()`;
- `update_remote_check()`;
- `update_remote_fetch()`;
- `update_remote_release_check()`;
- `update_remote_release_fetch()`;
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
| 17 | `RELEASE_NOT_FOUND` |
| 18 | `RELEASE_FORMAT` |
| 19 | `RELEASE_TAG` |
| 20 | `RELEASE_ASSET` |
| 21 | `RELEASE_CHANGED` |
| 22 | `TLS` |
| 23 | `REDIRECT` |
| 24 | `RELEASE_API` |

As operacoes confirmadas aceitam callback cooperativo. Durante o download,
somente Esc ou F12 e interpretado como cancelamento.

Falha remota nunca degrada verificacao, aplicacao, rollback ou historico
local. O `health` informa `remoto=DISABLED`, `READY` ou `DEGRADED` de forma
independente.

Durante um job do Shell, check, fetch e clear conservam a geracao da
operacao. O cancelamento aguarda o retorno seguro do HTTP e da publicacao de
cache; o resultado so e publicado depois da drenagem. Um resultado tardio de
geracao anterior e registrado e descartado, sem alterar o cache ativo.
`update_remote_status_t.operation_generation` identifica a execucao remota;
`generation` continua reservado a geracao do manifesto/cache.

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
update github check --tag <tag>
update github fetch --tag <tag>
update github fetch --tag <tag> --confirm
```

`update fetch` consulta e apresenta o candidato sem gravar. A confirmacao
repete a consulta e exige exatamente o mesmo manifesto antes de iniciar a
transferencia. O pacote armazenado aparece na aba Pacotes como `ZUR0.ZUP` ou
`ZUR1.ZUP`, mas nunca e aplicado automaticamente.

### Regra operacional EP6.2: tag e ID antes do comando

Antes de passar qualquer comando `update github`, consultar as Releases
publicadas e confirmar o par real `tag_name` + `id`:

```powershell
$releases = Invoke-RestMethod -Uri 'https://api.github.com/repos/BadTonho/ZephyrOS/releases?per_page=20'
$releases | Select-Object tag_name,id,name
```

Uma tag Git isolada nao basta: o kernel consulta
`/releases/tags/{tag}`. Se a consulta retornar vazia, nao existe ID de
Release para o caminho feliz e nenhum comando deve ser passado com uma tag
inventada ou sem a identificacao confirmada. O `id` nao e argumento `--id` do
Shell; ele e lido da resposta da API e deve acompanhar a tag informada.

Somente depois da confirmacao, orientar:

```text
update remote enable
update github check --tag <tag_name-confirmada>
update github fetch --tag <tag_name-confirmada> --confirm
```

Concluida em: 2026-08-22 13:13 (America/Sao_Paulo).

## EP6.0 - Release por tag exata

EP6.0 adiciona uma camada de selecao sobre o transporte U5. A origem da fase
e o servidor de fixtures U5; a API real do GitHub pertence a EP6.2. O template
HTTP configuravel usa exatamente um marcador `{tag}` e somente o esquema
`http://` nesta fase:

```text
http://10.0.2.2:8000/zephyros/{tag}.json
```

O Shell aceita tags de 1 a 64 caracteres usando somente
`[A-Za-z0-9._-]`. A tag e substituida literalmente no template; nao existe
`latest`, ordenacao, comparacao de tags ou escolha automatica de uma Release
"maior".

O descritor deve ser o schema canonico `zephyros-release-v1`, com campos na
ordem abaixo:

```json
{
  "format": "zephyros-release-v1",
  "release_id": "...",
  "release_name": "...",
  "channel": "stable",
  "source_commit": "40 hexadecimais",
  "tag": "tag exata",
  "version_lock": {
    "minimum_version": "MAJOR.MINOR.PATCH",
    "target_version": "MAJOR.MINOR.PATCH",
    "base_epoch": 0,
    "target_epoch": 0
  },
  "assets": {
    "package": {"name": "...ZUP", "size": 0, "sha256": "..."},
    "manifest": {"name": "...zum", "size": 256, "sha256": "..."}
  }
}
```

O resolver valida a tag solicitada, o schema, os nomes e limites dos assets,
os hashes, o `version_lock` e a correspondencia exata da tag. Em seguida,
monta a URL do manifesto, exige que o ZUM1 assinado corresponda aos metadados
publicados e reutiliza o download/cache A/B da U5. O descritor, o nome da
Release e o titulo nao sao uma raiz de confianca.

`update github check` e somente leitura. `update github fetch --tag` executa
o mesmo preflight sem gravar. A forma `--confirm` exige um preflight anterior
para a mesma tag, baixa novamente o descritor e recusa qualquer alteracao
antes de tocar no cache. O ZUPD autenticado e publicado no cache, mas nenhuma
instalacao e iniciada; `update apply` continua sendo uma operacao separada.
Falhas, cancelamento e rede indisponivel preservam o slot ativo anterior.
O System Updater Classic continua sem campo de tag e permanece no canal U5,
servindo como regressao do fluxo remoto existente.

O resultado publico inclui `update_remote_release_t`, com tag, identificacao,
URLs, asset, tamanho e hashes, alem do hash do manifesto ZUM1 em
`update_remote_result_t`.

## EP6.1 - Fundacao de tempo confiavel e contrato TLS

A EP6.1 preparou a identidade do canal que a EP6.2 usa. O RTC/CMOS e
interpretado como UTC, lido somente quando duas amostras estaveis convergem e
validado contra calendario, ano bissexto e a janela 2000-2099. O UTC aceito e
ancorado no contador monotono do PIT; o rollover do tick de 32 bits e exposto
como estado de diagnostico. Sem RTC presente ou com data invalida, o sistema
preserva o monotono, mas recusa o UTC confiavel e falha fechado para qualquer
politica que dependa de validade temporal.

Os contratos publicos sao `rtc.h`, `clock.h` e `tls.h`. `clock status|check`
mostra a fonte, a ancora, os ticks e os autotestes de conversao, calendario,
rollover e invariantes. `tls status|check` mostra a politica e exercita
identidade valida, tempo indisponivel, certificado futuro/expirado, cadeia nao
confiavel, SAN divergente e pin SPKI ausente/correto/divergente.

A politica TLS exige cadeia X.509 confiavel por uma CA estatica, SAN
correspondente ao host e janela de validade baseada em UTC confiavel. A EP6.2
implementa essa politica com BearSSL 0.6 vendorizado, TLS 1.2 apenas, suites
ECDHE com AES-GCM, SNI, verificacao SAN e `br_x509_minimal` com trust anchor
estatico. A entropia externa vem exclusivamente de RDRAND validado por CPUID;
o tempo UTC vem de `clock` e e injetado no validador X.509. Sem qualquer uma
dessas capacidades, o canal falha fechado e nao converte HTTPS para HTTP.

Os trust anchors atualmente fixados sao o Sectigo Public Server Authentication
CA DV E36, usado pela cadeia do endpoint GitHub, e o Let's Encrypt YR1, usado
pelas rotas de assets GitHub observadas no momento da implementacao. A
rotacao de CAs continua sendo uma mudanca versionada do adaptador TLS; pinning
SPKI e reforco opcional e nunca substitui ZUM1/ZUPD.

`tls status|check` continua exercitando a politica e agora informa
`READY`, handshake, X.509, entropia e disponibilidade efetiva de HTTPS.

## EP6.2 - Canal GitHub configuravel

EP6.2 conecta o fluxo de Release por tag ao endpoint oficial do GitHub sem
credenciais no kernel. A requisicao usa `Accept: application/vnd.github+json`,
`X-GitHub-Api-Version` versionado e o template configurado. O parser JSON e
limitado por tamanho, profundidade e buffers estaticos; ele exige `tag_name`
igual a `--tag`, `published_at`, `draft=false`, `prerelease=false` e exatamente
os tres assets configurados. Cada asset valida nome, tamanho, estado
`uploaded`, URL HTTPS para o endpoint GitHub ou seus hosts de objetos e,
quando presente, `digest` no formato `sha256:<64 hex>`.

O fingerprint do preflight cobre tag, id, nome, publicação e os metadados
selecionados dos três assets. O `--confirm` repete a consulta e recusa
qualquer divergência antes de iniciar o download. A API GitHub somente
descobre a Release: `release.json` continua sendo baixado e validado pelo
parser EP6.0, e os hashes, versão, epoch, compatibilidade e assinatura vêm
dos artefatos ZUM1/ZUPD. O manifesto assinado deve apontar, em seu caminho
relativo, para o asset `update.zephyrosupd` publicado sob a mesma origem do
`release.zum`; isso mantém o ZUM1 como autoridade da URL de pacote usada pelo
cache U5.

O HTTP agora aceita `https://`, SNI, headers configuráveis e redirects
absolutos HTTPS limitados a três saltos. Redirect HTTP, downgrade, Location
ausente, host/porta inválidos, falha TLS ou resposta GitHub malformada recebem
motivos públicos próprios (`TLS`, `REDIRECT`, `RELEASE_API`). O download
continua streaming, usa o cache A/B U5, preserva cancelamento e rollback e
nunca chama `update apply`.
O polling HTTP/TLS da EP6.2 ocorre no processo `Zephyr System`, criado com
stack nativa dedicada de 8 KiB para acomodar BearSSL; o Shell mantém sua stack
padrão e recebe somente o resultado cooperativo da consulta. Os buffers
temporários do parser GitHub e da validação do descritor permanecem em
workspaces estáticos, sem alterar as assinaturas públicas do Update.

Para gerar fixtures públicas, sem copiar chaves privadas:

```text
python tools/updater.py fixtures-github --private <chave-fora-do-repo> --public config/update-release-public.json --tag ep62-fixture --output-dir <diretorio-vazio>
python tools/updater.py serve-github --root <diretorio-vazio> --cert <cert-fora-do-repo> --key <chave-tls-fora-do-repo> --tag ep62-fixture --public-host 10.0.2.2 --port 8443
```

As variantes `missing-asset`, `tag-divergent`, `invalid-json`, `bad-digest`,
`draft` e `prerelease` exercitam o parser da API. O certificado da fixture
deve ser emitido para o host configurado e encadear ao trust anchor usado no
build de teste; para testar redirects, use como base da API o mesmo host com
`/fixtures/redirect-http` ou `/fixtures/redirect-https`. Certificado e chave
privada permanecem fora do repositório.

## EP6.3 - Runtime v2, cache seletivo e matriz de falhas

EP6.3 mantém o transporte e o cache U5 v1 intactos e acrescenta um canal
runtime independente. A Release runtime publica `runtime.zum2`,
`runtime.zephyrosupd` e os BMPs individuais. O `release.json` v2 é apenas
metadado de transporte; a compatibilidade e os hashes confiáveis vêm do
manifesto ZUM2 assinado.

O fluxo HTTP por tag usa `runtime-<tag>.json`; a consulta sem tag usa o
manifesto fixo do canal. Quando a origem HTTP por tag está indisponível, o
runtime pode consultar a mesma tag pela API GitHub HTTPS configurada. A API
GitHub exige tag exata, Release publicada, assets `runtime.zum2`,
`runtime.zephyrosupd` e os arquivos do catálogo.

O manifesto é baixado primeiro. Depois de autenticar o ZUM2 e comparar os
hashes locais, o modo seletivo baixa somente os assets necessários; o modo
`--full` baixa o pacote completo. Nenhum desses caminhos aplica arquivos.
O cache usa aliases separados `ZRV0/ZRV1`, com manifesto, pacote, assets,
estado A/B e publicação do slot inativo apenas após validação integral.

Os arquivos controlados e os controles transacionais ficam em namespaces
próprios: `ZTV` para estado/journal, `ZTS` para staging e `ZTB` para backups.
Falhas de DNS, TLS/certificado, UTC, tag, asset, manifesto, assinatura, hash,
cancelamento ou energia preservam o cache ativo e a instalação anterior.

Durante HTTP/TLS, o trabalho executa na stack de 16 KiB do `Zephyr System`.
Ao medir folga de 1 KiB ou menos, o cliente fecha TLS/socket e marca a sessão
como `FAILED` com `ERR_OVERFLOW`; o job remoto é então cancelado sem instalar
nem alterar o cache. Canários de stack rompidos são falhas fatais do kernel.

Comandos do Shell:

```text
update runtime status
update runtime check [--tag TAG]
update runtime fetch [--tag TAG] [--full] [--confirm]
update runtime verify [ARQUIVO|--cached]
update runtime apply --confirm
update runtime rollback --confirm
update runtime clear --confirm
update runtime test fail-after <1-16>
```

Aplicar e rollback exigem confirmação explícita e não reiniciam o sistema. O
Updater Classic possui uma aba `Runtime` com consulta, download seletivo ou
completo, progresso, cancelamento, aplicação, rollback e limpeza. Simple não
recebe funcionalidades novas.

O comando `update runtime test fail-after <1-16>` existe somente para a matriz
de recuperacao da EP6.3. Ele arma uma interrupcao one-shot apos o numero de
arquivos indicado na proxima aplicacao confirmada; o boot seguinte deve
restaurar o estado anterior e limpar journal, staging e backups temporarios.

O procedimento host e a auditoria dos aliases estão em
[contrato-zupd-v2.md](contrato-zupd-v2.md) e
[ferramenta-zupd.md](ferramenta-zupd.md). A validação executável da EP6.3 ainda
deve ser registrada após `make q3check`, `make clean && make` e a matriz QEMU
do usuário.

## EP9.0A — namespace system e preflight ZSYS

A Release combinada v2 mantém os fluxos legados e runtime e acrescenta o
namespace system:

    legacy:  release.zum + update.zephyrosupd
    runtime: runtime.zum2 + runtime.zephyrosupd + assets do catálogo
    system:  system.zsys

O GitHub continua sendo somente transporte. A API deve localizar
system.zsys, mas o ZephyrOS baixa o descritor v2 e o asset em modo streaming,
confere o tamanho e o digest publicado e valida novamente a assinatura,
compatibilidade e hashes do ZSYS. A compatibilidade do JSON é redundante e
deve coincidir com o envelope assinado.

Os comandos somente leitura são:

    update system verify system:/<arquivo.ZSYS>
    update system check --tag <tag>

Não há download para cache, staging, aplicação, reboot automático, slots,
rollback pós-reboot ou escrita de setores crus na EP9.0A.

## Limites de seguranca

Ed25519 e SHA-256 protegem autenticidade e integridade mesmo sobre HTTP.
HTTP simples nao protege confidencialidade, disponibilidade, metadados ou
observacao do trafego. Sem Secure Boot e armazenamento protegido, adulteracao
offline do sistema ou rollback integral do disco permanecem fora do modelo.

Nao existe atualizacao silenciosa, consulta remota no boot, telemetria,
instalacao direta pelo download ou varios candidatos em um mesmo manifesto.
EP6.3 implementa o pacote runtime completo, download seletivo por arquivo,
staging, rollback e a nova versao ZUPD v2 em caches separados; a validacao
executavel no QEMU ainda depende dos gates e da matriz do usuario. O DHCP
automatico pertence ao Network Manager, usa somente RAM e nunca dispara HTTP
ou habilita a distribuicao remota.

## Referencias

- [Contrato ZUPD v1](contrato-zupd-v1.md)
- [System Updater](system-updater.md)
- [Ferramenta host](ferramenta-zupd.md)
- [Comandos do Shell](../09-shell/comandos.md)
- [BearSSL 0.6](https://bearssl.org/)
- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases?apiVersion=latest)
