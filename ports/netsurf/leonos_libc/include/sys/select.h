#ifndef LEONOS_LIBC_SYS_SELECT_H
#define LEONOS_LIBC_SYS_SELECT_H

#include <sys/time.h>

#define FD_SETSIZE 1024

typedef struct {
    unsigned int bits[(FD_SETSIZE + 31) / 32];
} fd_set;

static inline void FD_ZERO(fd_set *set)
{
    for (unsigned int i = 0; i < (FD_SETSIZE + 31) / 32; i += 1u) {
        set->bits[i] = 0u;
    }
}

static inline void FD_SET(int fd, fd_set *set)
{
    if (fd >= 0 && fd < FD_SETSIZE) {
        set->bits[(unsigned int) fd / 32u] |= (1u << ((unsigned int) fd % 32u));
    }
}

static inline int FD_ISSET(int fd, fd_set *set)
{
    if (fd < 0 || fd >= FD_SETSIZE) {
        return 0;
    }
    return (set->bits[(unsigned int) fd / 32u] & (1u << ((unsigned int) fd % 32u))) != 0u;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);

#endif
