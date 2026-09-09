# SEC2 — Auditoria de memória e syscalls

## Escopo e estado

Esta matriz registra o contrato observado e os reforços implementados na SEC2
do Roadmap 19. A ABI ring 3, as assinaturas das syscalls e da App API, o
`process_t` público, o bootloader, Rust e o scheduler permanecem inalterados.

A aplicação efetiva de UID/GID, metadados de proprietário e permissões no
`open` continua pendente para a SEC5. A SEC2 valida isolamento, memória,
ownership e ciclo de vida dos recursos sem transformar o VFS atual em um
sistema de permissões.

## Convenções de fronteira

- Entradas ring 3 são endereços e escalares, nunca ponteiros de kernel.
- Toda faixa usa endereço inicial e tamanho validados antes da cópia; o
  intervalo não pode sair de `USER_SPACE_START..USER_SPACE_END`, sofrer
  wraparound ou atravessar uma página ausente.
- `write == 0` valida leitura pelo kernel; `write == 1` valida escrita pelo
  kernel. Qualquer outro modo é `ERR_INVALID`.
- Strings são copiadas para buffers internos, limitadas pelo contrato e devem
  conter terminador. O kernel não guarda o endereço recebido pelo processo.
- Estruturas de entrada/saída são copiadas para áreas internas antes da App
  API, VFS ou IPC. A publicação ocorre somente depois de o resultado estar
  completo.
- Operações que criam recursos desfazem o recurso quando a publicação do
  handle ou endereço no processo falha. O `file_open`, `pipe` e `mmap` não
  deixam o recurso publicado apenas no kernel nesses caminhos.
- O destino de `message_receive` é validado novamente depois de uma espera,
  antes da cópia final.

## Matriz de syscalls ring 3

