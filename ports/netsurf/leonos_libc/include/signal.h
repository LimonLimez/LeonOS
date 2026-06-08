#ifndef LEONOS_LIBC_SIGNAL_H
#define LEONOS_LIBC_SIGNAL_H

#define SIG_ERR ((void (*)(int)) -1)
#define SIG_DFL ((void (*)(int)) 0)
#define SIG_IGN ((void (*)(int)) 1)

#define SIGSEGV 11
#define SIGILL 4
#define SIGFPE 8
#define SIGBUS 7
#define SIGPIPE 13

typedef void (*sighandler_t)(int);

sighandler_t signal(int signum, sighandler_t handler);

#endif
