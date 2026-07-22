# API de Aplicativos e Syscalls

## Resumo de Progresso

Status: Fases 1, 2 e 3 validadas; Fase 4 implementada e aguardando validacao no QEMU; carregador planejado para a Fase 5.

Esta etapa preparara o ZephyrOS para executar aplicativos independentes do
kernel. O objetivo nao e apenas criar mais comandos, mas definir uma fronteira
segura e estavel entre aplicativos, sistema de arquivos, memoria, processos e
interfaces graficas.

A implementacao sera incremental. Shell, Explorer, Settings, Task Manager e
as interfaces classica e moderna continuarao funcionando durante a transicao.

## Objetivo

Hoje os aplicativos nativos chamam diretamente funcoes internas do kernel e
dos drivers. Esse modelo e adequado para a fase inicial, mas dificulta a
criacao de aplicativos grandes, aumenta o acoplamento e permite que uma falha
de um modulo alcance outras partes do sistema.

O modelo futuro sera:

```text
Aplicativo -> syscall -> validacao do kernel -> servico -> resultado
```

Os aplicativos nao deverao acessar diretamente:

- tabelas de paging;
- memoria de outros processos;
- portas ATA ou PCI;
- framebuffer e controladores de video;
- estruturas internas do FAT;
- filas privadas de outros processos.

## ABI inicial de syscalls

As syscalls usam a convencao de registradores abaixo:

| Registrador | Funcao |
|---|---|
| `EAX` | Numero da syscall na entrada e codigo de retorno na saida |
| `EBX` | Primeiro argumento |
| `ECX` | Segundo argumento |
| `EDX` | Terceiro argumento |
| `ESI`, `EDI`, `EBP` | Argumentos reservados |

Os numeros atuais sao:

| Numero | Nome | Argumentos |
|---|---|---|
| `0` | `process_exit` | `EBX`: codigo de saida |
| `1` | `console_write` | `EBX`: texto; `ECX`: tamanho |
| `2` | `uptime` | `EBX`: `app_uptime_info_t*` |
| `3` | `memory_info` | `EBX`: `app_memory_info_t*` |
| `4` | `file_open` | `EBX`: caminho; `ECX`: modo; `EDX`: handle de saida |
| `5` | `file_read` | `EBX`: handle; `ECX`: buffer; `EDX`: tamanho; `ESI`: bytes |
| `6` | `file_write` | `EBX`: handle; `ECX`: buffer; `EDX`: tamanho; `ESI`: bytes |
| `7` | `file_close` | `EBX`: handle |
| `8` | `message_send` | `EBX`: PID; `ECX`: `app_message_t*` |
| `9` | `message_receive` | `EBX`: `app_message_t*` |

O vetor `int 0x80` inicia com gate `0x8E` (DPL 0) e passa para `0xEE` (DPL 3)
depois que paging, TSS, Idle e os processos essenciais estao prontos. A ponte
`syscall_invoke_kernel()` continua sendo usada pelo `appcheck` para exercitar
o mesmo dispatcher sem depender de ring 3.

O primeiro processo de teste usa a faixa de codigo `0x00800000`, dados em
`0x00801000` e stack em `0x00C00000`. Mapeamentos do kernel permanecem
supervisor e nao podem ser acessados pelo processo ring 3.

Quando um processo ring 3 termina por `process_exit` ou por uma excecao
isolada, o handler apenas o marca como `ZOMBIE`. O retorno da interrupcao e
redirecionado para uma trampoline de kernel, que troca de contexto fora da
pilha do ISR. Isso impede que um frame de usuario encerrado seja reutilizado
e preserva a execucao do Shell.

## Contrato inicial da API

Os nomes abaixo formam o contrato atual. A fachada interna `app_api_*` e o
dispatcher ja validam as operacoes de sistema, enquanto os aplicativos ainda
executam em ring 0.

