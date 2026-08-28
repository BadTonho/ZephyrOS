#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_dispatch.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "fs/storage.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/wait.h"
#include "process/process.h"
#include "drivers/ata.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "apps/taskmanager.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
#include "ui/updater.h"
#include "ui/appstore.h"
#include "ui/wm.h"
#include "memory/compress.h"
#include "apps/mediaplayer.h"
#include "apps/editor.h"
#include "ui/filemanager.h"
#include "ui/icons.h"
#include "memory/paging.h"
#include "core/string.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "apps/guitest.h"
#include "core/recovery.h"
#include "core/device_manager.h"
#include "core/usb_manager.h"
#include "drivers/usb_msc.h"
#include "core/arp.h"
#include "core/dhcp.h"
#include "core/dns.h"
#include "core/http.h"
#include "core/ipv4.h"
#include "core/icmp.h"
#include "core/net_socket.h"
#include "core/tcp.h"
#include "core/udp.h"
#include "core/network_manager.h"
#include "core/power.h"
#include "core/app_api.h"
#include "core/app_catalog.h"
#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/app_remote.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_config.h"
#include "core/syscall.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/acpi.h"
#include "ui/display.h"
#include "apps/shell_command_utils.h"
#include "apps/shell_pipeline.h"
#include "apps/shell_runtime.h"

#define SHELL_Q2CHECK_FAULT_RUNS 2U
#define SHELL_HOSTED_DEFAULT_CONTENT_WIDTH 880
#define SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT 560
#define SHELL_HOSTED_FRAME_WIDTH 4
#define SHELL_HOSTED_FRAME_HEIGHT 28
#define SHELL_IPV4_TEXT_SIZE 16U
#define SHELL_IPV4_OCTET_COUNT 4U
#define SHELL_IPV4_OCTET_MAX 255U
#define SHELL_IPV4_OCTET_BITS 8U
#define SHELL_IPV4_OCTET_DIGITS 3U
#define SHELL_NET_QEMU_REPLY_IPV4 0x0A000202U
#define SHELL_NET_QEMU_TIMEOUT_IPV4 0x0A0002FEU
#define SHELL_NET_QEMU_SUBNET_MASK 0xFFFFFF00U
#define SHELL_NET_CHECK_WAIT_SECONDS 5U
#define SHELL_NET_CHECK_EXPECTED_ATTEMPTS 3U
#define SHELL_PING_WAIT_EXTRA_SECONDS 5U
#define SHELL_DHCP_WAIT_SECONDS 20U
#define SHELL_DNS_WAIT_SECONDS 20U
#define SHELL_TCP_WAIT_SECONDS 45U
#define SHELL_HTTP_WAIT_SECONDS 50U
#define SHELL_HTTP_SUITE_ATTEMPTS 3U
#define SHELL_HTTP_PREVIEW_SIZE 512U
#define SHELL_NETWORK_REQUIRED_IPV4_HANDLERS 3U
#define SHELL_DNS_NAME_SIZE DNS_NAME_BUFFER_SIZE
#define SHELL_MILLISECONDS_PER_SECOND 1000U
#define SHELL_MAX_TICK_INTERVAL 0xFFFFFFFFU
#define SHELL_NOINLINE __attribute__((noinline))
#define SHELL_UPDATE_SCANCODE_ESCAPE 0x01U
#define SHELL_UPDATE_SCANCODE_F12 0x58U
#define SHELL_INDEX_ACTION_SIZE 16U
#define SHELL_FS_DIRECTORY_ATTRIBUTE 0x10U
#define SHELL_LOG_TAIL_DEFAULT 10U
#define SHELL_LOG_TAIL_MAXIMUM 16U
#define SHELL_WHEEL_SCROLL_LINES 3
#define APP_CHECK_DEMO_PATH "DEMO.ZAP"
#define APP_CHECK_DEMO_MAX_CLEANUP 4U
#define APP_INPUT_TEST_PATH "INPUT.ZAP"
#define APP_INPUT_EVENT_OFFSET 128U
#define APP_INPUT_EVENT_DATA1_OFFSET (APP_INPUT_EVENT_OFFSET + 4U)
#define SHELL_APP_OUTPUTTEST_FAILURE_CODE 1U
#define SHELL_Q2CHECK_FIRST_FAULT_INDEX 0U
#define SHELL_Q2CHECK_SECOND_FAULT_INDEX 1U
#define SHELL_Q2CHECK_EXPECTED_FAULT_VECTOR 14U
#define SHELL_Q2CHECK_EXPECTED_FAULT_ERROR 4U
#define SHELL_MEMCHECK_BLOCK_A 96U
#define SHELL_MEMCHECK_BLOCK_B 160U
#define SHELL_MEMCHECK_BLOCK_C 224U
#define SHELL_REGCHECK_NON_ATA_DEVICE_COUNT 7U
#define SHELL_REGCHECK_PCI_NETWORK_CLASS 0x02U
#define SHELL_REGCHECK_ACPI_SDT_HEADER_SIZE 36U
#define SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE (1024U * 1024U)
#define SHELL_REGCHECK_ACPI_MAX_ROOT_ENTRIES 256U

