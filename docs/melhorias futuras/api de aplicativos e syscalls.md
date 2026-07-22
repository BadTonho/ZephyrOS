# API de Aplicativos e Syscalls

## Resumo de Progresso

Status: planejado para a proxima etapa de arquitetura.

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

## Contrato inicial da API

Os nomes abaixo sao a proposta inicial. Os numeros definitivos das syscalls
serao definidos durante a implementacao e documentados junto ao dispatcher.

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

Durante a implementacao, o Shell devera receber um comando de diagnostico
para testar a camada sem depender de um aplicativo completo. O nome planejado
e:

```text
appcheck
```

O comando devera testar somente chamadas seguras, como `console_write`,
`uptime` e `memory_info`, e exibir o codigo retornado por cada uma.

## Fases

### Fase 1 - Contrato e tipos comuns

- definir tipos de retorno, handles e versao da API;
- documentar limites de buffers e regras de ownership;
- separar interfaces publicas de estruturas internas;
- manter wrappers compativeis para os aplicativos nativos atuais.

### Fase 2 - Dispatcher de syscalls

- criar a entrada controlada de syscall;
- validar numero, argumentos e processo chamador;
- registrar chamadas invalidas sem travar o kernel;
- implementar primeiro `console_write`, `uptime`, `memory_info` e
  `process_exit`.

### Fase 3 - Servicos de arquivo e IPC

- criar handles de arquivo;
- expor leitura e escrita atraves do FS unificado;
- impedir acesso direto as estruturas FAT;
- reutilizar as validacoes existentes de IPC e recovery.

### Fase 4 - Processo em modo usuario

- preparar segmentos e stack de usuario;
- validar diretorios de pagina por processo;
- restringir acesso a memoria do kernel;
- retornar ao kernel somente pela entrada de syscall;
- manter o processo Idle e os servicos essenciais protegidos.

### Fase 5 - Carregador de aplicativos

- definir um formato inicial de programa;
- validar cabecalho, tamanho e pontos de entrada;
- carregar o programa em memoria isolada;
- iniciar, acompanhar e finalizar o processo;
- registrar falhas de carregamento no recovery.

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
- uma falha no aplicativo nao altera o estado do kernel;
- o Shell continua funcionando depois de encerrar um aplicativo;
- Explorer, Task Manager, Settings e Desktop continuam disponiveis;
- os modos classic e modern permanecem compativeis;
- o comando `health` mostra falhas da camada de aplicativos;
- o fallback nativo continua disponivel caso o carregador falhe;
- o build e a validacao no QEMU passam antes de cada nova fase.

## Limites

- Nenhuma syscall sera implementada nesta documentacao;
- o isolamento completo de processos ainda nao existe;
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
