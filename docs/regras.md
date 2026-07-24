# Regras do Projeto — ZephyrOS

Regras obrigatórias para todo código escrito neste projeto.

---

## 1. Logging de Erros

**Toda função que pode falhar DEVE ter log de erro.**

### Níveis de log

| Nível | Quando usar | Cor |
|-------|-------------|-----|
| `LOG_ERROR` | Erro fatal que impede continuidade | Fundo vermelho |
| `LOG_WARN` | Erro recuperável ou situação inesperada | Marrom |
| `LOG_INFO` | Evento importante para debug | Ciano |
| `LOG_DEBUG` | Informação detalhada para desenvolvimento | Cinza |

### Regras

- [ ] Toda função de driver DEVE logar `LOG_ERROR` ao falhar
- [ ] Toda função de driver DEVE logar `LOG_INFO` na inicialização bem-sucedida
- [ ] Toda função de filesystem DEVE logar erros de leitura/escrita
- [ ] Toda função de memória DEVE logar falhas de alocação
- [ ] Toda operação de hardware DEVE logar timeout ou falha de comunicação
- [ ] Logs DEVE usar o módulo correto (ex: "ATA", "FAT12", "AC97", "PCI")
- [ ] Mensagens DEVE ser em português para facilitar entendimento

### Uso

```c
#include "core/log.h"

void driver_init(void) {
    LOG_INFO("DRIVER", "Inicializando driver...");

    if (hardware_fail) {
        LOG_ERROR("DRIVER", "Falha ao comunicar com hardware!");
        return;
    }

    LOG_INFO("DRIVER", "Driver inicializado com sucesso");
}
```

### Módulos existentes

| Módulo | Arquivo | Exemplo de log |
|--------|---------|----------------|
| `BOOT` | `boot.asm` | `LOG_INFO("BOOT", "Kernel carregado")` |
| `LOG` | `log.c` | `LOG_INFO("LOG", "Sistema de log inicializado")` |
| `IDT` | `idt.c` | `LOG_INFO("IDT", "Interrupcoes configuradas")` |
| `KBD` | `keyboard.c` | `LOG_INFO("KBD", "Teclado PS/2 inicializado")` |
| `TIMER` | `timer.c` | `LOG_INFO("TIMER", "PIT configurado a 50Hz")` |
| `MEM` | `memory.c` | `LOG_INFO("MEM", "Memoria detectada: 128MB")` |
| `ATA` | `ata.c` | `LOG_ERROR("ATA", "Falha ao ler setor!")` |
| `VESA` | `vesa.c` | `LOG_INFO("VESA", "Framebuffer detectado")` |
| `FAT12` | `fat12.c` | `LOG_INFO("FAT12", "Sistema de arquivos montado")` |
| `FAT32` | `fat32.c` | `LOG_INFO("FAT32", "Sistema de arquivos montado")` |
| `AC97` | `ac97.c` | `LOG_INFO("AC97", "Controladora de audio encontrada")` |
| `PCI` | `pci.c` | `LOG_INFO("PCI", "Enumeracao concluida: 5 dispositivos")` |
| `THRD` | `tss.c` | `LOG_INFO("THRD", "TSS inicializada")` |
| `SHELL` | `shell.c` | `LOG_INFO("SHELL", "Shell inicializado")` |
| `WM` | `wm.c` | `LOG_INFO("WM", "Gerenciador de janelas pronto")` |
| `PROC` | `process.c` | `LOG_WARN("PROC", "Limite de processos atingido")` |
| `FS` | `fs.c` | `LOG_INFO("FS", "Sistema de arquivos unificado ativo")` |
| `DESKTOP` | `desktop.c` | `LOG_ERROR("DESKTOP", "Tipo de icone invalido")` |
| `MOUSE` | `mouse.c` | `LOG_INFO("MOUSE", "Mouse PS/2 inicializado")` |
| `IPC` | `ipc.c` | `LOG_INFO("IPC", "Sistema de IPC inicializado")` |
| `GUI` | `gui.c` | `LOG_INFO("GUI", "Primitivas GUI inicializadas")` |
| `STRING` | `string.c` | `LOG_DEBUG("STRING", "kmemcpy: 256 bytes copiados")` |

### Integração com panic

Para erros fatais que derrubam o sistema, usar `panic()` após o log:

```c
if (falha_grave) {
    LOG_ERROR("MODULO", "Erro fatal: motivo");
    panic("MODULO: Erro fatal detectado");
}
```

---

## 2. Tratamento de Erros em Funções

Toda função que pode falhar DEVE retornar código de erro:

```c
// RETORNOS PADRÃO
#define OK        0
#define ERR_NULL  1
#define ERR_MEM   2
#define ERR_DISK  3
#define ERR_NOT_FOUND 4
#define ERR_OVERFLOW  5

int driver_operation(void* data) {
    if (!data) {
        LOG_ERROR("DRIVER", "Ponteiro nulo recebido");
        return ERR_NULL;
    }

    if (disk_read(...) != OK) {
        LOG_ERROR("DRIVER", "Falha na leitura do disco");
        return ERR_DISK;
    }

    return OK;
}
```

### Uso em chamadas

```c
int result = driver_operation(data);
if (result != OK) {
    LOG_ERROR("MODULO", "Operacao falhou");
    return result;
}
```

---

## 3. Inicialização de Módulos

Todo módulo DEVE:
- Ter uma função `xxx_init(void)` chamada no boot
- Logar `LOG_INFO` quando inicializar com sucesso
- Logar `LOG_ERROR` quando falhar
- Retornar código de erro (ou chamar panic para erros fatais)

