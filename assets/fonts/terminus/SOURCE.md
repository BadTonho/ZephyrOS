# Zephyr UI Bitmap

As faces bitmap deste diretorio foram importadas sem alteracao do pacote
Terminus Font 4.49.1 e servem como fonte reproduzivel para a familia derivada
`Zephyr UI Bitmap`.

- Upstream: <https://terminus-font.sourceforge.net/>
- Release: `terminus-font-4.49.1.tar.gz`
- SHA-256 do arquivo: `d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79`
- SHA-256 de `ter-u16n.bdf`: `5197662b22bf9f3e68d4af9f969a7fefa3edae40dd82ae969a147381130fb4ae`
- SHA-256 de `ter-u20n.bdf`: `b3868699145f51f1c059b955110c35db990d10a07536995cbcf7a4a901f82178`
- SHA-256 de `ter-u24n.bdf`: `ff640e9e097983355e8f70ed8fb645bd850184a0d590de17f6fc9ceec1bf8eaf`
- SHA-256 de `OFL.TXT`: `c14f8d795784a547ea35e69c51dee2957bb71a1cdb492ec5321e4b61d3d97630`
- Peso incorporado: normal (`ter-u16n`, `ter-u20n` e `ter-u24n`)
- Subconjunto gerado: U+0020 a U+007E
- Licenca: SIL Open Font License 1.1, preservada em `OFL.TXT`

O nome reservado upstream nao e usado para a familia derivada incorporada ao
kernel. `tools/vendor_terminus.py` valida os hashes dos BDFs e gera
deterministicamente `src/drivers/font_data.inc`.