typedef enum {
    SHELL_Q2CHECK_IDLE = 0,
    SHELL_Q2CHECK_FIRST_FAULT,
    SHELL_Q2CHECK_SECOND_FAULT
} shell_q2check_state_t;

typedef enum {
    SHELL_REGCHECK_IDLE = 0,
    SHELL_REGCHECK_WAIT_DEMO,
    SHELL_REGCHECK_WAIT_CANCEL
} shell_regcheck_state_t;

typedef struct {
    uint32_t initial_focus;
    uint32_t initial_user_count;
    uint32_t initial_zombie_count;
    uint32_t initial_fault_count;
    uint32_t expected_pid;
    int logger_result;
    int fault_result[SHELL_Q2CHECK_FAULT_RUNS];
    int summary_result;
    int cleanup_result;
    shell_q2check_state_t state;
} shell_q2check_t;

typedef struct {
    uint32_t initial_focus;
    uint32_t initial_process_count;
    uint32_t initial_user_count;
    uint32_t initial_zombie_count;
    uint32_t initial_user_directories;
    uint32_t initial_user_pages;
    uint32_t expected_pid;
    int health_result;
    int services_result;
    int scheduler_result;
    int memory_result;
    int package_result;
    int thread_result;
    int processes_result;
    int device_scan_result;
    int devices_result;
    int usb_result;
    int network_result;
    int acpi_result;
    int power_result;
    int index_result;
    int loader_result;
    int cancellation_result;
    int cleanup_result;
    uint8_t full_mode;
    uint8_t loader_started;
    uint8_t cancellation_started;
    shell_regcheck_state_t state;
} shell_regcheck_t;

