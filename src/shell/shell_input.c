#include "apps/shell_input.h"
#include "apps/shell.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/video.h"

#define SHELL_INPUT_HISTORY_CAPACITY 16U
#define SHELL_INPUT_SCANCODE_EXTENDED 0xE0U
#define SHELL_INPUT_SCANCODE_LEFT_SHIFT 0x2AU
#define SHELL_INPUT_SCANCODE_RIGHT_SHIFT 0x36U
#define SHELL_INPUT_SCANCODE_LEFT_SHIFT_RELEASE 0xAAU
#define SHELL_INPUT_SCANCODE_RIGHT_SHIFT_RELEASE 0xB6U
#define SHELL_INPUT_SHIFT_LEFT_MASK 0x01U
#define SHELL_INPUT_SHIFT_RIGHT_MASK 0x02U
#define SHELL_INPUT_SCANCODE_BACKSPACE 0x0EU
#define SHELL_INPUT_SCANCODE_ENTER 0x1CU
#define SHELL_INPUT_SCANCODE_UP 0x48U
#define SHELL_INPUT_SCANCODE_DOWN 0x50U
#define SHELL_INPUT_SCANCODE_PAGE_UP 0x49U
#define SHELL_INPUT_SCANCODE_PAGE_DOWN 0x51U
#define SHELL_INPUT_SCANCODE_HOME 0x47U
#define SHELL_INPUT_SCANCODE_END 0x4FU
#define SHELL_INPUT_SCROLL_PAGE_LINES 20
#define SHELL_INPUT_COLOR_PROMPT 0x0AU
#define SHELL_INPUT_COLOR_TEXT 0x07U

static char input_buffer[SHELL_BUFFER_SIZE];
static char shell_command_history[SHELL_INPUT_HISTORY_CAPACITY]
                                 [SHELL_BUFFER_SIZE];
static char shell_command_draft[SHELL_BUFFER_SIZE];
static int input_pos = 0;
static uint32_t shell_history_count = 0;
static uint32_t shell_history_next = 0;
static uint32_t shell_history_depth = 0;
static uint8_t shell_extended_scancode = 0;
static uint8_t shell_shift_mask = 0;
static uint8_t shell_prompt_visible = 0;
static uint8_t shell_input_overflow_warned = 0;

static void shell_input_history_reset_navigation(void) {
    shell_history_depth = 0;
    shell_command_draft[0] = '\0';
}

