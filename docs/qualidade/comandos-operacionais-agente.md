# Memoria operacional do agente — comandos e validacoes gerais

> Documento de contexto operacional para agentes. Nao e contrato publico e nao
> deve armazenar senhas, tokens, chaves privadas, certificados privados,
> caminhos pessoais ou outros dados sensiveis.

## Regra de consulta

Consultar este arquivo antes de orientar comandos, builds ou validacoes do
projeto. O catalogo de comandos visiveis ao usuario fica em `comandos.md`; as
regras de trabalho ficam em `AGENTS.md`; os procedimentos especificos de cada
tarefa ficam em seus documentos canonicos e nos roadmaps.

Nao inventar comandos, argumentos, tags, IDs, endpoints ou opcoes do QEMU. Se
um valor dinamico nao estiver neste arquivo ou na saida mais recente fornecida
pelo usuario, usar a fonte real do projeto ou pedir a saida correspondente.

## Gates de build e QEMU

Depois de alterar codigo, header ou Makefile, o usuario executa os gates
operacionais:

```text
make q3check
make clean && make
make run
```

O agente nao executa `make`, build, testes ou QEMU neste projeto. Depois que o
usuario confirmar esses gates para a mesma versao do codigo, eles nao devem ser
reapresentados como testes funcionais pendentes da fase.

Nao adicionar opcoes genericas ao comando de execucao. Em especial, nao
sugerir `-cpu max` como parte do fluxo atual: essa opcao nao pertence ao
comando documentado enquanto nao estiver explicitamente configurada no
`Makefile`.

## Comandos host verificados

Para instalar as dependencias Python fixadas pelo atualizador:

```text
python -m pip install -r tools/requirements-updater.txt
```

O autoteste do atualizador pode ser executado pelo usuario com:

```text
make update-test
python tools/updater.py selftest
```

Para conferir ou gerar a configuracao remota, os comandos suportados sao:

```text
python tools/updater.py check-remote --config config/update-remote.json --header src/include/core/update_remote_config.h
python tools/updater.py sync-remote --config config/update-remote.json --output src/include/core/update_remote_config.h
```

`sync-remote` gera um header e somente deve ser usado quando essa alteracao
estiver dentro do escopo da tarefa. Nao sobrescrever configuracao de producao
por tentativa ou por inferencia.

O servidor HTTP local da integracao U5 usa:

```text
python tools/updater.py serve-u5 --root docs/fixtures/updates/u5 --port 8000
```

Procedimentos especificos de distribuicao remota ficam na documentacao
canonica da funcionalidade, nao nesta memoria geral.

## EP9.0A: fixtures ZSYS para QEMU

O alvo `system-fixtures` gera uma matriz compacta assinada usando a chave
privada indicada por `SYSTEM_PRIVATE_KEY` em `Makefile.local`. A chave privada
permanece fora do repositorio. A imagem de fixture e hibrida: FAT12 legado no
inicio e FAT32 `ZEPHYROS` a partir do LBA 4096. Os arquivos usam a extensao
`.ZSYS` no FAT32.

Cada fixture e gravada em uma imagem propria dentro de
`build\system-fixture-images`. O alvo nao injeta a matriz inteira em
`build\zephyros.img`; cada imagem armazena somente um envelope ZSYS na raiz
FAT32. Antes da primeira geracao apos uma tentativa antiga, recrie a imagem
base com `make clean` e `make`.

Depois de gerar as fixtures, inicie uma imagem por vez com estes comandos
completos:

```text
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\VALID.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\TRUNC.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\HDRBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\PAYBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\SIGBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\OVERSIZ.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\MISALGN.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\VERBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\EPCHBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\ABIBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\SCHBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\IMGHASH.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\CMPHASH.img
```

Dentro de cada QEMU, execute o comando correspondente ao alias da imagem:

