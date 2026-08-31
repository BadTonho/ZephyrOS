#ifndef VIDEO_TEST_H
#define VIDEO_TEST_H

#include "core/errors.h"
#include "types.h"

#define VIDEO_TEST_TEXT_CAPACITY 16384U

typedef struct {
    uint32_t generation;
    uint32_t line_count;
    uint32_t cursor_x;
    uint8_t active;
    uint8_t hosted;
    uint8_t truncated;
} video_test_terminal_info_t;

int video_test_copy_terminal(char* output, uint32_t capacity,
                             video_test_terminal_info_t* info);

#endif
