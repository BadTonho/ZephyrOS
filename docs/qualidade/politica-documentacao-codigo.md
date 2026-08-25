# Política de documentação do código

## Objetivo

O código-fonte do ZephyrOS deve permanecer direto e legível por meio de nomes,
tipos, constantes e funções pequenas. Explicações técnicas continuam
obrigatórias, mas ficam fora dos arquivos `.c`, `.h`, `.asm`, `.ld` e scripts.

## O que documentar fora do código

- decisões de arquitetura e alternativas recusadas;
- invariantes, endereços físicos e limites de memória;
- ABI e handoffs entre boot, loader e kernel;
- layouts de disco, formatos persistentes e regras transacionais;
- motivos de workarounds, compatibilidade e restrições de hardware;
- pré-condições, efeitos de falha e estratégia de recuperação.

## Destino das informações

Usar primeiro o documento canônico do módulo listado em
`docs/qualidade/contratos-publicos.md`. Para boot e recovery loader, usar
`docs/03-bootloader/bootloader.md` para fluxo e memória e
`docs/14-atualizacoes/contrato-zsys-v1.md` para confiança, slots e handoff.
Relatos cronológicos pertencem somente a
`docs/qualidade/registro-validacoes.md`.

Quando não existir documento apropriado, criar um arquivo no diretório do
módulo em `docs/` e adicioná-lo a `docs/indice.md`. Não criar um arquivo de
notas por função ou repetir a mesma justificativa em vários documentos.

## Regra para alterações

Uma alteração nova não deve introduzir comentário explicativo no código. Ao
tocar num trecho que já possui comentários, migrar apenas a informação ainda
válida e relacionada ao escopo, removendo os comentários daquele trecho. Não
é necessário limpar arquivos inteiros nem comentários não relacionados.

Avisos legais e marcações exigidas por compilador, assembler, gerador ou
formato externo são exceções, pois não substituem documentação técnica.
