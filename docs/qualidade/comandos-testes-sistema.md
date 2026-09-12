# Comandos de testes do sistema

Este e o indice operacional dos comandos de validacao disponiveis no
Makefile. Os caminhos de ferramentas podem ser definidos em `Makefile.local`,
que nao e versionado. O compilador `GCC` do kernel e o cross-compiler
freestanding; `HOST_CC` e `HOST_SANITIZE_CC` sao compiladores nativos para os
testes fora do QEMU.

## Configuracao local

Exemplo de variaveis locais, sem caminhos pessoais no repositorio:

```text
HOST_CC=cc
HOST_SANITIZE_CC=clang
QEMU=qemu-system-i386
QEMU_TEST_CPU=max
```

No Windows, `HOST_SANITIZE_CC` pode apontar para o `clang.exe` do LLVM/MSYS2.
O runtime ASan/UBSan precisa estar disponivel na mesma instalacao. Se a
dependencia nao puder ser executada, o resultado correto e `BLOCKED`; nao use
o cross-compiler do kernel como fallback para testes host-only.

## Fluxo minimo apos alterar codigo

Execute os gates antes de abrir a imagem no QEMU:

```text
make q3check
make clean && make
make run
```

`make run` e uma execucao manual do sistema, nao substitui um caso
automatizado. Para verificar somente a infraestrutura do runner QEMU:

```text
make test-qemu-selftest
```

## Gates e ferramentas auxiliares

```text
make q3check
make q3check-test
make catalog-test
make storage-fixtures
make storage-fixtures-test
make storage-fixtures-verify
make package-test
make update-test
```

`catalog-test` valida `tests/catalog.json` e a visao renderizada. Os comandos
de fixtures devem ser executados antes dos testes que dependem de suas
imagens. `package-test` e `update-test` executam os self-tests das ferramentas
de empacotamento e atualizacao.

## SHELL1 — dispatcher e ciclo de vida do prompt

```text
make test-shell1-host
make test-shell1-qemu SHELL1_QEMU_WORKERS=4 SHELL1_QEMU_SEED=2201
make test-shell1
```

O agregado host cobre sucesso, erro do dispatcher, cancelamento, retry quando
o terminal estava indisponivel, historico, entrada, jobs, cenas e o observer
black-box. O caso QEMU `qemu:shell1:prompt-lifecycle` usa snapshot independente
e confirma retorno unico ao prompt e reentrada.

## STO1 — invariantes de Storage

```text
make test-sto1-host
```

O alvo agregado executa Block, cache, FAT12, FAT32, FS e Storage com as
fixtures host-only diretamente afetadas pelo STO1.

## STO2 — sync, flush e transações

```text
make test-sto2-host
```

O agregado executa as fixtures de Block/cache, FAT12, FAT32, FS, Storage,
energia e VFS. Ele verifica a ordem dados → FAT → diretório → flush, sync
repetido, deadlines, durabilidade degradada, publicação de substituições,
rename, delete, streaming e limpeza de temporários. Journaling persistente e
recuperação após reboot ficam para STO6–STO7.

## STO3 — VFS e ciclo de vida dos volumes

```text
make test-sto3-host
```

O agregado executa FS, Storage, energia, processo e VFS/path. Ele verifica o
gate de transição, diagnósticos durante bloqueio, refresh atômico, rollback,
desmontagem ocupada, gerações de montagem, perda de dispositivo, aliases
diagnosticáveis e preservação dos pseudo-filesystems.

## STO4 - verificacao de consistencia

```text
make test-sto4-host
```

O agregado cobre o diagnostico somente leitura de FAT12 e FAT32, MBR/BPB,
copias da FAT, cadeias, tamanhos, LFNs, duplicidades, clusters orfaos e
divergencias de FSInfo. Os fixtures confirmam que `storage check <id>` nao
escreve no volume nem altera o Block Cache.

## STO5 - atualizacao segura do sistema

```text
make test-sto5-host
make system-fixtures
make system-slots-matrix
make test-tst5-qemu-update-recovery
make run-system-slots-matrix
make run-system-update-matrix
```

