# Third-Party Notices

## Zephyr UI Bitmap / Terminus Font 4.49.1

ZephyrOS inclui em `src/drivers/font_data.inc` um subconjunto ASCII derivado
das faces normais 8x16, 10x20 e 12x24 da Terminus Font 4.49.1:

- projeto: <https://terminus-font.sourceforge.net/>;
- release: `terminus-font-4.49.1.tar.gz`;
- SHA-256 do arquivo:
  `d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79`;
- caracteres incorporados: U+0020 a U+007E;
- nome da derivacao no ZephyrOS: `Zephyr UI Bitmap`;
- gerador reproduzivel: `tools/vendor_terminus.py`;
- licenca upstream: `SIL Open Font License 1.1`.

Os BDFs originais, seus hashes individuais, a proveniencia e o texto integral
da licenca estao em `assets/fonts/terminus`. O nome reservado upstream nao e
usado para a familia derivada.

Copyright (C) 2020 Dimitar Toshkov Zhekov.

## Monocypher 4.0.3

ZephyrOS inclui em `src/core/crypto_ed25519.c` um subconjunto verify-only de
Monocypher 4.0.3:

- projeto: <https://monocypher.org/>;
- release: <https://github.com/LoupVaillant/Monocypher/releases/tag/4.0.3>;
- componentes adaptados: aritmetica Ed25519, reducao escalar, verificacao da
  equacao, SHA-512 e utilitarios estritamente necessarios;
- gerador reproduzivel: `tools/vendor_monocypher.py`;
- licenca upstream: `BSD-2-Clause OR CC0-1.0`.

O gerador exige SHA-256
`57eb914fc88136119bd41655cccb8c250048bf54d470540625186f8ab16f64be`
para `monocypher.c` e
`60fce3578fb00b00da96490653d993c4cb427b1e1be38183285c66e04d22cc18`
para `optional/monocypher-ed25519.c`.

O arquivo adaptado preserva o cabecalho de copyright e o texto de licenca
upstream. Nenhuma rotina de geracao de chave, assinatura, troca de chaves ou
criptografia simetrica foi incorporada ao kernel.

Copyright (c) 2017-2020, Loup Vaillant. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
