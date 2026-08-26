# System Updater

## Escopo

O System Updater e o aplicativo nativo das U4/U5 para operar o servico ZUPD
local e seu transporte remoto opcional.
Ele abre pelo comando `updater` ou pelo item `Atualizacoes` do menu Iniciar.
O modulo fica em `src/updater/updater.c` e seu contrato publico autocontido em
`src/include/ui/updater.h`.

O aplicativo nao implementa criptografia, parser ZUPD, escrita FAT, HTTP ou
recuperacao. Verificacao, preflight, aplicacao e rollback usam
`src/include/core/update.h`; consulta, download e cache usam
`src/include/core/update_remote.h`. Nenhum download aplica um pacote.

## Modos de interface

`updater_open()` escolhe o modo global atual:

- Simple abre uma TUI em tela cheia e restaura o Desktop ao fechar;
- Classic registra uma janela singleton hospedada pelo Window Manager;
- se a hospedagem classic estiver indisponivel, o aplicativo usa Simple
  automaticamente.

Para a U5, Shell e Classic formam a matriz obrigatoria de aceitacao. O Simple
continua disponivel como fallback funcional, mas sua regressao e complementar
e nao bloqueia a conclusao da distribuicao remota.

O modo Simple mantém as quatro abas legadas. O modo Classic oferece essas
quatro abas e uma seção adicional `Runtime`:

- `Pacotes`: aliases `.ZUP`, verificacao, preflight e aplicacao;
- `Estado`: integridade separada dos controles e arquivos atuais, versoes,
  capacidades e rollback;
- `Historico`: ate oito eventos persistidos, mais recentes primeiro.
- `Remoto`: opt-in da sessao, canal, rede, candidato, progresso, retry,
  motivo e pacote armazenado.
- `Runtime` (somente Classic): estado ZTV, cache A/B ZRV, assets reutilizados,
  progresso seletivo/completo, aplicação, rollback e limpeza.

O modulo enumera no maximo 16 arquivos da raiz. Diretorios, arquivos hidden ou
system e extensoes diferentes de `.ZUP` sao ignorados. A lista e ordenada por
alias FAT, nao usa alocacao dinamica e informa quando existem itens adicionais.
O slot remoto ativo e acrescentado explicitamente apesar dos atributos
hidden/system, para poder ser verificado e aplicado como qualquer pacote.

## Fluxos

Verificar apenas chama `update_verify_file()` e nao grava. Aplicar primeiro
chama `update_apply_file()` com `dry_run=1`; somente depois de uma confirmacao
explicita repete integralmente a operacao com `dry_run=0`. Rollback segue a
mesma separacao usando `update_rollback()`.

Trocar selecao, aba ou atualizar a lista cancela qualquer confirmacao pendente.
Durante uma mutacao, o callback cooperativo considera somente Esc e F12. O
resultado mostra motivo de acao/verificacao, progresso, recuperacao pendente e
necessidade de reboot. Os BMPs nao sao recarregados na sessao atual.

Na aba Remoto, habilitar apenas libera o opt-in em RAM. Consultar autentica o
manifesto e nao grava. Baixar apresenta uma confirmacao, repete a consulta e
so publica o slot depois de SHA-256 e verificacao ZUPD completa. Limpar cache
tambem exige confirmacao. Trocar de aba cancela qualquer confirmacao remota.

Consulta e download iniciados pela interface sao enfileirados em um processo
cooperativo dedicado. O callback de teclado ou mouse retorna imediatamente,
deixando o processo de sistema continuar o polling da rede e a composicao do
Window Manager. Enquanto o worker aguarda HTTP, a janela continua mostrando
estado e progresso; `Esc` ou `F12` solicita cancelamento cooperativo.

Na seção Runtime, `Consultar` e `Seletivo` executam preflight ou download
somente após confirmação; `Completo` baixa o `runtime.zephyrosupd` como
fallback independente de cadeia. `Aplicar` usa staging/journal e exige
confirmação, enquanto `Rollback` restaura o backup local sem rede. `Limpar`
remove apenas o cache runtime v2. O resultado informa cancelamento, slot
preservado, recuperação pendente e a necessidade de reinicialização; o
Updater nunca reinicia silenciosamente.

## Teclado e mouse