```c
void module_init(void) {
    LOG_INFO("MODULO", "Inicializando...");

    if (!detect_hardware()) {
        LOG_ERROR("MODULO", "Hardware nao encontrado!");
        return;
    }

    if (configure() != OK) {
        LOG_ERROR("MODULO", "Falha na configuracao!");
        return;
    }

    LOG_INFO("MODULO", "Inicializado com sucesso");
}
```

---

## 4. Estrutura de Código

- [ ] Todo arquivo `.c` DEVE incluir `core/log.h` se tiver logs
- [ ] Funções DEVE ter no máximo 100 linhas
- [ ] Níveis de aninhamento máximos: 4
- [ ] Constantes DEVE serem definidas via `#define` (não magic numbers)
- [ ] Nomes de funções: `modulo_verbosa()` (ex: `ata_read_sector()`)
- [ ] Nomes de variáveis: `snake_case` (ex: `sector_count`)
- [ ] Nomes de constantes: `UPPER_SNAKE_CASE` (ex: `MAX_SECTORS`)

---

## 5. Comentários

- [ ] Funções DEVE ter comentário de uma linha explicando o que faz
- [ ] Parâmetros DEVE serem documentados se não óbvios
- [ ] Código complexo DEVE ter comentários explicando a lógica
- [ ] NÃO adicionar comentários óbvios (ex: `i++ // incrementa i`)

```c
// Lê setores do disco via PIO
// Retorna OK ou código de erro
int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!buffer) return ERR_NULL;

    // Seleciona drive master
    outb(ATA_PORT_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));

    // Envia LBA
    outb(ATA_PORT_LBA_LOW,  lba & 0xFF);
    outb(ATA_PORT_LBA_MID,  (lba >> 8) & 0xFF);
    outb(ATA_PORT_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_PORT_COMMAND,  ATA_CMD_READ);

    // Aguarda disco ficar pronto
    if (ata_wait_ready() != OK) {
        LOG_ERROR("ATA", "Timeout aguardando disco");
        return ERR_DISK;
    }

    // Lê 256 words (512 bytes)
    insw(ATA_PORT_DATA, buffer, 256);
    return OK;
}
```

---

## 6. Segurança

- [ ] NUNCA confiar em input do usuário — sempre validar
- [ ] NUNCA usar `strcpy` sem limite — usar `strncpy`
- [ ] SEMPRE verificar bounds antes de acessar array
- [ ] NUNCA logar dados sensíveis (senhas, chaves)
- [ ] SEMPRE inicializar variáveis antes de usar
- [ ] NUNCA usar `memcpy` sem validar tamanho

---

## 7. Performance

- [ ] Evitar alocações dinâmicas em loops
- [ ] Usar buffers estáticos quando possível
- [ ] Cache de dados quando fizer sentido
- [ ] Logs `LOG_DEBUG` não devem estar em código hot-path
- [ ] Usar `LOG_DEBUG` para logs detalhados que só precisam durante desenvolvimento

---

## 8. Gate de mudanca (Q3)

Antes de compilar ou testar uma alteracao de codigo, execute:

```bash
make q3check
```

O gate analisa somente o diff atual. Ele rejeita alteracoes no bootloader,
whitespace invalido, novas funcoes C que retornam `ERR_*` sem `LOG_ERROR` ou
`LOG_WARN`, e alteracoes em headers publicos sem atualizacao do documento
tecnico canonico definido no [catalogo de contratos](qualidade/contratos-publicos.md).
Ele tambem valida que cada evidencia registrada em
[metricas de otimizacao](qualidade/metricas.md) contenha todos os campos do
modelo.

Uma mudanca de desempenho deve registrar comparacao reproduzivel de antes e
depois em [metricas de otimizacao](qualidade/metricas.md). Se a mudanca nao
for uma otimizacao, a revisao registra `N/D`; nao crie uma metrica artificial.

O comando nao compila, nao altera o sistema e nao substitui `make clean &&
make` nem a validacao no QEMU. Para verificar o proprio gate sem tocar no
repositorio, execute `make q3check-test`.

---

## 9. Git

- [ ] Commits DEVE ter mensagens claras e descritivas
- [ ] Formato: `tipo(escopo): descrição`
- [ ] Tipos: `feat`, `fix`, `docs`, `refactor`, `test`
- [ ] Exemplos:
  - `feat(ata): implementa leitura de setores`
  - `fix(fat12): corrige erro na alocacao de clusters`
  - `docs: adiciona regras do projeto`

### Segurança antes do commit

Antes de preparar um commit, revisar somente o que aparece no Source Control:

- executar `git status --short` para identificar arquivos modificados, novos e staged;
- revisar principalmente `git diff --cached` e `git diff --cached --check`;
- verificar os arquivos que entrarão no commit contra senhas, tokens, chaves,
  credenciais, e-mails pessoais, caminhos locais, backups, dumps e artefatos de build;
- nunca incluir `.mailmap`, `Makefile.local`, `build/`, imagens de disco ou arquivos
  locais sem uma decisão explícita;
- não reexaminar arquivos que não foram alterados, salvo limpeza de histórico ou
  investigação de informação exposta anteriormente.

Na dúvida sobre um arquivo staged, interromper a preparação do commit e pedir
confirmação antes de prosseguir.

---

## Checklist de Revisão

Antes de commitar, verificar:

- [ ] Todo código compilou sem warnings?
- [ ] Toda função de erro tem `LOG_ERROR`?
- [ ] Toda inicialização bem-sucedida tem `LOG_INFO`?
- [ ] Não há magic numbers no código?
- [ ] Funções têm no máximo 100 linhas?
- [ ] Comentários explicam o "porquê", não o "o quê"?
- [ ] Não há variáveis não utilizadas?
- [ ] Não há memória leak (malloc sem free)?