O agregado host-only cobre ZUPD, ZSYS, runtime, transporte remoto, slots,
Updater, Shell, estado e a politica estatica de confianca. As matrizes QEMU
validam staging, slot inativo, confirmacao, rollback e recuperacao; devem ser
geradas depois de `make q3check` e `make clean` seguido de `make`. Depois da
geracao, os alvos `run-system-slots-matrix` e `run-system-update-matrix`
reutilizam as imagens existentes e nao pedem a chave privada novamente.

## STO6 - recuperacao transacional

```text
make test-sto6-host
make catalog-test
make system-fixtures
make system-slots-matrix
make test-tst5-qemu-update-recovery
make run-system-slots-matrix
make run-system-update-matrix
make test-tst6-qemu-fault-recovery
```

O agregado host-only cobre runtime, slots A/B, journals redundantes, recovery
runtime/menu/loader, Shell, estado e diagnosticos. A matriz deve verificar
recuperacao idempotente, estados ambiguos preservados, limite de duas
tentativas, rollback ao slot anterior e ausencia de residuos. A validacao
QEMU depende das fixtures assinadas e das chaves externas de distribuicao.

## STO7 - matriz adversarial final

```text
make test-sto7-host
make catalog-test
make test-sto7-qemu STO7_QEMU_WORKERS=4 STO7_QEMU_SEED=7007
```

O agregado reutiliza os casos TST4, TST5 e TST6 marcados com `sto7`, sem
duplicar IDs. A matriz cobre Storage/VFS, FAT12/FAT32, USB MSC, falhas de
recursos, cancelamento, reboot, poweroff, atualização, rollback e recuperação.
O runner paralelo grava os artefatos em `build/test-results/sto7/`.

No Ryzen 5 3600, seis workers podem ser usados após a validação inicial:

```text
make test-sto7-qemu STO7_QEMU_WORKERS=6 STO7_QEMU_SEED=7007
```

O alvo não gera fixtures assinadas. Se uma fixture externa ausente exigir
chave privada, a execução deve ser interrompida e a chave fornecida pelo
operador; nenhuma fixture insegura será criada como substituição.

## HW1 - catalogo de perfis de hardware

O manifesto versionado em `config/hardware-profiles.json` define os sete
perfis QEMU obrigatorios: `baseline`, `no-acpi`, `no-nic`, `no-usb`,
`no-vesa`, `no-audio` e `no-storage`. Os perfis preservam serial/QMP,
registram fallback e diagnosticos e usam snapshots independentes. Hardware
fisico permanece `PENDING` ate haver evidencia reproduzivel.

| Perfil | Variacao | Fallback esperado |
|---|---|---|
| `baseline` | ACPI, PCI, VGA e NIC E1000 | nenhum |
| `no-acpi` | ACPI ausente | serial |
| `no-nic` | NIC ausente | rede indisponivel |
| `no-usb` | USB desativado | PS/2 ou serial |
| `no-vesa` | video desativado | serial |
| `no-audio` | AC97 ausente | sem saida de audio |
| `no-storage` | somente disco de boot | disco de boot |

```text
make test-hw1-host
make catalog-test
make test-hw1-qemu HW1_QEMU_WORKERS=4 HW1_QEMU_SEED=2101
make test-hw1
```

O alvo QEMU reutiliza os casos TST6/TST5 associados pela tag `hw1`, gera
artefatos em `build/test-results/hw1/` e prepara as fixtures de Storage para
o caso USB. O intervalo de workers continua sendo 1 a 64; seis workers podem
ser usados no Ryzen 5 3600 depois da validacao inicial. A matriz deve
confirmar `health`, `regcheck full`, `devices` e `device-scan`, alem de
degradacao explicita sem panic ou espera infinita.

## HW2 - inicializacao e ownership dos drivers

O teste host-only exercita diretamente o lifecycle interno e o ownership de
recursos. A matriz QEMU reutiliza os casos `hw2` existentes, sem criar uma
segunda execucao para o mesmo contrato:

```text
make test-hw2-host
make catalog-test
make test-hw2-qemu HW2_QEMU_WORKERS=4 HW2_QEMU_SEED=2102
make test-hw2
```

