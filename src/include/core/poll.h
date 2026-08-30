#ifndef POLL_H
#define POLL_H

#include "types.h"
#include "core/wait.h"

#define POLL_MAX_FDS 32U

#define POLLIN   0x0001U
#define POLLOUT  0x0002U
#define POLLERR  0x0004U
#define POLLHUP  0x0008U
#define POLLNVAL 0x0010U

#define POLL_SUPPORTED_EVENTS \
    (POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL)

#define POLL_TIMEOUT_IMMEDIATE WAIT_TIMEOUT_IMMEDIATE
#define POLL_TIMEOUT_INFINITE WAIT_TIMEOUT_INFINITE

#define SELECT_SET_READ    0x01U
#define SELECT_SET_WRITE   0x02U
#define SELECT_SET_EXCEPT  0x04U
#define SELECT_SET_ALL     (SELECT_SET_READ | SELECT_SET_WRITE | \
                            SELECT_SET_EXCEPT)

typedef struct {
    int32_t fd;
    uint32_t events;
    uint32_t revents;
} pollfd_t;

typedef struct {
    uint32_t bits;
} fd_set_t;

typedef struct {
    uint32_t nfds;
    fd_set_t readfds;
    fd_set_t writefds;
    fd_set_t exceptfds;
    uint32_t set_mask;
    uint32_t timeout_ticks;
    uint32_t ready_count;
} select_request_t;

#define FD_ZERO(set) \
    do { if ((set)) (set)->bits = 0U; } while (0)

#define FD_SET(fd, set) \
    do { \
        if ((set) && (fd) >= 0 && (uint32_t)(fd) < POLL_MAX_FDS) { \
            (set)->bits |= (1U << (uint32_t)(fd)); \
        } \
    } while (0)

#define FD_CLR(fd, set) \
    do { \
        if ((set) && (fd) >= 0 && (uint32_t)(fd) < POLL_MAX_FDS) { \
            (set)->bits &= ~(1U << (uint32_t)(fd)); \
        } \
    } while (0)

#define FD_ISSET(fd, set) \
    ((set) && (fd) >= 0 && (uint32_t)(fd) < POLL_MAX_FDS && \
     (((set)->bits & (1U << (uint32_t)(fd))) != 0U))

#endif