| Grupo | Operacao inicial | Finalidade |
|---|---|---|
| Console | `console_write` | Escrever texto validado |
| Sistema | `uptime` | Consultar ticks e tempo ligado |
| Memoria | `memory_info` | Consultar total, usada e livre |
| Processo | `process_exit` | Encerrar o aplicativo atual |
| Arquivo | `file_open` | Abrir um arquivo por handle |
| Arquivo | `file_read` | Ler dados para buffer validado |
| Arquivo | `file_write` | Salvar dados com permissao valida |
| IPC | `message_send` | Enviar mensagem por PID ou handle |
| Entrada | `input_read` | Receber eventos do aplicativo |
| GUI | `window_create` | Solicitar uma janela ao sistema |

As funcoes deverao retornar `OK` ou um codigo de `errors.h`. Ponteiros de
aplicativos nunca serao usados diretamente sem validacao de endereco, tamanho
e permissao.

### Fachada disponivel na Fase 1

O contrato inicial esta disponivel para os modulos nativos do kernel por meio
de `src/include/core/app_api.h` e `src/core/app_api.c`:

- `app_api_get_version()` retorna a versao `0.1`;
- `app_api_console_write()` aceita texto ASCII validado de ate 1024 bytes;
- `app_api_get_uptime()` retorna ticks e segundos desde o boot;
- `app_api_get_memory_info()` retorna memoria e paginas disponiveis;
- `app_handle_t` e um handle opaco de 32 bits, com zero reservado como invalido.

### Servicos implementados na Fase 3

- handles de arquivo em tabela fixa, com geracao e ownership por PID;
- leitura sequencial por offset, com limite de 4096 bytes por chamada;
- escrita integral por caminho, sem append ou escrita parcial nesta fase;
- mensagens IPC por PID usando os tipos internos de teclado e solicitacao;
- `message_receive` nao bloqueante, retornando `ERR_NOT_FOUND` sem mensagens;
- adaptacao unificada para FAT12 e FAT32, sem expor estruturas FAT.

Os aplicativos nativos ainda rodam dentro do kernel. O dispatcher ring 3,
entretanto, copia argumentos para buffers do kernel e valida cada faixa de
memoria antes de chamar os servicos internos.

## Regras de seguranca

- Handles serao usados para arquivos, janelas e recursos do sistema;
- enderecos crus de kernel nao serao expostos aos aplicativos;
- buffers terao tamanho maximo validado;
- PID, estado e permissao serao conferidos antes de cada operacao;
- falhas recuperaveis retornarao erro e registrarao diagnostico;
- excecoes causadas por corrupcao estrutural continuarao encaminhadas para
  `panic`;
- um aplicativo nao podera finalizar o processo Idle nem o processo atual de
  outro aplicativo;
- a API tera uma versao para permitir evolucao sem quebrar aplicativos.

## Atalhos e comandos de teste

O Shell recebeu um comando de diagnostico para testar a camada sem depender
de um aplicativo completo:

```text
appcheck
```

O comando testa o dispatcher para `console_write`, `uptime` e `memory_info`,
alem de numero desconhecido, argumentos nulos, texto vazio, texto acima do
limite e `process_exit`. Cada chamada exibe seu codigo de retorno e uma falha
de validacao nao interrompe o Shell.

## Fases

### Fase 1 - Contrato e tipos comuns - concluida

- [x] definir tipos de retorno, handles e versao da API;
- [x] documentar limites de buffers e regras de ownership;
- [x] separar interfaces publicas de estruturas internas;
- [x] manter wrappers compativeis para os aplicativos nativos atuais;
- [x] adicionar `appcheck` e o estado da API ao `health`.

### Fase 2 - Dispatcher de syscalls - concluida

- [x] criar a entrada controlada de syscall;
- [x] validar numero, argumentos e processo chamador;
- [x] registrar chamadas invalidas sem travar o kernel;
- [x] implementar `console_write`, `uptime`, `memory_info` e a reserva segura
  de `process_exit` ate o modo usuario.

