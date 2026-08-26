# Contrato ZSYS v1

## Resumo

ZSYS v1 é o envelope autenticado da imagem completa do sistema. A imagem
FAT/disco é o payload; o envelope não aplica, grava setores, seleciona slot ou
reinicia a máquina. Na EP9.0A ele serve somente para publicação e preflight.

O arquivo tem um cabeçalho little-endian fixo de 1024 bytes seguido por uma
imagem alinhada a setores. O tamanho máximo da imagem é 8 MiB e o tamanho total
é exatamente 1024 + image_size.

## Layout fixo

| Offset | Tamanho | Conteúdo |
|---:|---:|---|
| 0 | 4 | magic ZSYS |
| 4 | 2 | formato 1 |
| 6 | 2 | tamanho do cabeçalho 1024 |
| 8 | 2 | arquitetura i386 1 |
| 10 | 2 | flags: reboot obrigatório e bridge |
| 12 | 4 | tamanho total do envelope |
| 16 | 4 | início do payload 1024 |
| 20 | 4 | tamanho do payload |
| 24 | 4 | tamanho da imagem |
| 28 | 32 | SHA-256 da imagem completa |
| 60 | 10 | versão e epoch alvo |
| 70 | 2 | quantidade de supported_from |
| 72 | 2 | tamanho da entrada de origem 12 |
| 76 | 96 | até oito origens versão/epoch |
| 172 | 12 | min_updater |
| 184 | 4 | boot_abi |
| 188 | 4 | data_schema_from |
| 192 | 4 | data_schema_to |
| 196 | 1 | rota direct ou checkpoint |
| 197 | 1 | quantidade de checkpoints |
| 198 | 16 | canal |
| 214 | 64 | release_id |
| 278 | 64 | release_tag |
| 342 | 2 | quantidade de componentes 3 |
| 344 | 2 | tamanho da entrada 64 |
| 346 | 192 | boot, stage2 e kernel |
| 538 | 2 | tamanho da entrada de checkpoint 64 |
| 540 | 256 | até quatro checkpoints |
| 796 | 16 | key_id |
| 812 | 64 | assinatura Ed25519 |
| 876 | 4 | tamanho da assinatura 64 |
| 880 | 4 | offset da assinatura 812 |
| 884 | 140 | reservado, sempre zero |

Cada componente contém kind, flags reservadas, offset relativo ao payload,
tamanho, SHA-256 e padding zero. A EP9.0A exige os três componentes contíguos:
boot no offset zero com 512 bytes, stage2 a partir do setor seguinte e kernel
imediatamente depois. A imagem permanece alinhada a 512 bytes; portanto pode
existir padding final autenticado depois do componente kernel, que não é
executado pelo recovery loader.

## Assinatura e confiança

A assinatura é calculada sobre:

    ZEPHYROS-SYSTEM-IMAGE-V1\0 || header_com_assinatura_zerada || imagem

O key_id deve corresponder à chave pública compilada em
src/include/core/update_trust.h. Uma chave desconhecida, assinatura inválida,
hash divergente, truncamento, excesso de tamanho ou desalinhamento recusa o
envelope.

release.json é descoberta e transporte. No descritor combinado
zephyros-release-v2, os namespaces obrigatórios são:

    {
      "legacy": { "package": {}, "manifest": {} },
      "runtime": { "zum2": {}, "zephyrosupd": {}, "assets": [] },
      "system": { "zsys": {} }
    }

Os campos de compatibilidade repetidos no JSON são conferidos contra o ZSYS
assinado; eles não substituem a autoridade do envelope. A identidade também
deve coincidir: release_id e release_tag do ZSYS precisam corresponder ao
descritor e ao tag selecionado.

## Compatibilidade EP9.0A

- supported_from: lista de versões/epochs aceitos; a imagem atual é a primeira
  origem publicada.
- min_updater: versão/epoch mínimo do updater.
- boot_abi: 1.
- data_schema_from e data_schema_to: 1 e 1.
- requires_reboot: true.
- upgrade_route.kind: direct.
- upgrade_route.checkpoints: lista suportada, vazia nesta etapa.
- bridge_required: false.

