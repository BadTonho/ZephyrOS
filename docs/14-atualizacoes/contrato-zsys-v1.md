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
imediatamente depois.

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
identidade divergente. Não existe staging, cache de imagem, slots A/B,
aplicação, rollback pós-reboot ou alteração de boot/stage2 nesta etapa.

## Ferramentas e comandos

No host:

    python tools/updater.py system-build ...
    python tools/updater.py system-verify --package <arquivo.ZSYS> --public <chave.json>
    python tools/updater.py release-v2-build ...
    python tools/updater.py release-v2-check --release <release.json> --public <chave.json>
    python tools/updater.py fixtures-system ...

No Shell:

    update system verify <arquivo.ZSYS>
    update system check --tag <tag>

As duas operações são somente leitura. A verificação local usa leitura em
streaming e não aloca a imagem inteira; a consulta por tag valida o descritor
v2, o asset system.zsys, a assinatura e os hashes durante o download.