Os artefatos ficam em `build/test-results/hw2/`. O caso host
`host:drivers:lifecycle` valida transicoes, idempotencia, conflito de IRQ,
DMA, limpeza reversa, callbacks apos quiescencia e geracoes obsoletas. Os
casos QEMU devem terminar em `PASS`, `BLOCKED` justificavel ou degradacao
esperada e verificar `health`, `regcheck full`, `devices`, `device-scan` e
`vfs status`.

## HW3 - entrada, video e audio robustos

O agregado host-only cobre as filas de entrada, teclado, mouse, USB HID,
VESA, VGA, backbuffer, AC97, speaker, Shell e lifecycle dos drivers. A matriz
QEMU reutiliza os casos marcados com `hw3`, sem criar perfis novos:

```text
make test-hw3-host
make catalog-test
make test-hw3-qemu HW3_QEMU_WORKERS=4 HW3_QEMU_SEED=2103
```

Os artefatos ficam em `build/test-results/hw3/`. O perfil `no-vesa` preserva
serial/QMP; `no-usb` e `no-audio` validam ausencia e fallback. A execucao
fisica de PS/2 permanece `PENDING` e nao e declarada como suporte.

## HW4 - storage e USB somente-leitura

O agregado host-only cobre ATA, UHCI, EHCI, transporte USB, USB Manager, MSC,
Block/Cache, FAT12/FAT32, FS, Storage, VFS e diagnósticos relacionados. A
matriz seleciona os casos com a tag `hw4`, incluindo o perfil interno
`usb-storage-ehci`, que exige uma imagem raw de armazenamento somente-leitura:

```text
make test-hw4-host
make catalog-test
make test-hw4-qemu HW4_QEMU_WORKERS=4 HW4_QEMU_SEED=2104
make test-hw4
```

Os artefatos ficam em `build/test-results/hw4/`. O perfil `usb-storage` cobre
UHCI; `usb-storage-ehci` cobre EHCI high-speed; `no-storage` e `no-usb`
confirmam a ausência segura do recurso. Escritas MSC são rejeitadas e a perda
do dispositivo deve remover provider, cache, handles e aliases sem resíduos.

## HW5 - rede e energia

O agregado host-only cobre E1000, RTL8139, ACPI, energia, RTC/clock, protocolos
de rede offline, sockets, Shell e lifecycle. A matriz usa somente redes privadas
ou restritas do QEMU; nenhum caso acessa a Internet ou depende de DHCP externo.
O perfil interno `network-dual` cria duas NICs E1000 com identidades estaveis:

```text
make test-hw5-host
make catalog-test
make test-hw5-qemu HW5_QEMU_WORKERS=4 HW5_QEMU_SEED=2105
make test-hw5
```

Os artefatos ficam em `build/test-results/hw5/`. Os perfis `baseline`,
`network`, `network-dual`, `no-nic` e `no-acpi` devem terminar em `PASS` ou
degradacao esperada. A matriz verifica `health`, `regcheck full`, `devices`,
`device-scan`, `net status` e `power status`, alem de limpeza de IRQ, DMA,
buffers, filas, sockets, leases e callbacks.

## HW6 - diagnostico e suporte

O agregado repete os diagnosticos sem reinicializar drivers e valida snapshots
por copia, estados `READY`, `DEGRADED`, `FAILED`, `ABSENT` e `UNSUPPORTED`,
retorno ao prompt e ausencia de residuos:

```text
make test-hw6-host
make catalog-test
make test-hw6-qemu HW6_QEMU_WORKERS=4 HW6_QEMU_SEED=2106
make test-hw6
```

Os artefatos ficam em `build/test-results/hw6/`. O caso
`qemu:hw6:diagnostics-repeat` usa o perfil `baseline`, snapshots
independentes e rede restrita quando a NIC estiver presente. Ele executa duas
vezes `health check`, `regcheck full`, `devices`, `devices -v`, `device-info`,
`device-scan`, `acpi tables`, `net status`, `usb status` e `power status`.
Hardware fisico continua `PENDING` e nao e declarado suportado sem evidencia.

## TST2 - protocolo e executor QEMU

Testes host-only:

```text
make test-tst2-host
```

Teste rapido do executor e smoke QEMU:

```text
make test-qemu-selftest
make test-qemu
```

Para usar o runner diretamente, informe uma imagem ja compilada, o catalogo e
as ferramentas configuradas:

