#include "apps/shell_dispatch.h"
#include "apps/shell_job.h"
#include "apps/shell_pipeline.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"

#define SHELL_DISPATCH_COMMAND_SIZE 32U
#define SHELL_DISPATCH_COMMAND_MAX_LENGTH \
    (SHELL_DISPATCH_COMMAND_SIZE - 1U)

extern void shell_dispatch_cmd_help(const char* arguments);
extern void shell_dispatch_cmd_clear(const char* arguments);
extern void shell_dispatch_cmd_ls(const char* arguments);
extern void shell_dispatch_cmd_cat(const char* arguments);
extern void shell_dispatch_cmd_echo(const char* arguments);
extern void shell_dispatch_cmd_mem(const char* arguments);
extern void shell_dispatch_cmd_procs(const char* arguments);
extern void shell_dispatch_cmd_stack(const char* arguments);
extern void shell_dispatch_cmd_threads(const char* arguments);
extern void shell_dispatch_cmd_threadtest(const char* arguments);
extern void shell_dispatch_cmd_uptime(const char* arguments);
extern void shell_dispatch_cmd_health(const char* arguments);
extern void shell_dispatch_cmd_log(const char* arguments);
extern void shell_dispatch_cmd_irqstat(const char* arguments);
extern void shell_dispatch_cmd_timer(const char* arguments);
extern void shell_dispatch_cmd_clock(const char* arguments);
extern void shell_dispatch_cmd_tls(const char* arguments);
extern void shell_dispatch_cmd_wait(const char* arguments);
extern void shell_dispatch_cmd_wqinfo(const char* arguments);
extern void shell_dispatch_cmd_workq(const char* arguments);
extern void shell_dispatch_cmd_kill(const char* arguments);
extern void shell_dispatch_cmd_sigtest(const char* arguments);
extern void shell_dispatch_cmd_vfs(const char* arguments);
extern void shell_dispatch_cmd_devcheck(const char* arguments);
extern void shell_dispatch_cmd_proccheck(const char* arguments);
extern void shell_dispatch_cmd_mount(const char* arguments);
extern void shell_dispatch_cmd_pwd(const char* arguments);
extern void shell_dispatch_cmd_cd(const char* arguments);
extern void shell_dispatch_cmd_devices(const char* arguments);
extern void shell_dispatch_cmd_device_info(const char* arguments);
extern void shell_dispatch_cmd_device_scan(const char* arguments);
extern void shell_dispatch_cmd_usb(const char* arguments);
extern void shell_dispatch_cmd_net(const char* arguments);
extern void shell_dispatch_cmd_skbstat(const char* arguments);
extern void shell_dispatch_cmd_sockstat(const char* arguments);
extern void shell_dispatch_cmd_selecttest(const char* arguments);
extern void shell_dispatch_cmd_netstat(const char* arguments);
extern void shell_dispatch_cmd_route(const char* arguments);
extern void shell_dispatch_cmd_wifi(const char* arguments);
extern void shell_dispatch_cmd_ping(const char* arguments);
extern void shell_dispatch_cmd_nslookup(const char* arguments);
extern void shell_dispatch_cmd_http(const char* arguments);
extern void shell_dispatch_cmd_acpi(const char* arguments);
extern void shell_dispatch_cmd_power(const char* arguments);
extern void shell_dispatch_cmd_kmetrics(const char* arguments);
extern void shell_dispatch_cmd_memcheck(const char* arguments);
extern void shell_dispatch_cmd_slabinfo(const char* arguments);
extern void shell_dispatch_cmd_slabtest(const char* arguments);
extern void shell_dispatch_cmd_pagefault(const char* arguments);
extern void shell_dispatch_cmd_vmamap(const char* arguments);
extern void shell_dispatch_cmd_schedcheck(const char* arguments);
extern void shell_dispatch_cmd_q2check(const char* arguments);
extern void shell_dispatch_cmd_regcheck(const char* arguments);
extern void shell_dispatch_cmd_appcheck(const char* arguments);
extern void shell_dispatch_cmd_blkcheck(const char* arguments);
extern void shell_dispatch_cmd_pkg(const char* arguments);
extern void shell_dispatch_cmd_store(const char* arguments);
extern void shell_dispatch_cmd_update(const char* arguments);
extern void shell_dispatch_cmd_pkgcheck(const char* arguments);
extern void shell_dispatch_cmd_app(const char* arguments);
extern void shell_dispatch_cmd_usertest(const char* arguments);
extern void shell_dispatch_cmd_beep(const char* arguments);
extern void shell_dispatch_cmd_melody(const char* arguments);
extern void shell_dispatch_cmd_desktop(const char* arguments);
extern void shell_dispatch_cmd_guimode(const char* arguments);
extern void shell_dispatch_cmd_display(const char* arguments);
extern void shell_dispatch_cmd_explorer(const char* arguments);
extern void shell_dispatch_cmd_reboot(const char* arguments);
extern void shell_dispatch_cmd_shutdown(const char* arguments);
extern void shell_dispatch_cmd_guitest(const char* arguments);
extern void shell_dispatch_cmd_taskmgr(const char* arguments);
extern void shell_dispatch_cmd_taskcfg(const char* arguments);
extern void shell_dispatch_cmd_settings(const char* arguments);
extern void shell_dispatch_cmd_updater(const char* arguments);
extern void shell_dispatch_cmd_wm(const char* arguments);
extern void shell_dispatch_cmd_play(const char* arguments);
extern void shell_dispatch_cmd_view(const char* arguments);
extern void shell_dispatch_cmd_icons(const char* arguments);
extern void shell_dispatch_cmd_stop(const char* arguments);
extern void shell_dispatch_cmd_compress(const char* arguments);
extern void shell_dispatch_cmd_stats(const char* arguments);
extern void shell_dispatch_cmd_edit(const char* arguments);
extern void shell_dispatch_cmd_storage(const char* arguments);
extern void shell_dispatch_cmd_blkstat(const char* arguments);
extern void shell_dispatch_cmd_cachestat(const char* arguments);
extern void shell_dispatch_cmd_cache(const char* arguments);
extern void shell_dispatch_cmd_sync(const char* arguments);
extern void shell_dispatch_cmd_index(const char* arguments);
extern void shell_dispatch_cmd_search(const char* arguments);
extern void shell_dispatch_cmd_mouse(const char* arguments);
extern void shell_dispatch_cmd_grep(const char* arguments);
extern void shell_dispatch_cmd_pipetest(const char* arguments);