| Operação | Entrada e validação | Saída, ownership e mutabilidade | Erros e teste específico |
|---|---|---|---|
| `console_write` | `EBX` aponta texto; `ECX` é tamanho limitado por `APP_API_MAX_TEXT_SIZE`; copia para buffer privado | App API só recebe a cópia; entrada não é retida | `ERR_NULL`, `ERR_OVERFLOW`, erro de cópia; `test_syscall_host.c:test_user_syscalls` |
| `uptime`, `memory_info` | `EBX` é faixa de saída gravável com tamanho da estrutura | resultado nasce em estrutura local e só depois é publicado | `ERR_NULL`, `ERR_INVALID`, `ERR_UNAVAILABLE`; `test_syscall_host.c:test_user_syscalls` |
| `file_open` | caminho em `EBX` é string limitada; `EDX` é saída gravável | caminho é buffer local; handle só pertence à tabela após abertura; falha ao publicar fecha o handle | `ERR_OVERFLOW`, `ERR_INVALID`, `ERR_MEM`; rollback coberto por `test_syscall_host.c:test_user_syscalls` |
| `chdir`, `getcwd` | caminho de entrada copiado; saída de `getcwd` validada pelo tamanho solicitado | VFS recebe string local; cwd só muda após validação da entrada | `ERR_NULL`, `ERR_OVERFLOW`, `ERR_INVALID`; `test_syscall_host.c:test_user_syscalls` |
| `file_read` | buffer de saída e contador são faixas graváveis; tamanho limitado; buffer interno recebe a leitura | App API não recebe ponteiro de usuário; bytes e dados só são publicados após leitura válida | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_UNAVAILABLE`; `test_syscall_host.c:test_user_syscalls` |
| `file_write` | buffer de entrada é faixa legível; contador é saída gravável; tamanho limitado | dados são copiados para buffer privado antes do VFS | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_UNAVAILABLE`; `test_syscall_host.c:test_user_syscalls` |
| `poll` | quantidade limitada; multiplicação de `count * sizeof(pollfd_t)` protegida; array é entrada/saída gravável | array local é usado pelo VFS e copiado de volta; contador é saída local | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`; `test_syscall_host.c:test_user_syscalls` |
| `select` | requisição única, faixa gravável, argumentos de registrador excedentes rejeitados | requisição local é processada e publicada somente em sucesso | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`; `test_syscall_host.c:test_user_syscalls` |
| `file_close`, `fsync`, `sync` | apenas escalares e handles; não há ponteiro ring 3 para armazenar | VFS valida handle ativo e estado do descritor | `ERR_INVALID`, `ERR_STATE`, `ERR_UNAVAILABLE`; `test_vfs_host.c` e `test_syscall_host.c` |
| `file_lseek` | saída de posição é faixa gravável; offset e whence são escalares | posição é calculada no kernel e publicada em sucesso | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`; `test_syscall_host.c:test_user_syscalls` |
| `file_ioctl` | `BEEP` copia estrutura de tom de faixa legível; `STOP` rejeita argumento | driver recebe estrutura local; nenhum ponteiro de usuário é retido | `ERR_NULL`, `ERR_INVALID`, `ERR_UNAVAILABLE`; `test_syscall_host.c:test_user_syscalls` |
| `pipe` | saída de dois handles é faixa gravável | handles são criados pelo VFS e ambos são fechados se a publicação falhar | `ERR_NULL`, `ERR_INVALID`, `ERR_MEM`; rollback coberto por `test_syscall_host.c:test_user_syscalls` |
| `message_send` | mensagem é faixa legível e copiada para estrutura local | IPC recebe somente a cópia; PID continua escalar | `ERR_NULL`, `ERR_INVALID`, `ERR_NOT_FOUND`; `test_syscall_host.c:test_user_syscalls` |
| `message_receive` | saída é faixa gravável; validação é repetida após `ipc_wait` | IPC preenche mensagem local; processo recebe cópia final | `ERR_NULL`, `ERR_INVALID`, `ERR_NOT_FOUND`, `ERR_AGAIN`; `test_syscall_host.c:test_user_syscalls` |
| `mmap` | saída de endereço é faixa gravável; comprimento/proteção/flags passam pela VMA | VMA é criada antes da publicação; falha de publicação chama `munmap` de rollback | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_MEM`, `ERR_UNAVAILABLE`; rollback coberto por `test_syscall_host.c:test_user_syscalls` e `test_vma_host.c` |
| `munmap` | endereço e comprimento são escalares alinhados e limitados pela VMA | VMA e páginas permanecem sob ownership do processo; preflight rejeita página supervisora | `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_STATE`; `test_vma_host.c:test_mmap_limits_and_unmap` |
| `signal_action`, `signal_mask` | ações e máscaras de entrada/saída são copiadas/validadas separadamente | sinal usa estruturas locais; nenhuma ação aponta para memória ring 3 após o retorno | `ERR_NULL`, `ERR_INVALID`, `ERR_STATE`; `test_syscall_host.c` e `test-process-signal-host` |
| `signal_raise`, `signal_return` | somente identificadores e frame controlados pelo kernel | entrega/reentrada seguem o estado do processo | `ERR_INVALID`, `ERR_STATE`, `ERR_UNAVAILABLE`; `test-process-signal-host` |
| syscall desconhecida | número não pertence à tabela pública | nenhum efeito persistente | `ERR_INVALID`; `test_syscall_host.c:test_user_syscalls` |

As rotas ring 0 continuam confiáveis e não recebem a mesma cópia de usuário;
isso não concede a um processo ring 3 uma rota alternativa, pois o dispatcher
seleciona o contrato pelo seletor de código e valida o chamador antes da
operação.

## Paging e VMA

