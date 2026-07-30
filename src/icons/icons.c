#include "ui/icons.h"
#include "fs/bmp.h"
#include "fs/fs.h"
#include "core/memory.h"
#include "core/errors.h"
#include "core/log.h"

#define ICONS_DESKTOP_BMP_MAX_SIZE 4096
#define ICONS_DESKTOP_BMP_SIZE 32
#define ICONS_WARNING_FILESYSTEM 0x01u
#define ICONS_WARNING_MEMORY     0x02u
#define ICONS_WARNING_FILE       0x04u
#define ICONS_WARNING_FORMAT     0x08u

typedef struct {
    const char* filename;
    bmp_image_t image;
    int status;
} desktop_bmp_cache_t;

static icon_registry_t registry;
static desktop_bmp_cache_t desktop_bmps[ICON_DESKTOP_COUNT] = {
    {"SHELL.BMP", {0}, ERR_UNAVAILABLE},
    {"EXPLORER.BMP", {0}, ERR_UNAVAILABLE},
    {"TASKMGR.BMP", {0}, ERR_UNAVAILABLE}
};
static uint8_t emitted_warning_mask = 0;

static void icons_warn_once(uint8_t cause, const char* message) {
    if ((emitted_warning_mask & cause) != 0) return;

    emitted_warning_mask |= cause;
    LOG_WARN("ICONS", message);
}

static void icons_set_defaults(void) {
    registry.desktop[ICON_DESKTOP_SHELL] = (icon_entry_t){'S', 0x0F, 0x17};
    registry.desktop[ICON_DESKTOP_EXPLORER] = (icon_entry_t){'E', 0x0F, 0x17};
    registry.desktop[ICON_DESKTOP_TASKMGR] = (icon_entry_t){'T', 0x0F, 0x17};

    registry.wm[ICON_WM_CLOSE] = (icon_entry_t){'x', 0x4F, 0x4F};
    registry.wm[ICON_WM_MINIMIZE] = (icon_entry_t){'_', 0x1F, 0x08};
    registry.wm[ICON_WM_MAXIMIZE] = (icon_entry_t){0x10, 0x1F, 0x08};

    registry.fm[ICON_FM_FOLDER] = (icon_entry_t){'[', 0x0B, 0x70};
    registry.fm[ICON_FM_FILE] = (icon_entry_t){'-', 0x08, 0x70};

    registry.tb[ICON_TB_START] = (icon_entry_t){'>', 0x0F, 0x1F};
}

static void icons_release_desktop_bmps(void) {
    for (int i = 0; i < ICON_DESKTOP_COUNT; i++) {
        bmp_free(&desktop_bmps[i].image);
        desktop_bmps[i].status = ERR_UNAVAILABLE;
    }
}

static int icons_load_desktop_bmp(icon_desktop_id_t id) {
    desktop_bmp_cache_t* cache;
    uint8_t* raw_data;
    int bytes_read;
    int result;

    if (id >= ICON_DESKTOP_COUNT) {
        LOG_ERROR("ICONS", "Identificador de bitmap de Desktop invalido");
        return ERR_INVALID;
    }
    cache = &desktop_bmps[id];
    raw_data = (uint8_t*)kmalloc(ICONS_DESKTOP_BMP_MAX_SIZE);
    if (!raw_data) {
        icons_warn_once(ICONS_WARNING_MEMORY,
                        "Memoria insuficiente para cache de bitmap");
        cache->status = ERR_MEM;
        return cache->status;
    }

    bytes_read = fs_read_file_at(cache->filename, raw_data,
                                 ICONS_DESKTOP_BMP_MAX_SIZE);
    if (bytes_read <= 0) {
        icons_warn_once(ICONS_WARNING_FILE,
                        "Arquivo de bitmap de Desktop indisponivel");
        kfree(raw_data);
        cache->status = ERR_NOT_FOUND;
        return cache->status;
    }

    result = bmp_load(raw_data, (uint32_t)bytes_read, &cache->image);
    kfree(raw_data);
    if (result != OK) {
        icons_warn_once(result == ERR_MEM ? ICONS_WARNING_MEMORY :
                        ICONS_WARNING_FORMAT,
                        result == ERR_MEM ?
                        "Memoria insuficiente para bitmap de Desktop" :
                        "Bitmap de Desktop invalido; usando fallback");
        cache->status = result;
        return cache->status;
    }
    if (cache->image.width != ICONS_DESKTOP_BMP_SIZE ||
        cache->image.height != ICONS_DESKTOP_BMP_SIZE ||
        cache->image.bpp != 24) {
        icons_warn_once(ICONS_WARNING_FORMAT,
                        "Bitmap de Desktop fora do formato esperado");
        bmp_free(&cache->image);
        cache->status = ERR_INVALID;
        return cache->status;
    }

    cache->status = OK;
    LOG_INFO("ICONS", "Bitmap de Desktop carregado no cache");
    return OK;
}

