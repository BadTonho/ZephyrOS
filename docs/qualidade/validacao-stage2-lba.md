# Validação do stage2 LBA

Este procedimento valida o carregamento do kernel por EDD/LBA e o fallback
CHS. `src/boot/boot.asm` deve permanecer sem alterações.

## 1. Gate e build

Executar na raiz do repositório:

```text
make q3check
make clean && make
```

Conferir os artefatos gerados:

```powershell
Get-Item build\boot.bin, build\stage2.bin | Select-Object Name, Length
git diff -- src/boot/boot.asm
```

Aceite no host:

- `boot.bin` possui exatamente 512 bytes;
- `stage2.bin` é múltiplo de 512 e não ultrapassa 45056 bytes;
- `git diff -- src/boot/boot.asm` não produz saída.

## 2. Geometria IDE explícita

```text
make run
```

No Shell:

```text
health check
memcheck
regcheck full
```

Fechar o QEMU antes do próximo cenário.

## 3. EDD/LBA sem geometria CHS fixa

```text
make run-stage2-lba
```

No Shell:

```text
health check
memcheck
regcheck full
```

O sistema deve alcançar o Shell e as três validações devem permanecer
operacionais. Fechar o QEMU antes do próximo cenário.

## 4. Fallback CHS

```text
make run-stage2-chs
```

No Shell:

```text
health check
memcheck
regcheck full
```

Esse alvo inicia a imagem como floppy, caminho no qual EDD não é anunciado.
O Shell deve abrir pelo fallback CHS. Uma cópia da imagem fornece o boot por
floppy, enquanto a imagem original permanece anexada como disco ATA para que
as regressões do kernel executem no mesmo ambiente dos outros cenários.

## 5. Aceite final

- os três cenários chegam ao Shell;
- o caminho IDE sem geometria fixa inicia sem depender de CHS no stage2;
- o cenário floppy inicia pelo fallback CHS;
- `memcheck` e `regcheck full` terminam em `OK`;
- nenhum erro `Kernel LBA read error!` ou `Kernel CHS read error!` aparece;
- horários e resultados reais são registrados em `registro-validacoes.md`.
