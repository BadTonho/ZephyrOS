#include "ui/icons.h"

static icon_registry_t registry;

void icons_init(void) {
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