| Entrada | Acao |
|---|---|
| `Tab` | alterna as quatro abas legadas; no Classic inclui Runtime |
| Setas | mudam aba ou pacote selecionado |
| `F5` | atualiza lista, status e historico |
| `V` | verifica o pacote selecionado |
| `A` | executa o preflight de aplicacao |
| `B` | executa o preflight de rollback |
| `H` | habilita ou desabilita remoto na aba Remoto |
| `C` | consulta o manifesto na aba Remoto |
| `D` | prepara o download na aba Remoto |
| `X` | prepara a limpeza do cache na aba Remoto |
| `C` (Runtime) | consulta o manifesto ZUM2 |
| `D` (Runtime) | prepara download seletivo |
| `V` (Runtime) | prepara download completo ou verifica o pacote selecionado |
| `A` (Runtime) | executa o preflight de aplicação |
| `B` (Runtime) | executa o preflight de rollback |
| `X` (Runtime) | prepara a limpeza do cache v2 |
| `Enter` | confirma a acao pendente |
| `Esc` | cancela confirmacao ou mutacao; fecha somente o Simple ocioso |
| `F12` | cancela cooperativamente uma mutacao |
| `Alt+F4` ou botao `X` | fecha a janela Classic |

No modo Classic, abas, lista e botoes oferecem por mouse as mesmas operacoes.
Controles da moldura, foco, arraste, resize, minimizar e fechar continuam
pertencendo ao Window Manager. Um `Esc` ocioso nao fecha a janela hospedada.

## Recovery e diagnostico

`updater_init()` registra o aplicativo e publica
`RECOVERY_COMPONENT_SYSTEM_UPDATER`:

- `READY`: interface, worker cooperativo e servico Update local disponiveis;
- `DEGRADED`: interface utilizavel com filesystem, estado ou historico parcial;
- `DISABLED`: servico Update nao inicializado ou worker indisponivel.

O componente e diferente de `RECOVERY_COMPONENT_UPDATE`: o primeiro descreve a
interface U4, enquanto o segundo descreve o verificador e a transacao U2/U3.
`update status`, `update history`, `health` e `health summary` permitem
inspecionar ambos sem gravar. Falha remota nao degrada o componente System
Updater nem o servico Update local; somente a capacidade `remoto` muda.

O worker usa o ciclo de vida de jobs da Fase 5: verify, fetch, apply e
rollback exibem geracao, progresso e deadline; cancelamento espera o ponto
seguro da etapa atomica e drena journal/recovery antes de publicar o estado
final. Os wrappers publicos de `update.h` permanecem compatíveis.

## Limitacoes

- apenas arquivos `.ZUP` presentes na raiz podem ser selecionados;
- FAT32 permite verificacao, mas nao aplicacao nem rollback;
- FAT32 permite consultar o manifesto, mas nao armazenar o pacote remoto;
- somente a ultima geracao de rollback pode ser restaurada;
- remoto e desabilitado a cada boot e nunca configura rede automaticamente;
- nao existe consulta, download ou instalacao silenciosa;
- Runtime v2 usa somente o catalogo fixo dos tres BMPs e continua limitado ao
  FAT12 para cache/aplicacao; o Simple nao recebe a aba Runtime;

- o aplicativo nao altera boot, stage2, kernel em setores crus ou Desktop;
- o modo Simple permanece congelado e nao recebe a aba Sistema.

A EP9.3 acrescentou inicialmente o fluxo ZSYS aos comandos Shell. A EP9.4C
adiciona a sexta aba `Sistema` exclusivamente ao Classic. Ela mostra cache,
tag, versao, progresso, sequencia e o estado A/B; permite consultar, baixar,
verificar, aplicar e cancelar usando o mesmo worker cooperativo e os mesmos
servicos autenticados do Shell.

A tag aceita somente letras ASCII, numeros, ponto, sublinhado e hifen, limitada
por `UPDATE_REMOTE_TAG_SIZE`. Clique ou `T` inicia a edicao, Backspace remove,
Enter aceita e Esc restaura o valor anterior. Download, aplicacao e
cancelamento fazem preflight antes da confirmacao e repetem a validacao no
worker confirmado. Esc ou F12 solicita cancelamento do job sem bloquear a
janela.

Depois da publicacao do pendente, a interface oferece `Reiniciar agora` e
`Reiniciar depois`. A segunda opcao fecha apenas a oferta; um banner e o botao
de reinicio permanecem em novas aberturas enquanto o estado possuir pendente.
Reiniciar agora abre uma confirmacao final, verifica se a sequencia apresentada
nao mudou e chama o preflight redundante dos slots imediatamente antes de
`power_reboot()`. Nenhum retorno cancela o pendente ou promove o candidato.

## Referencias

- [Contrato ZUPD v1](contrato-zupd-v1.md)
- [Ferramenta host](ferramenta-zupd.md)
- [Distribuicao remota](distribuicao-remota.md)
- [Contrato ZUM2/ZUPD v2](contrato-zupd-v2.md)
- [Comandos do Shell](../09-shell/comandos.md)
- [Desktop e Window Manager](../12-desktop/desktop.md)
