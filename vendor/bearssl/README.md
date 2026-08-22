# BearSSL 0.6 vendorizado

Este diretório contém a fonte oficial BearSSL 0.6, distribuída sob a licença
MIT. O arquivo de origem usado pelo EP6.2 é `bearssl-0.6.tar.gz` com SHA-256:

`6705BBA1714961B41A728DFC5DEBBE348D2966C117649392F8C8139EFC83FF14`

O perfil do ZephyrOS desativa alocação dinâmica, relógio e RNG de sistema do
BearSSL. A entropia é injetada pelo adaptador `src/drivers/rng.c`, e o horário
de validação X.509 vem de `src/core/clock.c`.

O perfil de build também exclui os perfis TLS full e todos os módulos DES/3DES
do BearSSL. O cliente do sistema usa somente TLS 1.2 com suites ECDHE e
AES-GCM; algoritmos legados não fazem parte do artefato final.