O preflight rejeita downgrade, origem não suportada, ABI/schema incompatíveis e
identidade divergente. A validação de um slot persistido também aceita a
imagem da versão atualmente instalada, sem transformar essa imagem em
candidato de atualização.

## EP9.1 — Slots e staging

O serviço `update_system_slots` usa aliases 8.3 na raiz do volume FAT32
`system:/`:

| Alias | Função |
|---|---|
| `ZSA0.ZSY` | envelope completo do slot A |
| `ZSB0.ZSY` | envelope completo do slot B |
| `ZSI0.STA`, `ZSI1.STA` | cópias do estado redundante |
| `ZSI0.JRN`, `ZSI1.JRN` | cópias do journal redundante |
| `ZSTG.ZSY` | entrada temporária única de staging |

Os dois arquivos de slot armazenam o envelope ZSYS completo, não somente a
imagem. O estado de 512 bytes tem magic `ZSI1`, sequência monotônica, slot
ativo, slot pendente, flags de recuperação e metadados de A/B. Cada metadado
contém versão, epoch, tamanho total do envelope, SHA-256, `release_id` e
`release_tag`; os últimos 32 bytes do estado são o SHA-256 dos primeiros 480
bytes.

O journal de 512 bytes usa magic `ZSJ1` e as fases `PREPARED`, `STAGING`,
`VERIFIED` e `COMMITTED`. Na inicialização, a cópia válida de maior sequência
é escolhida. Uma cópia corrompida pode ser tolerada quando a outra é válida;
duas cópias inválidas deixam o serviço `DEGRADED`, sem reparo silencioso.
Sem estado válido, os campos públicos de slot ativo, pendente, anterior e em
tentativa usam o sentinela `NONE`; nenhum índice de slot pode ser inferido de
uma estrutura zerada.
Interrupções preservam o slot ativo. Staging incompleto é descartado quando
o journal ainda está em `PREPARED`/`STAGING`; um staging verificado pode ser
republicado como slot pendente durante a recuperação.

O escritor FAT32 de slots usa buffer fixo de 64 KiB e grava clusters em chunks;
nenhuma operação aloca a imagem de até 8 MiB inteira. O arquivo temporário é
controlado pelo journal e removido depois do aborto ou da publicação. O slot
ativo nunca é substituído nesta etapa.

### Matriz de recuperacao

O alvo `system-slots-matrix` deriva imagens de recuperacao sem re-assinar o
envelope. Os casos `STATE_ONE_BAD`, `STATE_BOTH_BAD`, `STATE_NEWER`,
`JOURNAL_PREPARED`, `JOURNAL_STAGING`, `JOURNAL_VERIFIED`,
`JOURNAL_COMMITTED`, `JOURNAL_NEWER`, `JOURNAL_ONE_BAD`,
`JOURNAL_BOTH_BAD`, `NO_SPACE` e `NO_VOLUME` alteram apenas os aliases de
controle da fixture. A validacao
manual inicia um caso por vez com `run-system-slots-matrix` e confirma que o
slot A permanece preservado, que o journal e escolhido pela maior sequencia
valida e que duas copias invalidas deixam o servico degradado.

## Ferramentas e comandos

No host:

    python tools/updater.py system-build ...
    python tools/updater.py system-verify --package <arquivo.ZSYS> --public <chave.json>
    python tools/updater.py release-v2-build ...
    python tools/updater.py release-v2-check --release <release.json> --public <chave.json>
    python tools/updater.py fixtures-system ...

No Shell:

    update system verify system:/<arquivo.ZSYS>
    update system check --tag <tag>
    update system slots
    update system stage system:/VALID.ZSYS
    update system stage system:/VALID.ZSYS --confirm

`update system slots` somente consulta o estado. `stage` sem `--confirm` faz o
preflight completo sem gravar; com `--confirm`, repete a validação, copia o
envelope local para o slot inativo, verifica tamanho/hash/assinatura e publica
o slot como pendente. O comando aceita somente arquivos locais no volume
`system:/`; a EP9.3 acrescenta download, aplicação e cancelamento sem remover
esse caminho local. A verificação usa leitura em streaming e não aloca a
imagem inteira.

