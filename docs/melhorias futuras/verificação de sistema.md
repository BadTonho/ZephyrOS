# Verificação e Auto-reparo de Arquivos do Sistema — ZephyrOS

## Resumo de Progresso

- [ ] Módulo de cálculo e validação de hash (CRC32/MD5) de arquivos críticos do sistema.
- [ ] Banco de dados de integridade e baseline (`/security/integrity.db`).
- [ ] Imagem limpa e repositório de restauração do sistema (`/system/recovery/` ou `/security/backups/`).
- [ ] Mecanismo de verificação de integridade sob demanda e no boot.
- [ ] Mecanismo de auto-reparo e substituição automática de arquivos corrompidos ou adulterados.
- [ ] Comandos do shell `sfc check` e `sfc repair`.

## Atalhos

| Comando | Ação |
|---------|------|
| `sfc check` | Verifica a integridade dos arquivos do sistema em relação à baseline |
| `sfc repair` | Verifica e repara automaticamente qualquer arquivo corrompido ou ausente |
| `sfc baseline` | Atualiza/cria o mapa de hashes confiáveis dos arquivos do sistema |

## Fases

### Fase 1 — Baseline e Verificação de Integridade ⬜

- Módulo `src/security/integrity.c` e `integrity.h`.
- Estrutura `file_integrity_t` com caminho, CRC32/MD5, tamanho e flag `is_critical`.
- Geração e salvamento do mapa de hashes originais em `/security/integrity.db`.
- Função `integrity_verify(path)` e `integrity_verify_all()`.

### Fase 2 — Repositório de Recuperação (Recovery Cache) ⬜

- Armazenamento de cópias limpas e assinadas dos binaries essenciais (`kernel.bin`, `boot.bin`, drivers e executáveis base) em `/system/recovery/`.
- Proteção do diretório de recuperação contra escrita/exclusão não autorizada.
- Função `recovery_get_backup(path)` para resgatar a versão original de um arquivo.

### Fase 3 — Auto-reparo (Self-healing & Repair) ⬜

- Função `integrity_repair(path)`:
  1. Identifica se o arquivo está `TAMPERED` ou `CORRUPTED`.
  2. Isola o arquivo afetado (movendo para quarentena `/security/quarantine/`).
  3. Copia a versão limpa original a partir do repositório `/system/recovery/`.
  4. Recalcula o checksum e valida se o reparo foi concluído com sucesso.
- Função `integrity_repair_all()` para escaneamento e restauração automática em lote.
- Integração com a inicialização: se um arquivo crítico do sistema for detectado como corrompido durante a validação de boot, o kernel aciona a rotina de auto-reparo antes de prosseguir.

### Fase 4 — Interface e Diagnósticos ⬜

- Comando shell `sfc` (System File Checker) com os subcomandos `check`, `repair` e `baseline`.
- Integração com o comando de diagnóstico `health` (`resiliencia do sistema.md`) e opção gráfica no aplicativo Antivírus / Painel de Configurações.

## Limitações

- Arquivos modificados intencionalmente por atualizações oficiais exigem regeneração da baseline (`sfc baseline`).
- Se a imagem limpa no repositório `/system/recovery/` for corrompida no disco, será necessária restauração via mídia externa de instalação ou atualização completa do sistema.

## Referências

- `src/security/integrity.c`
- `src/include/security/integrity.h`
- `docs/melhorias futuras/anti virus.md`
- `docs/melhorias futuras/resiliencia do sistema.md`