```text
update system verify system:/VALID.ZSYS
update system verify system:/TRUNC.ZSYS
update system verify system:/HDRBAD.ZSYS
update system verify system:/PAYBAD.ZSYS
update system verify system:/SIGBAD.ZSYS
update system verify system:/OVERSIZ.ZSYS
update system verify system:/MISALGN.ZSYS
update system verify system:/VERBAD.ZSYS
update system verify system:/EPCHBAD.ZSYS
update system verify system:/ABIBAD.ZSYS
update system verify system:/SCHBAD.ZSYS
update system verify system:/IMGHASH.ZSYS
update system verify system:/CMPHASH.ZSYS
```

Somente `system:/VALID.ZSYS` deve ser aceito; os demais devem ser recusados
sem alterar imagem, cache, FAT12 legado ou estado persistente.

Para o diagnostico somente leitura do volume FAT32, primeiro copie o ID exato
mostrado por `storage list` e execute:

```text
storage check <id-exato-do-volume-fat32>
```

## EP9.1: matriz de recuperacao dos slots

O alvo `system-slots-matrix` gera imagens independentes em
`build\system-slots-matrix`, a partir da fixture base. O gerador nao precisa
de uma chave privada: ele copia o envelope ja assinado e altera somente os
controles FAT32 da fixture. A matriz cobre uma copia de estado invalida, as
duas copias de estado invalidas, cada fase do journal, journal redundante
parcial ou totalmente invalido, falta de espaco e volume FAT32 ausente.

Gere todas as imagens com:

```text
make system-slots-matrix
```

Inicie cada caso com o comando completo correspondente:

```text
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_ONE_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_BOTH_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_NEWER.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_PREPARED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_STAGING.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_VERIFIED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_COMMITTED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_NEWER.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_ONE_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_BOTH_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\NO_SPACE.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\NO_VOLUME.img
```

Expectativas: `STATE_ONE_BAD` deve continuar `READY`; `STATE_NEWER` deve
selecionar a sequencia 2; `STATE_BOTH_BAD` e `JOURNAL_BOTH_BAD` devem
aparecer `DEGRADED`; `JOURNAL_PREPARED`, `JOURNAL_STAGING` e
`JOURNAL_NEWER` devem preservar A, remover o staging e limpar o journal;
`JOURNAL_VERIFIED` e `JOURNAL_COMMITTED` devem publicar B como pendente;
`JOURNAL_ONE_BAD` deve recuperar usando a copia valida; `NO_SPACE` deve
recusar o preflight com `SPACE`; e `NO_VOLUME` deve deixar os slots
indisponiveis/degradados.

Para cancelamento cooperativo, use uma fixture nova e pressione F12 durante
a copia, antes de consultar novamente o estado:

```text
make run-system-slots-fixture
```

```text
update system stage system:/VALID.ZSYS --confirm
update system slots
```

O resultado esperado e cancelamento sem slot pendente, com A preservado e
journal limpo. A aplicacao, a selecao no boot e o reboot continuam fora da
EP9.1.

## Comandos no Shell

Para orientar comandos do sistema, consultar primeiro `comandos.md` e os
handlers existentes. Preservar o fluxo cooperativo, o cancelamento indicado
no proprio comando e o retorno ao prompt. Nao criar uma tag de teste ou um
argumento que nao exista no dispatcher, no header publico ou no parser da
ferramenta correspondente.

## Identificadores dinamicos

- Copiar tags, IDs de dispositivo, volumes, particoes e endpoints exatamente
  da saida mais recente disponível.
- Nao abreviar, normalizar, trocar separadores ou completar identificadores.

## Registro de etapas

Toda implementacao, validacao ou conclusao de fase deve registrar data e hora
reais em `docs/qualidade/registro-validacoes.md`. O roadmap correspondente
mantem escopo, requisitos, checklists, pendencias e criterios; relatos
cronologicos, saidas e tentativas ficam no registro. Usar o formato:

```text
Concluida em: YYYY-MM-DD HH:MM (America/Sao_Paulo)
```

Nao estimar horarios historicos. Se implementacao e validacao ocorrerem em
momentos diferentes, registrar os dois eventos separadamente.

## Registro deste documento

Memoria operacional geral criada em: 2026-08-22 14:52 (America/Sao_Paulo).