static const shell_dispatch_entry_t shell_dispatch_table[] = {
    {"job", shell_dispatch_cmd_job, SHELL_DISPATCH_FLAG_NONE},
    {"help", shell_dispatch_cmd_help, SHELL_DISPATCH_FLAG_NONE},
    {"clear", shell_dispatch_cmd_clear, SHELL_DISPATCH_FLAG_NONE},
    {"ls", shell_dispatch_cmd_ls, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"cat", shell_dispatch_cmd_cat, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"echo", shell_dispatch_cmd_echo, SHELL_DISPATCH_FLAG_NONE},
    {"grep", shell_dispatch_cmd_grep, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"pipetest", shell_dispatch_cmd_pipetest,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"mem", shell_dispatch_cmd_mem, SHELL_DISPATCH_FLAG_NONE},
    {"procs", shell_dispatch_cmd_procs, SHELL_DISPATCH_FLAG_NONE},
    {"stack", shell_dispatch_cmd_stack, SHELL_DISPATCH_FLAG_NONE},
    {"threads", shell_dispatch_cmd_threads, SHELL_DISPATCH_FLAG_NONE},
    {"threadtest", shell_dispatch_cmd_threadtest,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"uptime", shell_dispatch_cmd_uptime, SHELL_DISPATCH_FLAG_NONE},
    {"health", shell_dispatch_cmd_health, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"log", shell_dispatch_cmd_log, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"irqstat", shell_dispatch_cmd_irqstat, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"timer", shell_dispatch_cmd_timer, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"clock", shell_dispatch_cmd_clock, SHELL_DISPATCH_FLAG_NONE},
    {"tls", shell_dispatch_cmd_tls, SHELL_DISPATCH_FLAG_NONE},
    {"wait", shell_dispatch_cmd_wait, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"wqinfo", shell_dispatch_cmd_wqinfo, SHELL_DISPATCH_FLAG_NONE},
    {"workq", shell_dispatch_cmd_workq, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"kill", shell_dispatch_cmd_kill, SHELL_DISPATCH_FLAG_NONE},
    {"sigtest", shell_dispatch_cmd_sigtest, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"vfs", shell_dispatch_cmd_vfs, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"devcheck", shell_dispatch_cmd_devcheck, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"proccheck", shell_dispatch_cmd_proccheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"mount", shell_dispatch_cmd_mount, SHELL_DISPATCH_FLAG_NONE},
    {"pwd", shell_dispatch_cmd_pwd, SHELL_DISPATCH_FLAG_NONE},
    {"cd", shell_dispatch_cmd_cd, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"devices", shell_dispatch_cmd_devices, SHELL_DISPATCH_FLAG_NONE},
    {"device-info", shell_dispatch_cmd_device_info,
     SHELL_DISPATCH_FLAG_NONE},
    {"device-scan", shell_dispatch_cmd_device_scan,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"usb", shell_dispatch_cmd_usb, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"net", shell_dispatch_cmd_net,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"skbstat", shell_dispatch_cmd_skbstat, SHELL_DISPATCH_FLAG_NONE},
    {"sockstat", shell_dispatch_cmd_sockstat, SHELL_DISPATCH_FLAG_NONE},
    {"selecttest", shell_dispatch_cmd_selecttest,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"netstat", shell_dispatch_cmd_netstat, SHELL_DISPATCH_FLAG_NONE},
    {"route", shell_dispatch_cmd_route, SHELL_DISPATCH_FLAG_NONE},
    {"wifi", shell_dispatch_cmd_wifi, SHELL_DISPATCH_FLAG_NONE},
    {"ping", shell_dispatch_cmd_ping,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"nslookup", shell_dispatch_cmd_nslookup,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"http", shell_dispatch_cmd_http,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"acpi", shell_dispatch_cmd_acpi, SHELL_DISPATCH_FLAG_NONE},
    {"power", shell_dispatch_cmd_power, SHELL_DISPATCH_FLAG_NONE},
    {"kmetrics", shell_dispatch_cmd_kmetrics, SHELL_DISPATCH_FLAG_NONE},
    {"memcheck", shell_dispatch_cmd_memcheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"slabinfo", shell_dispatch_cmd_slabinfo, SHELL_DISPATCH_FLAG_NONE},
    {"slabtest", shell_dispatch_cmd_slabtest, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"pagefault", shell_dispatch_cmd_pagefault, SHELL_DISPATCH_FLAG_NONE},
    {"vmamap", shell_dispatch_cmd_vmamap, SHELL_DISPATCH_FLAG_NONE},
    {"schedcheck", shell_dispatch_cmd_schedcheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"q2check", shell_dispatch_cmd_q2check,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"regcheck", shell_dispatch_cmd_regcheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"appcheck", shell_dispatch_cmd_appcheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"blkcheck", shell_dispatch_cmd_blkcheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"pkg", shell_dispatch_cmd_pkg,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"store", shell_dispatch_cmd_store,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"update", shell_dispatch_cmd_update,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"pkgcheck", shell_dispatch_cmd_pkgcheck,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"app", shell_dispatch_cmd_app,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"usertest", shell_dispatch_cmd_usertest,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"beep", shell_dispatch_cmd_beep, SHELL_DISPATCH_FLAG_NONE},
    {"melody", shell_dispatch_cmd_melody, SHELL_DISPATCH_FLAG_NONE},
    {"desktop", shell_dispatch_cmd_desktop,
     SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"guimode", shell_dispatch_cmd_guimode,
     SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"display", shell_dispatch_cmd_display,
     SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"explorer", shell_dispatch_cmd_explorer,
     SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"reboot", shell_dispatch_cmd_reboot, SHELL_DISPATCH_FLAG_NONE},
    {"shutdown", shell_dispatch_cmd_shutdown, SHELL_DISPATCH_FLAG_NONE},
    {"guitest", shell_dispatch_cmd_guitest,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"taskmgr", shell_dispatch_cmd_taskmgr,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"taskcfg", shell_dispatch_cmd_taskcfg,
     SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"settings", shell_dispatch_cmd_settings,
     SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"updater", shell_dispatch_cmd_updater,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"wm", shell_dispatch_cmd_wm, SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"play", shell_dispatch_cmd_play,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"view", shell_dispatch_cmd_view,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"icons", shell_dispatch_cmd_icons, SHELL_DISPATCH_FLAG_NONE},
    {"stop", shell_dispatch_cmd_stop, SHELL_DISPATCH_FLAG_NONE},
    {"compress", shell_dispatch_cmd_compress, SHELL_DISPATCH_FLAG_NONE},
    {"stats", shell_dispatch_cmd_stats, SHELL_DISPATCH_FLAG_NONE},
    {"edit", shell_dispatch_cmd_edit,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_OPENS_SCENE},
    {"storage", shell_dispatch_cmd_storage,
     SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"blkstat", shell_dispatch_cmd_blkstat, SHELL_DISPATCH_FLAG_NONE},
    {"cachestat", shell_dispatch_cmd_cachestat,
     SHELL_DISPATCH_FLAG_NONE},
    {"cache", shell_dispatch_cmd_cache, SHELL_DISPATCH_FLAG_NONE},
    {"sync", shell_dispatch_cmd_sync, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"index", shell_dispatch_cmd_index,
     SHELL_DISPATCH_FLAG_MAY_BLOCK | SHELL_DISPATCH_FLAG_COOPERATIVE},
    {"search", shell_dispatch_cmd_search, SHELL_DISPATCH_FLAG_MAY_BLOCK},
    {"mouse", shell_dispatch_cmd_mouse, SHELL_DISPATCH_FLAG_NONE}
};

