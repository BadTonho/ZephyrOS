#include "core/log.h"
#include "core/video.h"

#define LOG_BUFFER_SIZE 4096
#define LOG_MAX_MSG 256

static log_level_t current_level = LOG_LEVEL_INFO;
static char log_buffer[LOG_BUFFER_SIZE];
static int log_buffer_pos = 0;
static int log_buffer_count = 0;

static uint8_t level_colors[] = {
    0x4F,
    0x6F,
    0x0F,
    0x07
};

static const char* level_labels[] = {
    "ERR",
    "WRN",
    "INF",
    "DBG"
};

void log_init(void) {
    log_buffer_pos = 0;
    log_buffer_count = 0;
    log_buffer[0] = '\0';
    LOG_INFO("LOG", "Sistema de log inicializado");
}

void log_set_level(log_level_t level) {
    current_level = level;
}

log_level_t log_get_level(void) {
    return current_level;
}

const char* log_level_str(log_level_t level) {
    if (level <= LOG_LEVEL_DEBUG) return level_labels[level];
    return "???";
}

static void log_print_colored(const char* prefix, uint8_t prefix_color, const char* msg) {
    video_print("[", 0x07);
    video_print(prefix, prefix_color);
    video_print("] ", 0x07);
    video_print(msg, 0x07);
    video_newline();
}

void log_print(log_level_t level, const char* module, const char* msg) {
    if (level > current_level) return;

    log_to_buffer(level, module, msg);

    if (level == LOG_LEVEL_ERROR) {
        log_print_colored(level_labels[level], level_colors[level], msg);
    } else if (level == LOG_LEVEL_WARN) {
        log_print_colored(level_labels[level], level_colors[level], msg);
    } else {
        log_print_colored(level_labels[level], level_colors[level], msg);
    }
}

void log_to_buffer(log_level_t level, const char* module, const char* msg) {
    if (log_buffer_pos >= LOG_BUFFER_SIZE - LOG_MAX_MSG - 32) {
        return;
    }

    const char* label = level_labels[level];

    log_buffer[log_buffer_pos++] = '[';
    log_buffer[log_buffer_pos++] = label[0];
    log_buffer[log_buffer_pos++] = label[1];
    log_buffer[log_buffer_pos++] = label[2];
    log_buffer[log_buffer_pos++] = ']';
    log_buffer[log_buffer_pos++] = ' ';
    log_buffer[log_buffer_pos++] = '[';

    while (*module && log_buffer_pos < LOG_BUFFER_SIZE - 2) {
        log_buffer[log_buffer_pos++] = *module++;
    }

    log_buffer[log_buffer_pos++] = ']';
    log_buffer[log_buffer_pos++] = ' ';

    while (*msg && log_buffer_pos < LOG_BUFFER_SIZE - 2) {
        log_buffer[log_buffer_pos++] = *msg++;
    }

    log_buffer[log_buffer_pos++] = '\n';
    log_buffer[log_buffer_pos] = '\0';
    log_buffer_count++;
}

int log_get_buffer(char* out, int max_size) {
    int len = 0;
    while (log_buffer[len] && len < max_size - 1) {
        out[len] = log_buffer[len];
        len++;
    }
    out[len] = '\0';
    return len;
}

void log_clear_buffer(void) {
    log_buffer_pos = 0;
    log_buffer_count = 0;
    log_buffer[0] = '\0';
}
