# Registro de validações

Este documento é o histórico cronológico de implementações, testes e
conclusões de fase. Cada entrada registra a evidência reproduzível e o horário
real. Os roadmaps mantêm apenas o estado e o link para a entrada correspondente.

Não registrar chaves privadas, senhas, tokens, caminhos pessoais ou outros
segredos.

## EP6.3 — GitHub runtime v2 via HTTPS

Concluída em: 2026-08-23 16:47:47 (America/Sao_Paulo)

Escopo: caminho real de Releases públicas GitHub. A Release
`ep63-runtime-b`, compatível com a base `0.1.0`, foi usada para validar
substituição, criação, remoção, aplicação e rollback do runtime v2.

No QEMU, a sequência executada foi:

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
update runtime rollback --confirm
reboot
update runtime status
ls
```

Resultado:

- `check` descobriu a Release GitHub por tag exata e autenticou o ZUM2;
- `fetch` publicou o cache; `verify --cached` retornou `assets faltantes=0`;
- após a aplicação: `0.1.2/e0`, `rollback=READY`, `EXPLORER.BMP` e
  `SHELL.BMP` presentes, `TASKMGR.BMP` ausente;
- após o rollback: `0.1.0/e0`, `rollback=DISABLED`, `EXPLORER.BMP` e
  `TASKMGR.BMP` presentes, `SHELL.BMP` ausente.

O aviso inicial de falha ao conectar no HTTP U5 local é o fallback anterior à
consulta GitHub e não invalida o cenário quando a descoberta e as operações
GitHub concluem com `NONE`.

Após encerrar o QEMU, a evidência persistida foi:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.0 --expect-rollback unavailable --expect-runtime-cache valid --expect-runtime-pending clean
```

Saída: `Audit image: OK`, `installed=0.1.0`, `rollback=DISABLED`,
`journal=CLEAN`, `runtime=READY`, `runtime_local=READY` e nenhuma operação
pendente.