```text
python tools/qemu_test_runner.py --self-test
python tools/qemu_test_runner.py stress --case qemu:tst2:boot-ready --iterations 1 --image build/zephyros.img --catalog tests/catalog.json
```

Os resultados do runner QEMU ficam em `build/test-results/<run-id>/`.

## QEMU paralelo e soak contínuo

O orquestrador geral executa casos QEMU independentes em processos paralelos.
O modo `parallel` exige uma seleção explícita; `--profile` e `--tag` podem ser
combinados. O modo `soak` usa por padrão as tags `stress`, `fault`, `apps` e
`storage`:

```text
make test-qemu-parallel QEMU_PARALLEL_WORKERS=4 QEMU_PARALLEL_ARGS="--profile smoke"
make test-qemu-parallel QEMU_PARALLEL_WORKERS=6 QEMU_PARALLEL_ARGS="--case qemu:tst5:apps --case qemu:tst5:processes --seed 12345"
make test-qemu-soak-parallel QEMU_PARALLEL_WORKERS=6 QEMU_PARALLEL_SOAK_ARGS="--seed 12345"
```

O intervalo operacional de workers é de 1 a 64, com padrão 4. Cada caso usa
snapshot, seed, portas, timeout, `run_id` e diretório próprio. Os resultados
agregados ficam em `build/test-results/parallel/<run-id>/`; falhas não
substituem artefatos anteriores.

## SEC6 — validação adversarial

Os alvos SEC6 executam a validação host, os diagnósticos QEMU adversariais e a
matriz TST6 reutilizada de falhas, recuperação e hardware. O perfil interno
`no-vesa` mantém serial/QMP ativos e desliga somente o framebuffer:

```text
make test-sec6-host
make test-sec6-qemu SEC6_QEMU_WORKERS=4 SEC6_QEMU_SEED=606
make test-sec6
```

Para o ciclo contínuo controlado, use o supervisor com limite finito; o
`--stop-file` pode ser criado para solicitar parada graciosa:

```text
python tools/tst7_continuous_runner.py start --mode soak-parallel --max-cycles 1 --interval 0 --workers 4 --seed 606 --stop-file build/sec6-stop
```

O fechamento SEC6 exige `PASS` agregado, nenhum processo QEMU residual e
artefatos preservados em `build/test-results/parallel/<run-id>/` e
`.tst7-results/continuous/<session-id>/`. `DT100-003` permanece visível como
dívida técnica aceita da SEC4.

## TST3 — logica host-only e limites

```text
make test-tst3-host
make test-tst3-sanitize
make package-test
make update-test
```

`test-tst3-host` executa strings, compressao, packager e updater. O alvo
sanitizado usa ASan/UBSan com Clang/LLVM e nao faz fallback silencioso.

## TST4 — autotestes internos do kernel

Cada alvo QEMU executa um caso independente por `RUN`, com uma iteracao e sem
retry automatico:

```text
make test-tst4-qemu
make test-tst4-qemu-paging-vma
make test-tst4-qemu-execution
make test-tst4-qemu-storage-vfs
make test-tst4-qemu-network
make test-tst4-qemu-platform
```

## TST5 — testes black-box no QEMU

Host-only:

```text
make test-tst5-host
```

Casos independentes:

```text
make test-tst5-qemu-shell
make test-tst5-qemu-input
make test-tst5-qemu-apps
make test-tst5-qemu-processes
make test-tst5-qemu-storage
make test-tst5-qemu-network
make test-tst5-qemu-update-recovery
make test-tst5-qemu-reboot
make test-tst5-qemu-poweroff
```

Os casos usam QMP, imagem isolada e timeout limitado. Reboot e poweroff
afetam somente a instancia QEMU.

## TST6 — matriz, estresse e falhas controladas

Host-only:

```text
make test-tst6-host
```

Matriz de perfis QEMU:

```text
make test-tst6-qemu-matrix-baseline
make test-tst6-qemu-matrix-minimal
make test-tst6-qemu-matrix-network
make test-tst6-qemu-matrix-usb-hid
make test-tst6-qemu-matrix-usb-storage
make test-tst6-qemu-matrix-audio
make test-tst6-qemu-matrix-display
make test-tst6-qemu-matrix-pci
```