### Fixture local no FAT32

O contrato publicado continua usando o asset `system.zsys`. Na EP9.4A, a
imagem híbrida de 64 MiB preserva o FAT12 bruto no início e monta a partição
FAT32 `ZEPHYROS` a partir do LBA 4096. O alvo `system-fixtures` injeta cada
envelope na raiz FAT32 usando o nome físico `.ZSYS`; não há truncamento para
`.ZSY` e o FAT12 legado permanece disponível somente por `legacy:/`.

Cada fixture é gravada em uma imagem própria em
`build\system-fixture-images`. A geração exige uma chave privada Ed25519
configurada somente no `Makefile.local`; nenhum segredo é versionado. O alvo
`run-system-fixture`, recebendo um dos caminhos gerados, inicia a imagem
selecionada para o teste manual sem alterar boot ou stage2.

Os nomes da matriz EP9.0A são `VALID.ZSYS`, `TRUNC.ZSYS`, `HDRBAD.ZSYS`,
`PAYBAD.ZSYS`, `SIGBAD.ZSYS`, `OVERSIZ.ZSYS`, `MISALGN.ZSYS`, `VERBAD.ZSYS`,
`EPCHBAD.ZSYS`, `ABIBAD.ZSYS`, `SCHBAD.ZSYS`, `IMGHASH.ZSYS` e
`CMPHASH.ZSYS`. No Shell, o caminho da fixture válida é
`system:/VALID.ZSYS`.

O alvo `system-slots-fixtures` acrescenta uma imagem com `ZSA0.ZSY` semeado
por um baseline assinado da versão atual, `ZSB0.ZSY` ausente, as duas cópias
iniciais `ZSI*.STA` e `VALID.ZSYS` como candidato. A chave privada continua
exclusiva de `Makefile.local`; a imagem normal não depende dela.

## EP9.2 — Confirmação de tentativa pelo kernel

O estado de slots v2 preserva a leitura de registros v1 e acrescenta o slot
anterior, slot em tentativa, estado de boot (`NONE`, `ATTEMPTED` ou `FAILED`),
motivo e sequência da tentativa. Os bytes reservados permanecem zero e o
SHA-256 continua protegendo os primeiros 480 bytes do registro.

O endereço físico `0x2800` é reservado para o handoff `ZSBH` de 64 bytes entre
o loader pré-kernel e o kernel. Quando presente, `update_system_slots_boot_confirm()`
aceita o handoff somente se slot, anterior, sequência de estado e sequência da
tentativa coincidirem com o estado persistido; então promove o slot e limpa o
pendente. O `stage2` legado limpa essa área antes de carregar o kernel, para
que um boot sem loader nunca confirme uma tentativa antiga.

Depois da ativacao da paginacao, a pagina fisica `0x2000–0x2FFF` permanece
identity-mapped como supervisor-only ate o consumo do handoff. Esse mapeamento
inclui o bloco VESA e `ZSBH`, mas nao concede acesso ring 3 nem amplia o mapa
para a pagina zero ou para as demais lacunas de memoria baixa.

### EP9.2A - Recovery loader fixo

O layout legado usa LBAs fixos: `boot` em 0, `stage2` a partir de 1, kernel
legado no LBA 64 e recovery loader na janela LBA 3000..4095, antes do FAT32.
`boot.asm`
permanece inalterado. O `stage2` carrega o loader para a janela
0x00900000..0x00A00000 e entrega mapa de memoria, VESA, LBA e tamanho do
fallback. O build recusa loader maior que a janela, kernel maior que
0x00100000..0x00800000 ou payload que alcance o FAT32 no LBA 4096.

