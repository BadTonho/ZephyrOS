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

## TST2 — protocolo e executor QEMU

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

Suite completa, com `clean`, build, gates, catalogo e os 36 casos QEMU em
processos separados:

```text
make test-tst7-full
```

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