### Fase 3 - Servicos de arquivo e IPC

- [x] criar handles de arquivo com geracao e ownership por PID;
- [x] expor leitura sequencial e escrita integral atraves do FS unificado;
- [x] impedir acesso direto as estruturas FAT;
- [x] reutilizar as validacoes existentes de IPC e recovery;
- [x] testar as chamadas pelo dispatcher usando `appcheck`;

### Fase 4 - Processo em modo usuario - implementada, validacao pendente

- [x] adicionar segmentos de codigo e dados DPL 3 ao GDT;
- [x] criar diretorios de pagina com mapeamentos de kernel supervisor;
- [x] validar e copiar ponteiros entre ring 3 e kernel;
- [x] trocar `CR3` e preservar contexto durante preempcao;
- [x] habilitar `int 0x80` com gate DPL 3;
- [x] implementar `usertest` e `usertest fault`;
- [x] encerrar somente o processo de usuario em `process_exit` ou excecao;
- [ ] validar `usertest`, `usertest fault` e os fluxos nativos no QEMU.

### Fase 5 - Carregador de aplicativos

- definir um formato inicial de programa;
- validar cabecalho, tamanho e pontos de entrada;
- carregar o programa em memoria isolada;
- iniciar, acompanhar e finalizar o processo;
- registrar falhas de carregamento no recovery.

Depois da validacao da Fase 4, esta sera a proxima etapa. Ela definira um formato de imagem, validara o ponto
de entrada e carregara programas sem alterar ainda os aplicativos nativos.

### Fase 6 - Migracao gradual

- criar um aplicativo de teste minimo;
- migrar primeiro o Editor ou uma ferramenta simples;
- migrar o Explorer para usar handles e syscalls de arquivo;
- migrar aplicativos graficos depois que a API de janelas estiver estavel;
- preservar os modulos nativos como fallback durante toda a transicao.

### Fase 7 - Pacotes e ecossistema

- definir o formato `.zephyrosapp`;
- incluir manifesto, versao, permissoes e ponto de entrada;
- criar instalacao, remocao e validacao de pacotes;
- preparar bibliotecas de usuario para aplicativos produtivos e jogos.

## Criterios de aceitacao

- um aplicativo de teste consegue usar as quatro syscalls iniciais;
- argumentos nulos, tamanhos invalidos e handles inexistentes retornam erro;
- uma falha no processo `usertest` nao altera o estado do kernel;
- o Shell continua funcionando depois de encerrar um aplicativo;
- Explorer, Task Manager, Settings e Desktop continuam disponiveis;
- os modos classic e modern permanecem compativeis;
- o comando `health` mostra falhas da camada de aplicativos;
- `usertest fault` encerra somente o processo ring 3;
- o fallback nativo continua disponivel durante o carregador;
- o build e a validacao no QEMU passam antes de cada nova fase.

## Limites

- o carregador e o formato de aplicativos ainda nao existem;
- os aplicativos nativos continuam em ring 0;
- o isolamento de memoria cobre o processo de teste, nao uma politica de
  permissoes completa para pacotes;
- nao sera escolhido um formato ELF ou `.zephyrosapp` antes da API basica;
- a etapa nao altera `src/boot/boot.asm` inicialmente;
- nao sera criado um novo Window Manager nesta etapa;
- compatibilidade com aplicativos de outros sistemas depende de bibliotecas,
  drivers e APIs adicionais no futuro.

## Referencias

- `docs/melhorias futuras/fundacao do kernel.md`
- `docs/melhorias futuras/atualizacao do kernel.md`
- `docs/04-kernel/kernel.md`
- `docs/07-processos/processos.md`
- `docs/08-sistema-arquivos/sistema-arquivos.md`
- `src/process/`
- `src/memory/`
- `src/fs/`
