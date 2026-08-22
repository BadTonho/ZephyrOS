# Contrato ZUM2 e ZUPD v2 — EP6.3

## Resumo de Progresso

EP6.3 acrescenta um runtime de atualizacao separado do ZUPD v1. O v1, seus
enums, aliases `ZUR0/ZUR1` e sua matriz de regressao permanecem inalterados.
O v2 nao aceita boot, stage2, kernel, setores crus ou dados persistentes do
usuario; esses alvos pertencem a EP9.

O contrato usa um manifesto `ZUM2` assinado e um pacote completo `ZUPD v2`.
O manifesto e a autoridade para compatibilidade, operacoes e hashes; o
`release.json` e somente um inventario de transporte.

## Atalhos

Gerar uma Release local, sempre em um diretorio novo e com a chave privada
fora do repositorio:

```text
python tools/updater.py runtime-build --manifest <runtime.json> --private <chave> --public config/update-release-public.json --output-dir <diretorio-novo>
python tools/updater.py runtime-verify --manifest <diretorio-novo>/runtime.zum2 --package <diretorio-novo>/runtime.zephyrosupd --public config/update-release-public.json
```

Gerar e servir fixtures EP6.3:

```text
python tools/updater.py fixtures-runtime --private <chave> --public config/update-release-public.json --output-dir <diretorio-vazio>
python tools/updater.py serve-runtime --root <diretorio-das-fixtures> --tag ep63-runtime
python tools/updater.py serve-github-runtime --root <diretorio-das-fixtures> --cert <certificado> --key <chave-tls> --tag ep63-runtime
```

Os comandos do sistema são:

```text
update runtime status
update runtime check [--tag TAG]
update runtime fetch [--tag TAG] [--full] [--confirm]
update runtime verify [ARQUIVO|--cached]
update runtime apply --confirm
update runtime rollback --confirm
update runtime clear --confirm
```

`fetch` nunca instala. `apply` e `rollback` exigem confirmação e informam a
necessidade de reinicialização; nenhum caminho reinicia silenciosamente.

## Fases

### Manifesto ZUM2

O manifesto tem exatamente 4096 bytes, é little-endian, sem padding implícito,
e assina:

```text
"ZEPHYROS-RUNTIME-MANIFEST-V2\0" || bytes[0:4032]
```

O layout fixo é:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic `ZUM2` |
| 4 | 2 | formato `2` |
| 6 | 2 | tamanho `4096` |
| 8 | 2 | arquitetura i386 `1` |
| 10 | 2 | flags reservadas, zero |
| 12 | 4 | generation |
| 16 | 6 | versão alvo |
| 22 | 4 | epoch alvo |
| 26 | 2 | quantidade de entradas |
| 28 | 2 | tamanho de entrada `224` |
| 30 | 2 | quantidade de bases |
| 32 | 4 | tamanho do pacote completo |
| 36 | 32 | SHA-256 do pacote completo |
| 68 | 16 | `key_id` da raiz de confiança |
| 84 | 64 | tag da Release, NUL e zeros |
| 148 | 64 | ID da Release, NUL e zeros |
| 256 | 8 × 16 | versões-base e epochs declarados |
| 384 | 16 × 224 | catálogo de arquivos |
| 4032 | 64 | assinatura Ed25519 |

O catálogo compilado da primeira entrega contém exatamente:
`EXPLORER.BMP`, `SHELL.BMP` e `TASKMGR.BMP`. Cada entrada informa presença
alvo, operação permitida, tamanho/hash do alvo e o asset individual. Arquivo
presente usa `replace`, `create` ou a combinação `replace_or_create`; arquivo
ausente usa somente `delete`. Caminhos fora do catálogo, duplicidades, hashes
zerados, operações incompatíveis e campos reservados não são aceitos.

O alvo precisa ser superior a todas as versões-base declaradas, comparando
epoch antes da versão. A instalação atual pode ser qualquer uma dessas bases;
isso permite uma atualização direta sem cadeia de deltas.

### Pacote completo ZUPD v2

O pacote tem no máximo 128 KiB e assina:

```text
"ZEPHYROS-RUNTIME-PACKAGE-V2\0" || bytes[0:signature_offset]
```

Seu layout é:

