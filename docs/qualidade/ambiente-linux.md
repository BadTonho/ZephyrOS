# Ambiente Linux do ZephyrOS

Este arquivo registra o procedimento para compilar, verificar e executar o
ZephyrOS em um PC Linux. O `GNUmakefile` tem prioridade automática sobre
`makefile` e `Makefile`; `Makefile.linux` só é usado quando informado com
`-f`.

## Pré-requisitos

As ferramentas precisam estar disponíveis no `PATH` ou configuradas no
`GNUmakefile`:

```text
python3
nasm
qemu-system-i386
i686-elf-gcc
i686-elf-ld
i686-elf-nm
cc
clang
make
```

O compilador do kernel deve ser o cross-compiler `i686-elf-*`. Um GCC nativo
do Linux não substitui esse compilador no build freestanding.

Confira as ferramentas:

```bash
command -v make
command -v python3
command -v nasm
command -v qemu-system-i386
command -v i686-elf-gcc
command -v i686-elf-ld
```

## Qual makefile está sendo usado

Se existir `GNUmakefile`, o comando simples `make` usará esse arquivo. Para
forçar um arquivo específico:

```bash
make -f GNUmakefile q3check
make -f Makefile.linux q3check
```

O repositório ignora `GNUmakefile` e `Makefile.linux` porque eles podem conter
caminhos locais. Eles precisam ser copiados manualmente para o outro PC, ou
ser recriados a partir deste procedimento.

Confirme que o runner está com o nome correto:

```bash
test -f tools/tst7_continuous_runner.py
rg -n "tst7_continuos|tst7_continuous" GNUmakefile Makefile.linux
```

O nome correto é `tools/tst7_continuous_runner.py`. A forma
`tst7_continuos_runner.py` está errada.

## Build e execução

Execute a partir da raiz do repositório:

```bash
make q3check
make clean
make
make run
```

Antes de executar o QEMU, confira o comando que será usado sem iniciar nada:

```bash
make -n run
```

Ele deve apontar para `build/zephyros.img` e anexá-la como disco raw ao
`qemu-system-i386`.

## Verificação da imagem híbrida

O build válido deve criar uma imagem de 256 MiB com FAT32 iniciando no LBA
8192. Depois de `make`, execute:

```bash
python3 - <<'PY'
from pathlib import Path
import struct

image = Path("build/zephyros.img").read_bytes()
fat32 = 8192 * 512

print("bytes:", len(image))
print("mbr:", image[510:512].hex())
print("partition_type:", hex(image[450]))
print("partition_lba:", struct.unpack_from("<I", image, 454)[0])
print("fat32_signature:", image[fat32 + 510:fat32 + 512].hex())
print("label:", image[fat32 + 71:fat32 + 82].decode("ascii", "replace"))
print("filesystem:", image[fat32 + 82:fat32 + 90].decode("ascii", "replace"))
print("volume_lba:", struct.unpack_from("<I", image, fat32 + 28)[0])
PY
```

Valores esperados:

```text
bytes: 268435456
mbr: 55aa
partition_type: 0xc
partition_lba: 8192
fat32_signature: 55aa
label: ZEPHYROS
filesystem: FAT32
volume_lba: 8192
```

Se esses valores não aparecerem, o `make run` está abrindo uma imagem antiga,
incompleta ou gerada sem `prepare-hybrid-image`. Nesse caso, corrija o
`GNUmakefile` e repita `make clean` seguido de `make`.

## Recovery

`FAT32 INDISPONIVEL` significa que o loader não conseguiu validar a partição
FAT32 bruta. Nesse estado, `INVALID` e `NONE` para os slots são consequências
esperadas: os arquivos `ZSI0.STA`, `ZSI1.STA` e os candidatos não puderam ser
lidos. F8 apenas abre o menu de recuperação; não corrige a imagem.

## Testador paralelo

Depois que o build e a imagem forem confirmados:

```bash
make test-qemu-parallel QEMU_PARALLEL_WORKERS=6
make test-tst7-continuous-parallel QEMU_PARALLEL_WORKERS=6
```

O segundo comando é contínuo e permanece executando até `Ctrl+C` ou até o
`--stop-file` configurado pelo supervisor.

## Diagnóstico mínimo para enviar

Se o Recovery ainda aparecer, envie a saída destes comandos:

```bash
git rev-parse --short HEAD
make -n run
ls -lh build/zephyros.img
test -f tools/tst7_continuous_runner.py && echo runner-ok
```

Não copie chaves privadas, tokens ou arquivos de configuração que contenham
segredos.
