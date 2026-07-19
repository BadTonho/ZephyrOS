#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "types.h"

#define FM_MAX_FILES 64
#define FM_NAME_LEN 13
#define FM_MAX_PATH 256
#define FM_MAX_HISTORY 16
#define FM_TITLE_BAR_COLOR 0x1F
#define FM_BORDER_COLOR 0x07
#define FM_FILE_COLOR 0x07
#define FM_DIR_COLOR 0x0B
#define FM_SELECTED_COLOR 0x70
#define FM_STATUS_COLOR 0x70
#define FM_HEADER_COLOR 0x0F
#define FM_SIZE_COLOR 0x08
#define FM_ADDRESS_COLOR 0x07
#define FM_ADDRESS_INPUT_COLOR 0x1F

typedef struct {
    char name[FM_NAME_LEN];
    uint32_t size;
    uint8_t is_dir;
    uint8_t attributes;
} fm_file_entry_t;

typedef struct {
    fm_file_entry_t files[FM_MAX_FILES];
    int file_count;
    int selected;
    int scroll_offset;
    int view_mode;
    int running;
    char current_path[FM_MAX_PATH];
    char history[FM_MAX_HISTORY][FM_MAX_PATH];
    int history_count;
    int history_pos;
    int address_mode;
    char address_buffer[FM_MAX_PATH];
    int address_pos;
} fm_state_t;

void fm_init(void);
void fm_run(void);
void fm_handle_key(uint8_t scancode);

#endif