static void shell_dispatch_print_unknown(const char* command) {
    video_print("Comando nao encontrado: ", 0x0C);
    video_print(command, 0x0C);
    video_print("\n", 0x0C);
    video_print("Digite 'help' para ver os comandos.\n", 0x08);
}

int shell_dispatch_execute(const char* input) {
    char command[SHELL_DISPATCH_COMMAND_SIZE];
    const char* cursor = input;
    uint8_t pipeline_handled;
    int pipeline_result;
    uint32_t command_length = 0U;
    uint32_t command_count =
        sizeof(shell_dispatch_table) / sizeof(shell_dispatch_table[0]);

    if (!input) {
        LOG_ERROR("SHELL", "Entrada nula no dispatcher");
        return ERR_NULL;
    }
    pipeline_result = shell_pipeline_try_execute(input, &pipeline_handled);
    if (pipeline_handled) return pipeline_result;

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n' || *cursor == 27) {
        cursor++;
    }

    if (!*cursor) return OK;

    while (*cursor && *cursor != ' ' && *cursor != '\t' &&
           command_length < SHELL_DISPATCH_COMMAND_MAX_LENGTH) {
        if ((uint8_t)*cursor >= ' ' && (uint8_t)*cursor <= '~') {
            command[command_length++] = *cursor;
        }
        cursor++;
    }
    command[command_length] = '\0';

    while (*cursor == ' ' || *cursor == '\t') cursor++;

    for (uint32_t index = 0U; index < command_count; index++) {
        const shell_dispatch_entry_t* entry = &shell_dispatch_table[index];

        if (kstrcmp(command, entry->name) == 0) {
            entry->handler(cursor);
            return OK;
        }
    }

    shell_dispatch_print_unknown(command);
    return OK;
}