static void shell_input_history_copy(char* destination, const char* source) {
    uint32_t index = 0;

    if (!destination) return;
    if (!source) source = "";
    while (source[index] && index + 1U < SHELL_BUFFER_SIZE) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void shell_input_history_record(const char* command) {
    uint32_t previous;

    if (!command || !command[0]) {
        shell_input_history_reset_navigation();
        return;
    }
    if (shell_history_count) {
        previous = (shell_history_next + SHELL_INPUT_HISTORY_CAPACITY - 1U) %
                   SHELL_INPUT_HISTORY_CAPACITY;
        if (kstrcmp(shell_command_history[previous], command) == 0) {
            shell_input_history_reset_navigation();
            return;
        }
    }
    shell_input_history_copy(shell_command_history[shell_history_next], command);
    shell_history_next =
        (shell_history_next + 1U) % SHELL_INPUT_HISTORY_CAPACITY;
    if (shell_history_count < SHELL_INPUT_HISTORY_CAPACITY) {
        shell_history_count++;
    }
    shell_input_history_reset_navigation();
}

static void shell_input_return_to_terminal_tail(uint8_t window_manager_active) {
    shell_input_resume_terminal(window_manager_active);
    if (video_terminal_is_scrolled()) video_terminal_scroll_end();
}

static void shell_input_history_replace_input(const char* command,
                                              uint8_t window_manager_active) {
    uint32_t index = 0;

    shell_input_return_to_terminal_tail(window_manager_active);
    while (input_pos > 0) {
        video_backspace();
        input_pos--;
    }
    kmemset(input_buffer, 0, sizeof(input_buffer));
    shell_input_overflow_warned = 0;
    if (!command) return;
    while (command[index] && index + 1U < SHELL_BUFFER_SIZE) {
        input_buffer[index] = command[index];
        video_put_char(command[index], SHELL_INPUT_COLOR_TEXT);
        index++;
    }
    input_buffer[index] = '\0';
    input_pos = (int)index;
}

static void shell_input_history_navigate(int direction,
                                         uint8_t window_manager_active) {
    uint32_t history_index;

    if (direction < 0) {
        if (!shell_history_count ||
            shell_history_depth >= shell_history_count) return;
        if (!shell_history_depth) {
            shell_input_history_copy(shell_command_draft, input_buffer);
        }
        shell_history_depth++;
    } else {
        if (!shell_history_depth) return;
        shell_history_depth--;
        if (!shell_history_depth) {
            shell_input_history_replace_input(shell_command_draft,
                                              window_manager_active);
            return;
        }
    }
    history_index =
        (shell_history_next + SHELL_INPUT_HISTORY_CAPACITY -
         shell_history_depth) % SHELL_INPUT_HISTORY_CAPACITY;
    shell_input_history_replace_input(shell_command_history[history_index],
                                      window_manager_active);
}

static void shell_input_history_detach_for_edit(void) {
    if (!shell_history_depth) return;
    shell_input_history_reset_navigation();
}

static int shell_input_handle_terminal_scroll_key(uint8_t scancode) {
    switch (scancode) {
        case SHELL_INPUT_SCANCODE_UP:
            return video_terminal_scroll(1);
        case SHELL_INPUT_SCANCODE_DOWN:
            return video_terminal_scroll(-1);
        case SHELL_INPUT_SCANCODE_PAGE_UP:
            return video_terminal_scroll(SHELL_INPUT_SCROLL_PAGE_LINES);
        case SHELL_INPUT_SCANCODE_PAGE_DOWN:
            return video_terminal_scroll(-SHELL_INPUT_SCROLL_PAGE_LINES);
        case SHELL_INPUT_SCANCODE_HOME:
            video_terminal_scroll_home();
            return 1;
        case SHELL_INPUT_SCANCODE_END:
            video_terminal_scroll_end();
            return 1;
        default:
            return 0;
    }
}

void shell_input_resume_terminal(uint8_t window_manager_active) {
    if (!window_manager_active && video_terminal_is_hosted()) {
        video_terminal_begin();
        return;
    }
    if (!video_terminal_is_active()) video_terminal_begin();
}

void shell_input_init(void) {
    LOG_INFO("SHELL", "Inicializando entrada de linha");
    kmemset(shell_command_history, 0, sizeof(shell_command_history));
    shell_history_count = 0;
    shell_history_next = 0;
    shell_prompt_visible = 0;
    shell_input_reset();
    LOG_INFO("SHELL", "Entrada de linha inicializada com sucesso");
}

void shell_input_reset(void) {
    input_pos = 0;
    shell_input_reset_modifiers();
    shell_input_overflow_warned = 0;
    shell_input_history_reset_navigation();
    kmemset(input_buffer, 0, sizeof(input_buffer));
}

void shell_input_cancel_extended(void) {
    shell_extended_scancode = 0;
}

void shell_input_reset_modifiers(void) {
    shell_extended_scancode = 0;
    shell_shift_mask = 0;
}

const char* shell_input_get_buffer(void) {
    return input_buffer;
}

void shell_input_print_prompt(uint8_t window_manager_active) {
    shell_input_resume_terminal(window_manager_active);
    if (shell_prompt_visible) return;
    video_print(SHELL_PROMPT, SHELL_INPUT_COLOR_PROMPT);
    shell_prompt_visible = 1;
}

shell_input_event_t shell_input_handle_key(uint8_t scancode,
                                           uint8_t window_manager_active,
                                           uint8_t input_blocked) {
    char character;

    if (scancode == SHELL_INPUT_SCANCODE_LEFT_SHIFT) {
        shell_shift_mask |= SHELL_INPUT_SHIFT_LEFT_MASK;
        return SHELL_INPUT_EVENT_NONE;
    }
    if (scancode == SHELL_INPUT_SCANCODE_RIGHT_SHIFT) {
        shell_shift_mask |= SHELL_INPUT_SHIFT_RIGHT_MASK;
        return SHELL_INPUT_EVENT_NONE;
    }
    if (scancode == SHELL_INPUT_SCANCODE_LEFT_SHIFT_RELEASE) {
        shell_shift_mask &= (uint8_t)~SHELL_INPUT_SHIFT_LEFT_MASK;
        return SHELL_INPUT_EVENT_NONE;
    }
    if (scancode == SHELL_INPUT_SCANCODE_RIGHT_SHIFT_RELEASE) {
        shell_shift_mask &= (uint8_t)~SHELL_INPUT_SHIFT_RIGHT_MASK;
        return SHELL_INPUT_EVENT_NONE;
    }
    if (input_blocked) return SHELL_INPUT_EVENT_NONE;

    shell_input_resume_terminal(window_manager_active);

    if (scancode == SHELL_INPUT_SCANCODE_EXTENDED) {
        shell_extended_scancode = 1;
        return SHELL_INPUT_EVENT_NONE;
    }

    if (scancode & 0x80U) {
        shell_extended_scancode = 0;
        return SHELL_INPUT_EVENT_NONE;
    }

    if (shell_extended_scancode) {
        shell_extended_scancode = 0;
        if (scancode == SHELL_INPUT_SCANCODE_UP ||
            scancode == SHELL_INPUT_SCANCODE_DOWN) {
            if (shell_shift_mask) {
                (void)shell_input_handle_terminal_scroll_key(scancode);
            } else {
                shell_input_history_navigate(
                    scancode == SHELL_INPUT_SCANCODE_UP ? -1 : 1,
                    window_manager_active);
            }
            return SHELL_INPUT_EVENT_NONE;
        }
        if (shell_input_handle_terminal_scroll_key(scancode)) {
            return SHELL_INPUT_EVENT_NONE;
        }
    }

    character = keyboard_scancode_to_ascii_shifted(
        scancode, shell_shift_mask ? 1U : 0U);

    if (scancode == SHELL_INPUT_SCANCODE_BACKSPACE) {
        shell_input_history_detach_for_edit();
        shell_input_return_to_terminal_tail(window_manager_active);
        if (input_pos > 0) {
            input_pos--;
            input_buffer[input_pos] = '\0';
            video_backspace();
        }
        return SHELL_INPUT_EVENT_NONE;
    }

    if (scancode == SHELL_INPUT_SCANCODE_ENTER) {
        shell_input_return_to_terminal_tail(window_manager_active);
        shell_prompt_visible = 0;
        video_print("\n", SHELL_INPUT_COLOR_TEXT);
        shell_input_history_record(input_buffer);
        return SHELL_INPUT_EVENT_COMMAND_READY;
    }

    if (character >= ' ' && character <= '~') {
        if (input_pos >= SHELL_BUFFER_SIZE - 1) {
            if (!shell_input_overflow_warned) {
                LOG_WARN("SHELL", "Buffer de entrada cheio; caractere ignorado");
                shell_input_overflow_warned = 1;
            }
            return SHELL_INPUT_EVENT_NONE;
        }
        shell_input_history_detach_for_edit();
        shell_input_return_to_terminal_tail(window_manager_active);
        input_buffer[input_pos++] = character;
        input_buffer[input_pos] = '\0';
        video_put_char(character, SHELL_INPUT_COLOR_TEXT);
    }

    return SHELL_INPUT_EVENT_NONE;
}