typedef struct {
    uint8_t reply;
    uint8_t cache_hit;
    uint8_t timeout;
    uint8_t ipv4;
    uint8_t icmp;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_check_t;

typedef struct {
    uint8_t udp;
    uint8_t dhcp;
    uint8_t lease;
    uint8_t dns;
    uint8_t dns_cache;
    uint8_t icmp;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_dhcp_check_t;

typedef struct {
    uint8_t dhcp;
    uint8_t dns;
    uint8_t tcp;
    uint8_t checksum;
    uint8_t sockets;
    uint8_t http;
    uint8_t closing;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_tcp_check_t;

typedef struct {
    uint32_t ticks;
    keyboard_metrics_t keyboard;
    ipc_stats_t ipc;
    scheduler_stats_t scheduler;
    vesa_metrics_t vesa;
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;
    paging_user_stats_t paging_user;
    paging_boot_stats_t paging_boot;
} shell_kmetrics_snapshot_t;

typedef struct {
    shell_kmetrics_snapshot_t snapshot;
    uint8_t valid;
} shell_kmetrics_baseline_t;

typedef struct {
    char operation[16];
    char first[FS_MAX_PATH];
    char second[UPDATE_REMOTE_URL_SIZE];
    char third[32];
    char extra[2];
    update_remote_status_t remote_status;
    update_remote_options_t remote_options;
    update_remote_result_t remote_result;
} shell_update_workspace_t;

typedef struct {
    char operation[16];
    char value[FS_MAX_PATH];
    char option[16];
    char extra[16];
    app_catalog_entry_t entry;
    app_package_action_result_t action;
    app_package_status_t status;
    app_launch_info_t launch;
    uint32_t pid;
    char remote_url[APP_REMOTE_URL_SIZE];
    char remote_token[APP_REMOTE_URL_SIZE];
    app_remote_entry_t remote_entry;
    app_remote_status_t remote_status;
    app_remote_options_t remote_options;
    app_remote_result_t remote_result;
} shell_store_workspace_t;

typedef struct {
    char query[FILE_INDEX_QUERY_SIZE];
    file_index_result_t results[FILE_INDEX_MAX_RESULTS];
    file_index_search_status_t status;
} shell_index_workspace_t;


static uint32_t shell_echo_loader_pid = 0;
static uint32_t shell_builtin_loader_pid = 0;
static shell_builtin_app_t shell_builtin_loader_app = SHELL_BUILTIN_APP_NONE;
static vfs_dir_entry_t shell_vfs_dir_entries[VFS_MAX_DIR_ENTRIES];

const char* shell_core_builtin_app_name(shell_builtin_app_t app) {
    if (app == SHELL_BUILTIN_APP_UPTIME) return "uptime";
    if (app == SHELL_BUILTIN_APP_MEM) return "mem";
    return "desconhecido";
}

static void cmd_help_core(void) {
    video_print("  help     - Mostra esta mensagem\n", 0x07);
    video_print("  clear    - Limpa tela e historico do terminal\n", 0x07);
    video_print("  Setas cima/baixo - Navega comandos; Shift rola saida\n",
                0x08);
    video_print("  desktop  - Abre a area de trabalho\n", 0x07);
    video_print("  guimode  - Alterna Desktop simple/classic\n", 0x07);
    video_print("  display  - Mostra status ou altera escala da GUI\n", 0x07);
    video_print("  settings - Abre o painel de configuracoes\n", 0x07);
    video_print("  updater  - Abre o System Updater\n", 0x07);
    video_print("  wm       - Abre gerenciador de janelas\n", 0x07);
}

static void cmd_help(void) {
    video_begin_update();
    video_print("Comandos disponiveis:\n", 0x0B);
    video_print("  job status - Mostra o job cooperativo atual\n", 0x07);
    cmd_help_core();
    video_print("  ls       - Lista arquivos\n", 0x07);
    video_print("  cat      - Exibe conteudo de arquivo\n", 0x07);
    video_print("  grep <texto> - Filtra linhas de um pipeline\n", 0x07);
    video_print("  pipetest - Testa pipes e backpressure\n", 0x07);
    video_print("  mount    - Lista montagens VFS\n", 0x07);
    video_print("  pwd      - Exibe o diretorio atual\n", 0x07);
    video_print("  cd       - Altera o diretorio atual\n", 0x07);
    video_print("  echo     - Exibe texto\n", 0x07);
    video_print("  mem      - Mostra informacoes de memoria\n", 0x07);
    video_print("  slabinfo - Mostra caches SLAB\n", 0x07);
    video_print("  slabtest - Valida alocador SLAB\n", 0x07);
    video_print("  procs    - Mostra processos ativos\n", 0x07);
    video_print("  stack status|check - Diagnostica stacks dos processos\n", 0x07);
    video_print("  threads  - Mostra threads ativas\n", 0x07);
    video_print("  threadtest - Valida troca cooperativa de threads\n", 0x07);
    video_print("  uptime   - Mostra tempo ligado\n", 0x07);
    video_print("  beep     - Toca um beep (freq duracao_ms)\n", 0x07);
    video_print("  melody   - Toca uma melodia\n", 0x07);
    video_print("  explorer - Abre o gerenciador de arquivos\n", 0x07);
    video_print("  taskmgr  - Abre o gerenciador de tarefas\n", 0x07);
    video_print("  taskcfg  - Configura a barra de tarefas\n", 0x07);
    video_print("  compress - Liga/desliga compressao de RAM\n", 0x07);
    video_print("  stats    - Mostra estatisticas de compressao\n", 0x07);
    video_print("  mouse    - Status e preferencias do mouse PS/2\n", 0x07);
    video_print("  storage  - Lista, inspeciona e monta volumes\n", 0x07);
    video_print("  index status|rebuild|cancel|check - Controla o indice\n",
                0x07);
    video_print("  search <termo> - Pesquisa nomes e caminhos globais\n", 0x07);
    video_print("  health [summary|check] - Estado completo ou erros compactos\n",
                0x07);
    video_print("  log      - Consulta, configura e testa o log circular\n",
                0x07);
    video_print("  timer    - Inspeciona e testa temporizadores\n", 0x07);
    video_print("  clock status|check - Inspeciona UTC e monotono\n", 0x07);
    video_print("  tls status|check - Inspeciona BearSSL TLS/X.509\n",
                0x07);
    video_print("  wait     - Inspeciona esperas e executa autoteste\n", 0x07);
    video_print("  wqinfo   - Lista filas de espera e ordem FIFO\n", 0x07);
    video_print("  devices  - Lista inventario de hardware (-v para detalhes)\n", 0x07);
    video_print("  device-info <id> - Mostra detalhes de um dispositivo\n", 0x07);
    video_print("  device-scan - Refaz varredura PCI, USB e rede\n", 0x07);
    video_print("  usb status|list|ports|devices|storage|hid - Inspeciona USB\n", 0x07);
    video_print("  usb device <id> - Mostra detalhes de um controlador USB\n",
                0x07);
    video_print("  net status - Mostra capacidades atuais de rede\n", 0x07);
    video_print("  wifi status|scan|connect - Diagnostica candidatos Wi-Fi\n",
                0x07);
    video_print("  net devices - Lista controladores de rede PCI\n", 0x07);
    video_print("  net info <id> - Mostra detalhes de uma interface\n", 0x07);
    video_print("  net ethernet <id> - Inspeciona recepcao Ethernet L2\n", 0x07);
    video_print("  net test <id> - Envia frame Ethernet de diagnostico\n", 0x07);
    video_print("  net arp config <id> <ip> - Configura ARP em RAM\n", 0x07);
    video_print("  net arp status|table|clear - Inspeciona cache ARP\n", 0x07);
    video_print("  net arp resolve <ip> - Resolve IPv4 para MAC\n", 0x07);
    video_print("  net ipv4 config <id> <ip> <mask> <gw> - Configura IPv4\n",
                0x07);
    video_print("  net ipv4 status - Inspeciona IPv4 e ICMP\n", 0x07);
    video_print("  net udp status - Inspeciona datagramas e endpoints\n", 0x07);
    video_print("  net dhcp acquire <id>|status|renew|release\n", 0x07);
    video_print("  net dns config <ip>|status|table|clear\n", 0x07);
    video_print("  net tcp status|connect <host> <porta>\n", 0x07);
    video_print("  net socket status|table - Inspeciona sockets nativos\n",
                0x07);
    video_print("  http get <url>|status - Cliente HTTP/1.1\n", 0x07);
    video_print("  nslookup <dominio> - Resolve registro DNS A\n", 0x07);
    video_print("  ping <ip-ou-dominio> [1-10] - Executa ICMP Echo\n", 0x07);
    video_print("  net check [id] - Agrupa diagnosticos de rede\n", 0x07);
    video_print("  net check qemu <id> <ip> - Executa suite de rede\n", 0x07);
    video_print("  net check qemu dhcp <id> <dominio> - Suite S2.6\n", 0x07);
    video_print("  net check qemu tcp <id> <dominio> - Suite S2.7\n", 0x07);
    video_print("  net check qemu multi <id-a> <id-b> - Suite S2.8\n",
                0x07);
    video_print("  acpi status - Mostra tabelas e energia ACPI observadas\n", 0x07);
    video_print("  power status - Mostra capacidades reais de energia\n", 0x07);
    video_print("  irqstat [status|list|check] - Inspeciona IRQs e Bottom-Halves\n",
                0x07);
    video_print("  workq [status|list|check] - Inspeciona a kworker\n", 0x07);
    video_print("  kill -SINAL PID - Envia sinal a processo ring3\n", 0x07);
    video_print("  sigtest - Valida sinais assincronos\n", 0x07);
    video_print("  vfs [status|test] - Inspeciona descritores e operacoes de I/O\n",
                0x07);
    video_print("  devcheck - Valida dispositivos do devfs\n", 0x07);
    video_print("  kmetrics - Mostra linha-base de metricas do kernel\n", 0x07);
    video_print("  memcheck - Valida heap, PMM e diretorios de usuario\n", 0x07);
    video_print("  schedcheck - Valida invariantes do scheduler\n", 0x07);
    video_print("  q2check  - Executa diagnostico compacto da Q2\n", 0x07);
    video_print("  regcheck [full] - F11 cancela nos modos normal e full\n",
                0x07);
    video_print("  appcheck - Testa API, arquivos, IPC e loader\n", 0x07);
    video_print("  pkg      - Gerencia pacotes .ZPK locais\n", 0x07);
    video_print("             pkg list | info | verify | install | remove\n", 0x08);
    video_print("  store    - Consulta e gerencia a App Store local\n", 0x07);
    video_print("             store status | list | info <ID|alias.ZPK>\n",
                0x08);
    video_print("             store install/update/rollback/history (use --confirm)\n",
                0x08);
    video_print("  update verify <arquivo.ZUP> - Verifica sem gravar\n", 0x07);
    video_print("  update status|history - Diagnostico persistente\n",
                0x07);
    video_print("  update remote ...|fetch ... - Distribuicao U5\n",
                0x07);
    video_print("  update github check/fetch --tag <tag> - Release EP6.2 HTTPS\n",
                0x07);
    video_print("  update system status|check|fetch|verify|apply|cancel|slots\n",
                0x07);
    video_print("  update apply <arquivo.ZUP> [--confirm] - Aplica U3\n",
                0x07);
    video_print("  update rollback [--confirm] - Desfaz a ultima U3\n",
                0x07);
    video_print("  pkgcheck - Testa validacoes de pacote sem gravar\n", 0x07);
    video_print("  app run <arquivo.ZAP> [args] - Executa aplicativo ring 3\n", 0x07);
    video_print("  app inputtest [tty] - Testa entrada ring 3\n", 0x07);
    video_print("  app devtest - Testa dispositivos em ring 3\n", 0x07);
    video_print("  app outputtest [fail] - Testa saida ZAPP em blocos\n", 0x07);
    video_print("  app pathtest - Testa caminhos VFS em ring 3\n", 0x07);
    video_print("  app argtest <texto> - Testa argumentos em aplicativo ring 3\n", 0x07);
    video_print("  usertest - Executa teste isolado em ring 3\n", 0x07);
    video_print("             usertest fault | falha controlada\n", 0x08);
    video_print("  play     - Toca arquivo WAV\n", 0x07);
    video_print("  view     - Exibe imagem BMP\n", 0x07);
    video_print("  icons    - Mostra estado dos icones BMP\n", 0x07);
    video_print("  stop     - Para player de midia\n", 0x07);
    video_print("  edit     - Editor de texto\n", 0x07);
    video_print("             edit (novo) | edit arquivo.txt\n", 0x08);
    video_print("  reboot   - Reinicia o sistema\n", 0x07);
    video_print("  shutdown - Desliga por ACPI ou usa fallback HLT\n", 0x07);
    video_print("  guitest [modern] - Testa primitivas GUI 2D\n", 0x07);
    video_end_update();
}

static void cmd_clear(void) {
    video_terminal_clear();
    if (!shell_runtime_is_hosted_visible()) taskbar_draw();
}

static void cmd_ls(const char* path) {
    char cwd[VFS_MAX_PATH];
    uint32_t count = 0U;
    int result;

    if (!path || !*path) {
        result = vfs_getcwd(cwd, sizeof(cwd));
        if (result != OK) {
            video_print("Erro: diretorio atual indisponivel.\n", 0x0C);
            return;
        }
        path = cwd;
    }
    result = vfs_list_dir(path, shell_vfs_dir_entries,
                          VFS_MAX_DIR_ENTRIES, &count);
    if (result != OK) {
        video_print("Erro: diretorio VFS indisponivel (codigo ", 0x0C);
        shell_command_print_num(result);
        video_print(").\n", 0x0C);
        return;
    }
    if (!count) {
        shell_pipeline_write("  (vazio)\n", 0x08);
        return;
    }
    for (uint32_t index = 0U; index < count; index++) {
        shell_pipeline_write("  ", 0x07);
        shell_pipeline_write(shell_vfs_dir_entries[index].name,
                             shell_vfs_dir_entries[index].type ==
                             VFS_NODE_DIRECTORY ? 0x0B : 0x07);
        if (shell_vfs_dir_entries[index].type == VFS_NODE_DIRECTORY) {
            shell_pipeline_write("/", 0x0B);
        }
        shell_pipeline_write("\n", 0x07);
    }
}

static void cmd_cat(const char* filename) {
    int32_t fd = VFS_FD_INVALID;
    uint32_t bytes = 0U;
    int result;

    if (!filename || !*filename) {
        video_print("Uso: cat <arquivo>\n", 0x0C);
        return;
    }
    uint8_t* buffer = (uint8_t*)kmalloc(4096);
    if (!buffer) {
        LOG_ERROR("SHELL", "Falha ao alocar buffer do cat VFS");
        video_print("Erro: sem memoria!\n", 0x0C);
        return;
    }
    result = vfs_open(filename, VFS_MODE_READ, &fd);
    if (result == OK) result = vfs_read(fd, buffer, 4095U, &bytes);
    if (fd != VFS_FD_INVALID && vfs_close(fd) != OK && result == OK) {
        result = ERR_STATE;
    }
    if (result != OK) {
        video_print("Erro: leitura VFS recusada (codigo ", 0x0C);
        shell_command_print_num(result);
        video_print(").\n", 0x0C);
    } else if (bytes == 0U) {
        shell_pipeline_write("(arquivo vazio)\n", 0x08);
    } else {
        buffer[bytes] = '\0';
        shell_pipeline_write((char*)buffer, 0x07);
        shell_pipeline_write("\n", 0x07);
    }
    kfree(buffer);
    buffer = 0;
}

static void cmd_echo_native(const char* text) {
    if (text && *text) {
        shell_pipeline_write(text, 0x07);
    }
    shell_pipeline_write("\n", 0x07);
}

static void cmd_echo(const char* text) {
    app_launch_info_t launch;
    uint32_t pid = 0;
    int result;

    if (!text) text = "";
    if (shell_pipeline_is_active()) {
        cmd_echo_native(text);
        return;
    }
    result = app_loader_build_launch_info(text, &launch);
    if (result != OK) {
        LOG_WARN("SHELL", "Argumentos do echo rejeitados; usando fallback nativo");
        cmd_echo_native(text);
        return;
    }

    result = app_builtin_run_echo(&launch, &pid);
    if (result == OK) {
        shell_echo_loader_pid = pid;
        return;
    }
    if (result == ERR_UNAVAILABLE) {
        LOG_WARN("SHELL", "Echo ring 3 indisponivel; usando fallback nativo");
        cmd_echo_native(text);
        return;
    }

    video_print("Erro: echo ring 3 nao iniciou (codigo ", 0x0C);
    shell_command_print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static void cmd_mem_native(void) {
    video_print("Memoria:\n", 0x0B);
    video_print("  Total: ", 0x07);
    shell_command_print_num(memory_get_total() / 1024);
    video_print(" KB\n", 0x07);

    video_print("  Livre: ", 0x07);
    shell_command_print_num(memory_get_free() / 1024);
    video_print(" KB\n", 0x07);

    video_print("  Usada: ", 0x07);
    shell_command_print_num(memory_get_used() / 1024);
    video_print(" KB\n", 0x07);
}

static void cmd_procs(void) {
    shell_pipeline_write("Processos ativos:\n", 0x0B);

    extern process_t* processes[];
    extern uint32_t process_count;

    const char* state_names[] = {"UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE"};

    for (int i = 0; i < 64; i++) {
        if (processes[i] && processes[i]->state != 0) {
            shell_pipeline_write("  PID ", 0x07);
            shell_pipeline_print_num(processes[i]->pid);
            shell_pipeline_write("  ", 0x07);
            shell_pipeline_write(processes[i]->name, 0x0B);
            shell_pipeline_write("  ", 0x07);
            shell_pipeline_write(state_names[processes[i]->state], 0x08);
            shell_pipeline_write("\n", 0x07);
        }
    }

    shell_pipeline_write("Total: ", 0x07);
    shell_pipeline_print_num(process_count);
    shell_pipeline_write(" processos\n", 0x07);
}

static void shell_stack_print_info(const process_stack_info_t* info) {
    uint8_t canaries_ok;

    if (!info) return;
    canaries_ok = info->lower_canary_ok && info->upper_canary_ok;
    video_print("  PID ", 0x07);
    shell_command_print_num(info->pid);
    video_print(" ", 0x07);
    video_print(info->name, 0x0B);
    video_print(": tamanho=", 0x07);
    shell_command_print_num(info->stack_size);
    video_print(" uso=", 0x07);
    shell_command_print_num(info->bytes_used);
    video_print(" pico=", 0x07);
    shell_command_print_num(info->peak_bytes_used);
    video_print(" menor_folga=", 0x07);
    shell_command_print_num(info->minimum_bytes_free);
    video_print(" avisos=", 0x07);
    shell_command_print_num(info->low_water_events);
    video_print(" falhas=", 0x07);
    shell_command_print_num(info->overflow_events);
    video_print(" canarios=", 0x07);
    video_print(canaries_ok ? "OK" : "FALHOU", canaries_ok ? 0x0A : 0x0C);
    video_print(" margem=", 0x07);
    video_print(info->low_water_active ? "BAIXA" : "OK",
                info->low_water_active ? 0x0E : 0x0A);
    video_print("\n", 0x07);
}

static void cmd_stack(const char* arguments) {
    process_stack_validation_t validation;
    int result;

    if (!arguments || !*arguments ||
        shell_command_args_equal(arguments, "status")) {
        extern process_t* processes[];

        video_print("Stacks dos processos:\n", 0x0B);
        for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
            process_stack_info_t info;

            if (!processes[index]) continue;
            result = process_stack_get_info(processes[index]->pid, &info);
            if (result == OK || result == ERR_OVERFLOW) {
                shell_stack_print_info(&info);
            } else {
                video_print("  PID ", 0x0C);
                shell_command_print_num(processes[index]->pid);
                video_print(": diagnostico indisponivel (codigo ", 0x0C);
                shell_command_print_num((uint32_t)result);
                video_print(")\n", 0x0C);
            }
        }
        return;
    }

    if (!shell_command_args_equal(arguments, "check")) {
        video_print("Uso: stack status|check\n", 0x0E);
        return;
    }

    result = process_stack_validate_all(&validation);
    if (result == OK) result = process_stack_self_test();
    if (result == OK) {
        video_print("StackCheck: OK processos=", 0x0A);
        shell_command_print_num(validation.checked);
        video_print(" validos=", 0x0A);
        shell_command_print_num(validation.valid);
        video_print(" margem_baixa=0 canarios=0\n", 0x0A);
        return;
    }

    video_print("StackCheck: FALHOU codigo=", 0x0C);
    shell_command_print_num((uint32_t)result);
    video_print(" processos=", 0x0C);
    shell_command_print_num(validation.checked);
    video_print(" margem_baixa=", 0x0C);
    shell_command_print_num(validation.low_water);
    video_print(" canarios=", 0x0C);
    shell_command_print_num(validation.corrupted);
    video_print("\n", 0x0C);
}

static void cmd_threads(void) {
    video_print("Threads ativas:\n", 0x0B);

    const char* state_names[] = {"UNUSED", "RUNNING", "BLOCKED", "FINISHED"};
    uint32_t count = thread_get_count();

    for (uint32_t i = 0; i < 32; i++) {
        thread_t* t = thread_get_by_id(i + 1);
        if (t) {
            video_print("  TID ", 0x07);
            shell_command_print_num(t->id);
            video_print("  ", 0x07);
            video_print(t->name, 0x0B);
            video_print("  ", 0x07);
            video_print(state_names[t->state], 0x08);
            video_print("\n", 0x07);
        }
    }

    video_print("Total: ", 0x07);
    shell_command_print_num(count);
    video_print(" threads\n", 0x07);
}

static void cmd_uptime_native(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 50;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    video_print("Uptime: ", 0x0B);
    shell_command_print_num(hours);
    video_print("h ", 0x07);
    shell_command_print_num(minutes % 60);
    video_print("m ", 0x07);
    shell_command_print_num(seconds % 60);
    video_print("s\n", 0x07);
}

static void cmd_run_migrated_builtin(shell_builtin_app_t app) {
    uint32_t pid = 0;
    int result;

    if (app == SHELL_BUILTIN_APP_UPTIME) {
        result = app_builtin_run_uptime(&pid);
    } else if (app == SHELL_BUILTIN_APP_MEM) {
        result = app_builtin_run_mem(&pid);
    } else {
        LOG_ERROR("SHELL", "Comando migrado sem aplicativo definido");
        return;
    }
    if (result == OK) {
        shell_builtin_loader_pid = pid;
        shell_builtin_loader_app = app;
        return;
    }
    if (result == ERR_UNAVAILABLE) {
        LOG_WARN("SHELL", "Aplicativo ring 3 indisponivel; usando fallback nativo");
        if (app == SHELL_BUILTIN_APP_UPTIME) {
            cmd_uptime_native();
        } else {
            cmd_mem_native();
        }
        return;
    }

    LOG_ERROR("SHELL", "Aplicativo ring 3 nao iniciou");
    video_print("Erro: ", 0x0C);
    video_print(shell_core_builtin_app_name(app), 0x0C);
    video_print(" ring 3 nao iniciou (codigo ", 0x0C);
    shell_command_print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static void cmd_mem(void) {
    cmd_run_migrated_builtin(SHELL_BUILTIN_APP_MEM);
}

static void cmd_uptime(void) {
    cmd_run_migrated_builtin(SHELL_BUILTIN_APP_UPTIME);
}

static void cmd_beep(const char* args) {
    if (!args || !*args) {
        speaker_beep(800, 200);
        video_print("Beep!\n", 0x0A);
        return;
    }

    uint32_t freq = shell_command_parse_number(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;

    uint32_t dur = shell_command_parse_number(args);
    if (dur == 0) dur = 200;
    if (freq == 0) freq = 800;

    speaker_beep(freq, dur);
    video_print("Beep! (", 0x0A);
    shell_command_print_num(freq);
    video_print(" Hz, ", 0x0A);
    shell_command_print_num(dur);
    video_print(" ms)\n", 0x0A);
}

static void cmd_melody(void) {
    video_print("Tocando melodia...\n", 0x0A);

    uint32_t freqs[] = {523, 587, 659, 698, 784, 880, 988, 1047};
    uint32_t durs[] =  {200, 200, 200, 200, 200, 200, 200, 400};

    speaker_play_melody(freqs, durs, 8);
    video_print("Melodia concluida!\n", 0x0A);
}

void shell_core_reboot(void) {
    video_print("Reiniciando...\n", 0x0E);
    if (power_reboot() != OK) {
        video_print("Falha ao solicitar reinicio.\n", 0x0C);
    }
}

void shell_core_shutdown(const char* args) {
    if (args && *args) {
        LOG_WARN("SHELL", "Uso invalido de shutdown");
        video_print("Uso: shutdown\n", 0x0C);
        return;
    }
    video_print("Desligando...\n", 0x0E);
    power_shutdown();
}

static void cmd_threadtest(void) {
    int result = thread_run_self_test();

    video_print("Teste de threads: ", 0x0B);
    video_print(result == OK ? "OK\n" : "ERRO\n", result == OK ? 0x0A : 0x0C);
}

static void shell_report_builtin_failure(shell_builtin_app_t app,
                                         const app_loader_result_t* result) {
    if (!result || (!result->start_failed && !result->faulted &&
                    !result->cancelled && result->exit_code == OK)) {
        return;
    }

    video_print("\n[", 0x08);
    video_print("ERRO", 0x0C);
    video_print("] ", 0x07);
    video_print(shell_core_builtin_app_name(app), 0x07);
    video_print(" ring 3 nao concluiu (codigo ", 0x07);
    shell_command_print_num(result->exit_code);
    video_print(").\n", 0x07);
}

int shell_core_handle_loader_result(const app_loader_result_t* result) {
    if (!result) return 0;

    if (shell_echo_loader_pid == result->pid) {
        shell_echo_loader_pid = 0;
        if (result->start_failed || result->faulted || result->cancelled ||
            result->exit_code != OK) {
            video_print("\n[", 0x08);
            video_print("ERRO", 0x0C);
            video_print("] echo ring 3 nao concluiu (codigo ", 0x07);
            shell_command_print_num(result->exit_code);
            video_print(").\n", 0x07);
        }
        shell_runtime_finish_command();
        return 1;
    }

    if (shell_builtin_loader_pid == result->pid) {
        shell_report_builtin_failure(shell_builtin_loader_app, result);
        shell_builtin_loader_pid = 0;
        shell_builtin_loader_app = SHELL_BUILTIN_APP_NONE;
        shell_runtime_finish_command();
        return 1;
    }

    return 0;
}

#define SHELL_CORE_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

#define SHELL_CORE_WRAP_NO_ARGS(adapter, handler) \
    void adapter(const char* arguments) { (void)arguments; handler(); }

SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_help, cmd_help)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_clear, cmd_clear)
SHELL_CORE_WRAP_ARGS(shell_dispatch_cmd_ls, cmd_ls)
SHELL_CORE_WRAP_ARGS(shell_dispatch_cmd_cat, cmd_cat)
SHELL_CORE_WRAP_ARGS(shell_dispatch_cmd_echo, cmd_echo)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_mem, cmd_mem)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_procs, cmd_procs)
SHELL_CORE_WRAP_ARGS(shell_dispatch_cmd_stack, cmd_stack)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_threads, cmd_threads)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_threadtest, cmd_threadtest)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_uptime, cmd_uptime)
SHELL_CORE_WRAP_ARGS(shell_dispatch_cmd_beep, cmd_beep)
SHELL_CORE_WRAP_NO_ARGS(shell_dispatch_cmd_melody, cmd_melody)

void shell_dispatch_cmd_reboot(const char* arguments) {
    (void)arguments;
    shell_core_reboot();
}

void shell_dispatch_cmd_shutdown(const char* arguments) {
    shell_core_shutdown(arguments);
}

void shell_dispatch_cmd_compress(const char* arguments) {
    (void)arguments;
    if (compress_is_enabled()) {
        compress_disable();
        video_print("Compressao de RAM DESATIVADA\n", 0x0C);
    } else {
        compress_enable();
        video_print("Compressao de RAM ATIVADA\n", 0x0A);
    }
}

void shell_dispatch_cmd_stats(const char* arguments) {
    (void)arguments;
    compress_print_stats();
}

#undef SHELL_CORE_WRAP_ARGS
#undef SHELL_CORE_WRAP_NO_ARGS