void icons_init(void) {
    LOG_INFO("ICONS", "Inicializando registro e cache de icones");
    icons_set_defaults();
    icons_release_desktop_bmps();

    if (fs_get_type() == FS_TYPE_NONE) {
        icons_warn_once(ICONS_WARNING_FILESYSTEM,
                        "Filesystem indisponivel; usando icones vetoriais");
        LOG_INFO("ICONS", "Registro de icones inicializado com fallback");
        return;
    }
    for (int i = 0; i < ICON_DESKTOP_COUNT; i++) {
        icons_load_desktop_bmp((icon_desktop_id_t)i);
    }
    LOG_INFO("ICONS", "Registro e cache de icones inicializados");
}

icon_registry_t* icons_get_registry(void) {
    return &registry;
}

icon_entry_t* icons_get_desktop(icon_desktop_id_t id) {
    if (id >= ICON_DESKTOP_COUNT) return 0;
    return &registry.desktop[id];
}

icon_entry_t* icons_get_wm(icon_wm_id_t id) {
    if (id >= ICON_WM_COUNT) return 0;
    return &registry.wm[id];
}

icon_entry_t* icons_get_fm(icon_fm_id_t id) {
    if (id >= ICON_FM_COUNT) return 0;
    return &registry.fm[id];
}

icon_entry_t* icons_get_tb(icon_tb_id_t id) {
    if (id >= ICON_TB_COUNT) return 0;
    return &registry.tb[id];
}

int icons_get_desktop_bitmap_status(icon_desktop_id_t id) {
    if (id >= ICON_DESKTOP_COUNT) {
        LOG_ERROR("ICONS", "Consulta de bitmap de Desktop invalida");
        return ERR_INVALID;
    }
    return desktop_bmps[id].status;
}

int icons_draw_desktop_bitmap(icon_desktop_id_t id, int x, int y) {
    return icons_draw_desktop_bitmap_resized(id, x, y,
                                             ICONS_DESKTOP_BMP_SIZE);
}

int icons_draw_desktop_bitmap_resized(icon_desktop_id_t id, int x, int y,
                                      uint32_t size) {
    vesa_color_t transparent_color;
    vesa_mode_t* mode;

    if (id < ICON_DESKTOP_SHELL || id >= ICON_DESKTOP_COUNT ||
        x < 0 || y < 0 || !size) {
        LOG_ERROR("ICONS", "Destino de bitmap de Desktop invalido");
        return ERR_INVALID;
    }
    if (desktop_bmps[id].status != OK) return desktop_bmps[id].status;

    mode = vesa_get_mode();
    if (!mode || !mode->initialized ||
        mode->width < size || mode->height < size ||
        x > (int)(mode->width - size) ||
        y > (int)(mode->height - size)) {
        LOG_ERROR("ICONS", "Bitmap de Desktop excede os limites VESA");
        return ERR_INVALID;
    }

    transparent_color.raw = vesa_rgb(255, 0, 255);
    return bmp_draw_transparent_resized(&desktop_bmps[id].image, x, y, size,
                                        size, transparent_color);
}

void icons_set_desktop(icon_desktop_id_t id, char ch, uint8_t color, uint8_t color_sel) {
    if (id >= ICON_DESKTOP_COUNT) return;
    registry.desktop[id].ch = ch;
    registry.desktop[id].color = color;
    registry.desktop[id].color_selected = color_sel;
}

void icons_set_wm(icon_wm_id_t id, char ch, uint8_t color, uint8_t color_sel) {
    if (id >= ICON_WM_COUNT) return;
    registry.wm[id].ch = ch;
    registry.wm[id].color = color;
    registry.wm[id].color_selected = color_sel;
}

void icons_set_fm(icon_fm_id_t id, char ch, uint8_t color, uint8_t color_sel) {
    if (id >= ICON_FM_COUNT) return;
    registry.fm[id].ch = ch;
    registry.fm[id].color = color;
    registry.fm[id].color_selected = color_sel;
}

void icons_set_tb(icon_tb_id_t id, char ch, uint8_t color, uint8_t color_sel) {
    if (id >= ICON_TB_COUNT) return;
    registry.tb[id].ch = ch;
    registry.tb[id].color = color;
    registry.tb[id].color_selected = color_sel;
}

void icons_reset_defaults(void) {
    icons_init();
}