O loader permanece em modo protegido e usa um gateway fixo do `stage2` para
leituras e escritas BIOS EDD (`INT 13h/AH=42` e `AH=43`) de um setor por vez,
mantendo CHS como fallback quando EDD nao estiver disponivel. DAP, estado do
gateway e bounce buffer ficam reservados em memoria baixa. O cursor de leitura
mantem cluster e deslocamento entre blocos, de modo que cada passagem de hash,
assinatura ou copia percorra a cadeia FAT linearmente. O loader implementa
somente o subconjunto FAT32 da raiz e dos aliases 8.3 de slots/controles,
escolhe o maior `ZSI*.STA` valido,
recusa journal pendente e verifica em streaming SHA-256 do envelope, `key_id`,
Ed25519, hash da imagem e os tres hashes de componentes contiguos. Somente o
kernel e copiado para 0x00100000; boot e stage2 sao autenticados, nao
executados.

O `stage2` configura VESA em modo real depois de carregar o loader fixo e antes
de transferir controle para seu runtime protegido. O loader desenha os
diagnósticos confiáveis diretamente no framebuffer de 24 ou 32 bpp e preserva
VGA texto quando VESA não estiver disponível. Não há uma segunda troca de modo
depois da autenticação, e o bloco VESA entregue ao kernel é o mesmo produzido
pelo caminho legado comprovado. Uma falha VESA não autoriza outro payload nem
interrompe o boot autenticado: o kernel usa o fallback Simple. A entrada do
kernel recria `ESP=0x9F000`, entrega o mapa E820 em `ESI` e o bloco VESA em
`EDI`. Nenhum argumento e empilhado pela ponte, pois a propria entrada do
kernel converte esses registradores para a chamada C.

Na entrada do recovery loader, os argumentos cdecl recebidos do `stage2` são
preservados antes da limpeza da BSS e repassados explicitamente para
`recovery_loader_main(mmap, vesa)`. O endereço de retorno do shim nunca integra
o contexto de boot. Na saída, a ponte para o kernel converte novamente esses
dois endereços para o contrato em registradores descrito acima.

Antes de iniciar um pendente, o loader grava e rele a outra copia de estado
como v2 com `ATTEMPTED`, slot anterior e sequencias novas, e publica `ZSBH`.
Uma tentativa sem confirmacao, ou um pendente que falha na validacao, vira
`FAILED`, limpa `pending` e preserva o envelope. Qualquer recusa apresenta
diagnostico VGA local e tenta o kernel legado apenas apos conferir seu SHA-256
incorporado pelo build. As copias de estado precisam existir com 512 bytes;
o loader nunca cria, realoca ou remove arquivos FAT32.

### EP9.2B - Menu pre-kernel e recuperacao interativa

O `stage2` oferece ao loader o gateway privado
`recovery_bios_wait_key(timeout_ticks)`. O gateway retorna temporariamente ao
modo real, consulta o teclado com `INT 16h` e mede o timeout pelos ticks BIOS,
incluindo o wrap diario. Antes de devolver o controle, restaura segmentos,
pilha, interrupcoes desabilitadas e modo protegido. O loader nao consulta
diretamente as portas PS/2 e a ABI publica `recovery_loader_main(mmap, vesa)`
permanece inalterada.

No fluxo normal existe uma janela de dois segundos para F8. Quando F8 abre o
menu voluntariamente, ele nao expira; setas mudam a selecao, Enter executa a
acao e Esc continua a selecao automatica sem gravar estado. Uma recusa de
estado, FAT32, journal, assinatura, hash ou metadados abre o mesmo menu por dez
segundos. Nesse caso, timeout ou Esc tenta o slot anterior autenticado e usa o
kernel legado autenticado quando o anterior nao estiver disponivel ou valido.

O menu exibe sequencia, ativo, pendente, anterior, tentativa, sequencia da
tentativa, estado de boot, ultimo motivo e validade/versao dos slots A e B. As
acoes sao habilitadas conforme o estado confiavel disponivel: continuar o boot
automatico, iniciar o anterior uma unica vez, tentar novamente o candidato ou
iniciar o kernel legado. Sem FAT32 utilizavel, sem ao menos uma copia de estado
confiavel ou com journal pendente, somente o legado fica habilitado. O retry,
por sua vez, exige tambem a copia alternativa prealocada com 512 bytes.
Os dois aliases de controle nao podem apontar para o mesmo cluster, e o
controle alternativo nao pode compartilhar cluster com as cadeias de A ou B.
Erro de leitura ao procurar journal nao equivale a journal ausente e restringe
o fluxo ao legado.