| Operação | Garantia SEC2 | Erro canônico | Teste |
|---|---|---|---|
| `paging_validate_user_range` | rejeita nulo/vazio, modo diferente de leitura/escrita, limites, wraparound, página ausente, página não usuária e escrita em página somente leitura; percorre todas as páginas | `ERR_NULL`, `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_UNAVAILABLE`, `ERR_STATE` | `test_paging_host.c:test_paging` |
| `paging_copy_from_user` | valida a faixa legível antes de copiar e resolve a fixture inteira, inclusive faixa cruzando páginas | `ERR_NULL`, `ERR_INVALID`, `ERR_UNAVAILABLE` | `test_paging_host.c:test_paging` |
| `paging_copy_to_user` | valida a faixa gravável antes de copiar e não expõe ponteiro privado | `ERR_NULL`, `ERR_INVALID`, `ERR_UNAVAILABLE` | `test_paging_host.c:test_paging` |
| `process_vma_ensure_page` | exige processo user atual, modo válido, endereço user e VMA compatível | `ERR_INVALID`, `ERR_STATE`, `ERR_UNAVAILABLE`, `ERR_MEM` | `test_vma_host.c:test_image_and_faults` |
| `process_vma_mmap` | arredonda com proteção contra overflow, encontra gap limitado, cria metadata somente após quota e publica endereço no fim | `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_MEM`, `ERR_UNAVAILABLE` | `test_vma_host.c:test_mmap_limits_and_unmap` |
| `process_vma_munmap` | exige alinhamento, faixa dinâmica sem wraparound e VMA anônima; faz preflight de todas as páginas antes do desmapeamento | `ERR_INVALID`, `ERR_OVERFLOW`, `ERR_STATE`, `ERR_NOT_FOUND` | `test_vma_host.c:test_mmap_limits_and_unmap` |

O rollback de `munmap` é obtido por preflight: páginas supervisoras, faixas
inválidas e VMAs incompatíveis são rejeitadas antes de liberar qualquer página.
O allocator e o contador de recursos só são atualizados depois da operação
válida; uma falha de publicação de `mmap` é desfeita pela syscall antes de
retornar ao processo.

## Handles, VFS e App API

O VFS mantém a validação de fd dentro da tabela do processo, rejeita fd fora
do limite, descritor fechado, handle sem vnode, operação concorrente pendente e
uso após `close`. `vfs_close` remove a entrada antes de liberar o arquivo e a
restaura quando o callback de fechamento falha; isso evita double close e
preserva o estado anterior no caminho de erro. Leituras, escritas, `lseek`,
`ioctl`, `poll`, `select` e pipes validam buffers, limites, modo do arquivo e
estado antes de chamar o driver ou a fila.

Esses contratos continuam cobertos pelas fixtures existentes:

- `tests/unit/test_vfs_host.c`: fd obsoleto, double close, buffers nulos,
  limites de I/O, pipes, poll/select, socket e cleanup;
- `tests/unit/test_app_api_host.c`: readiness, saída nula, handles, I/O,
  pipes, IPC e códigos propagados pela facade;
- `tests/unit/test_syscall_host.c`: publicação de handles e rollback no
  processo ring 3.

Não foi necessário alterar a ABI de `app_api.h` ou `vfs.h` nesta etapa.

## Itens deliberadamente pendentes

- UID/GID efetivo, credenciais persistentes, proprietário/modo no VFS e decisão
  de permissão no `open`: SEC5.
- Revalidação de PID/generation em callbacks administrativos tardios: SEC3.
- Matriz adversarial completa, TST7, QEMU, perfis de hardware ausente e
  validação final do Roadmap 19: fechamento do roadmap.

## Validação da etapa

Os testes essenciais da etapa foram executados e passaram:

`make test-syscall-host test-paging-host test-vma-host test-vfs-host
test-app-api-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`

Resultado: `PASS` nos cinco alvos host-only.

Como gates essenciais de integração, também passaram `make q3check`, `make
clean` e `make` após a reconstrução limpa. O build registrou apenas warnings
legados fora da SEC2, sem erro de compilação, linkedição ou composição da
imagem. A suíte completa do projeto, QEMU, TST7 e a matriz adversarial final
permanecem `PENDING` para o fechamento do Roadmap 19; essa ausência não altera
o contrato nem oculta uma falha.