Estresse e soak:

```text
make test-tst6-qemu-stress-kernel
make test-tst6-qemu-stress-storage
make test-tst6-qemu-stress-network
make test-tst6-qemu-stress-apps
```

Falhas controladas e recuperacao:

```text
make test-tst6-qemu-fault-memory
make test-tst6-qemu-fault-block
make test-tst6-qemu-fault-block-cache
make test-tst6-qemu-fault-package
make test-tst6-qemu-fault-update
make test-tst6-qemu-fault-network
make test-tst6-qemu-fault-process
make test-tst6-qemu-fault-recovery
```

Os casos TST6 sao independentes, usam snapshot, seed reproduzivel e limites
de iteracao/duracao. Hardware fisico nao e validado por esses comandos.

## TST7 — regressao continua

Teste unitario do comparador e do runner:

```text
make test-tst7-host
```

Suite rapida, sem a matriz QEMU completa:

```text
make test-tst7-quick
```

Suite completa, com `clean`, build, gates, catalogo e os 39 casos QEMU em
processos separados:

```text
make test-tst7-full
make test-tst7-continuous-host
make test-tst7-continuous-parallel QEMU_PARALLEL_WORKERS=6
```

O supervisor também aceita `--mode parallel` e `--mode soak-parallel`. O
primeiro executa um ciclo com seleção por `--case`, `--profile`, `--tag` ou
`--all`; o segundo repete o pool de tags com novas seeds por ciclo e respeita o
mesmo `--stop-file`.

A execucao completa nao aprova baseline automaticamente. Depois de revisar o
relatorio de um `full` sem `FAIL`, `BLOCKED` ou timeout, aprove explicitamente:

```text
python tools/tst7_regression_runner.py approve --run-id <id>
```

Os resultados ficam em `.tst7-results/<run-id>/` e incluem manifesto,
resultado, cobertura, resumo, logs e indice de artefatos. O baseline aprovado
fica em `tests/baselines/tst7-approved.json`.

## Testes de App Store e integracoes auxiliares

Estes alvos exercitam as ferramentas e fixtures de distribuicao sem fazer
parte do catalogo QEMU TST2–TST7:

```text
make store-test
make store-as2-test
make store-as4-test
make store-as5-test
```

Fixtures e matrizes de atualizacao do sistema:

```text
make system-fixtures
make run-system-fixture
make system-slots-fixtures
make run-system-slots-fixture
make system-slots-matrix
make run-system-slots-matrix
make run-system-update-matrix
make ep94b-fixtures
make ep94b-matrix
make run-ep94b-matrix
make ep94c-matrix
make run-ep94c-matrix
make run-recovery-menu-vga
```

Esses comandos podem exigir variaveis de imagem/fixture descritas em
`docs/qualidade/comandos-operacionais-agente.md` e nos roadmaps das
funcionalidades correspondentes.

## Interpretacao dos resultados

- `PASS`: o caso terminou conforme o contrato.
- `FAIL`: o sistema, guest, ferramenta ou contrato falhou.
- `BLOCKED`: falta ferramenta, imagem, fixture, hardware obrigatorio ou
  capacidade de infraestrutura.
- `TIMEOUT`: a execucao ultrapassou o limite; deve ser investigada como falha
  do caso quando nao for uma condicao esperada.

Todo teste QEMU deve terminar com sucesso, falha ou bloqueio identificavel e
preservar seus artefatos. Nenhum comando deve aguardar indefinidamente.

## SHELL2 - comandos basicos e diagnosticos

```text
make test-shell2-host
make test-shell2-qemu SHELL2_QEMU_WORKERS=4 SHELL2_QEMU_SEED=2202
make test-shell2
```

O agregado host combina os testes dos comandos core, diagnosticos, checks,
introspeccao, VFS, supervisor, prompt e black-box com
`test_shell2_matrix.py`. A matriz QEMU usa a tag `shell2`, snapshots
independentes e os casos de fallback sem hardware. O caso dedicado
`qemu:shell2:commands-diagnostics` cobre comandos validos e invalidos,
cancelamento, diagnosticos, `mount` separado e reentrada.