O boot one-shot do anterior revalida integralmente cabecalho, chave,
assinatura, imagem e componentes do ZSYS. Ele nao altera o estado persistido e
nao publica `ZSBH`; portanto `update system slots` continua mostrando o ativo
persistido e o proximo boot automatico volta a esse slot.
Essa revalidacao tambem exige alinhamento da imagem e os campos estruturais de
canal, bases, politica de reboot, ABI, schemas, rota, checkpoints e padding. O
loader aceita somente a geometria limitada da particao FAT32 `ZEPHYROS`; um
alias de diretorio ou LBA fora dos limites declarados nunca habilita escrita ou
execucao. A versao, o epoch, o release ID e a release tag do registro de slot
devem coincidir byte a byte com os campos assinados do cabecalho ZSYS; qualquer
divergencia resulta em `FORMAT`.

O retry manual so fica disponivel para estado v2 `FAILED`, candidato diferente
do ativo, alias presente, metadados coerentes e a copia redundante alternativa
de 512 bytes prealocada. A primeira confirmacao seleciona a acao e a segunda
confirmacao com Enter autoriza a tentativa. Esc retorna ao menu sem escrita.
Depois da revalidacao integral, o loader grava e rele `ATTEMPTED`, incrementa
as sequencias de estado e tentativa, publica `ZSBH` e inicia o kernel
candidato. A confirmacao pelo kernel continua exclusiva de
`update_system_slots_boot_confirm()`.

Um reset antes do acknowledge transforma a tentativa em `FAILED` no boot
seguinte e nao produz retry automatico. Uma revalidacao recusada volta ao menu,
desabilita novo retry durante aquela sessao e registra o motivo especifico
`SIGNATURE`, `HASH`, `FORMAT`, `SIZE`, `IO`, `JOURNAL` ou `BOOT_FAILED` quando
o controle persistente continua seguro. Falha de escrita ou releitura,
sequencia esgotada ou divergencia entre controles restringe a sessao ao kernel
legado autenticado. O loader continua sem criar, realocar ou remover arquivos
FAT32 e nenhum caminho interativo autoriza um payload nao autenticado.

### EP9.3 - Cache, aplicacao e cancelamento

O cache remoto ZSYS e independente dos caches ZUPD v1 e runtime v2. Ele usa
`ZSC0.ZSY`/`ZSC1.ZSY` para pacotes, `ZSC0.STA`/`ZSC1.STA` para controles de
512 bytes e `ZSCT.ZSY` como nome temporario transacional. Cada controle v1
inclui sequencia, copia ativa, tamanho e SHA-256 do pacote, versao, epoch, tag,
release ID, alias e SHA-256 do proprio registro. A copia valida de maior
sequencia vence; duas copias invalidas deixam somente o cache degradado.

O download escreve no cache inativo em blocos limitados. A Release v2, o
asset, Ed25519, imagem e componentes sao validados durante a transferencia; o
arquivo publicado e reaberto e verificado novamente antes do commit do
controle. Cancelamento, falta de espaco, I/O ou queda de energia preservam o
cache ativo anterior. O temporario orfao e ignorado e removido na proxima
inicializacao.

Os comandos publicos sao `update system status`, `check --tag`, `fetch --tag
[--confirm]`, `verify <arquivo>|--cached`, `apply [--confirm]`, `cancel
[--confirm]`, `slots` e o diagnostico local compativel `stage`. Fetch sem
confirmacao faz somente preflight; fetch confirmado publica apenas o cache.
Apply revalida o cache e reutiliza a transacao dos slots para marcar o slot
inativo como pendente. A ativacao nunca e imediata: o usuario executa `reboot`
separadamente.

Cancel sem confirmacao nao grava. A operacao confirmada recusa journal,
tentativa em andamento, estado divergente ou sequencia esgotada; depois limpa
o pendente com releitura confirmada e somente entao despublica o cache. O
arquivo do slot candidato nunca e removido. Estado `FAILED` e seus metadados
permanecem disponiveis para diagnostico e retry pelo menu pre-kernel.
