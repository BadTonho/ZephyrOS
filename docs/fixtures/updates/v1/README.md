# Vetores publicos ZUPD v1

Este diretorio contem fixtures imutaveis do contrato ZUPD v1. Os arquivos
foram assinados com chaves TEST ONLY publicadas nos testes do RFC 8032. Eles
nao autorizam atualizacoes de producao.

| Arquivo | Resultado esperado | Caso |
|---|---|---|
| `valid.zephyrosupd` | `ZUPD_REASON_NONE` | `0.1.0` para `0.1.1`, com `SHELL.BMP` |
| `corrupted.zephyrosupd` | `ZUPD_REASON_HASH` | byte de payload alterado apos assinatura |
| `unknown-key.zephyrosupd` | `ZUPD_REASON_UNKNOWN_KEY` | assinatura valida de outra chave de teste |
| `incompatible-version.zephyrosupd` | `ZUPD_REASON_BASE_VERSION` | assinatura confiavel, base `9.0.0` |

Os hashes exatos, IDs e chaves publicas ficam em `fixtures.json`. Nenhuma seed
ou chave privada e armazenada neste diretorio.