```text
[header de 128 bytes]
[3 a 16 entradas de 128 bytes]
[payloads contíguos dos arquivos presentes]
[assinatura Ed25519 de 64 bytes]
```

Cada payload possui no máximo 64 KiB. Entradas ausentes são `delete` e não
possuem payload. O campo de base do header identifica um perfil de referência
de empacotamento; a aplicação de um pacote armazenado usa o `ZUM2` do mesmo
cache para autorizar qualquer base declarada. Assim, o pacote completo não é
uma cadeia de deltas e pode ser aplicado diretamente a cada base suportada.

### Cache e transação

O cache remoto v2 é independente do U5 v1:

```text
ZRV0.STA / ZRV1.STA   estado A/B do cache
ZRV0.MAN / ZRV1.MAN   manifesto ZUM2
ZRV0.PKG / ZRV1.PKG   pacote completo
ZRV0.00 ... ZRV1.15   assets individuais
```

Todos os aliases são hidden/system/archive. O estado possui sequência,
fase, slot ativo/pendente, modo seletivo/completo, hashes, tamanho e alvo,
com SHA-256 nos bytes `0..479`. O slot novo é preparado, validado e somente
depois publicado como ativo; uma interrupção no download ou na validação
descarta apenas o pendente.

O runtime local usa namespaces separados:

```text
ZTV0.STA / ZTV1.STA   estado instalado e rollback
ZTV0.JRN / ZTV1.JRN   journal da aplicação/rollback
ZTS0.00 ... ZTS1.15   staging
ZTB0.00 ... ZTB1.15   backups de rollback
```

Staging e backups são publicados com escrita FAT atômica. Falha, cancelamento
ou reinicialização durante qualquer fase conserva o slot ativo, a instalação
anterior e os arquivos do usuário. O rollback é local e não exige rede.

### Transporte HTTP U5 e GitHub HTTPS

O fluxo comum é:

1. resolver a Release/tag e conferir os metadados de transporte;
2. baixar o `ZUM2` antes de qualquer payload;
3. validar assinatura, confiança, catálogo, bases, versão e hashes;
4. comparar os hashes locais;
5. baixar apenas os assets necessários, ou o `runtime.zephyrosupd` com `--full`;
6. verificar tamanho, hash, assinatura e compatibilidade do pacote;
7. publicar somente o slot inativo.

O descritor HTTP `runtime-<tag>.json` usa `zephyros-runtime-release-v2` e
lista `runtime.zum2`, `runtime.zephyrosupd` e os assets individuais. Para
GitHub, a API de Release por tag exata fornece os mesmos assets, sem
credenciais no kernel. Em ambos os casos, `ZUM2` e `ZUPD v2` continuam sendo a
autoridade criptográfica.

### Shell e Updater Classic

O Shell oferece consulta, preflight, download seletivo/completo, verificação,
aplicação, rollback e limpeza. A aba `Runtime` do Updater Classic exibe estado
local, journal, cache A/B, progresso, assets reutilizados/faltantes e
cancelamento. O modo Simple permanece congelado e não recebe a aba nova.

### Registro de implementação

Implementação do contrato, runtime local, transporte, comandos, Classic e
ferramentas host concluída em: 2026-08-22 20:01 (America/Sao_Paulo).
O bridge append-only em `update.h` sincroniza o estado compartilhado U3 após
uma transação v2 sem reinterpretar o layout ou os caches do ZUPD v1.

## Limitações

- o catálogo inicial é fixo nos três BMPs; não há wildcard FAT;
- deltas não fazem parte da EP6.3;
- arquivos persistentes do usuário, boot, stage2, kernel e imagem completa estão fora do contrato;
- aplicação continua limitada ao FAT12 atual; FAT32 pode consultar/verificar;
- validação QEMU, incluindo HTTP U5, GitHub HTTPS e auditoria offline, depende da execução do usuário após os gates de build;
- nenhum build, teste executável ou QEMU é executado pelo agente nesta etapa.

## Referências

- [Contrato ZUPD v1](contrato-zupd-v1.md)
- [Distribuição remota](distribuicao-remota.md)
- [System Updater](system-updater.md)
- [Ferramenta host](ferramenta-zupd.md)
- [Contratos públicos](../qualidade/contratos-publicos.md)
- [Roadmap 08](../roadmaps/08-evolucao-da-plataforma.md)
