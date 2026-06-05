#ifndef LEONOS_USER_H
#define LEONOS_USER_H

typedef unsigned int leonos_u32;

enum leonos_syscall {
    LEONOS_SYS_EXIT = 0,
    LEONOS_SYS_WRITE = 1,
    LEONOS_SYS_FB_INFO = 2,
    LEONOS_SYS_FB_FILL = 3,
    LEONOS_SYS_FB_PRESENT = 4,
    LEONOS_SYS_EVENT_POLL = 5
};

struct leonos_fb_info {
    leonos_u32 width;
    leonos_u32 height;
    leonos_u32 pitch;
    leonos_u32 bpp;
};

struct leonos_event {
    leonos_u32 type;
    leonos_u32 data0;
    leonos_u32 data1;
};

static inline leonos_u32 leonos_syscall0(leonos_u32 number)
{
    leonos_u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "0"(number)
                      : "memory");
    return ret;
}

static inline leonos_u32 leonos_syscall1(leonos_u32 number, leonos_u32 arg0)
{
    leonos_u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "0"(number), "b"(arg0)
                      : "memory");
    return ret;
}

static inline leonos_u32 leonos_syscall5(leonos_u32 number,
                                         leonos_u32 arg0,
                                         leonos_u32 arg1,
                                         leonos_u32 arg2,
                                         leonos_u32 arg3,
                                         leonos_u32 arg4)
{
    leonos_u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "0"(number), "b"(arg0), "c"(arg1), "d"(arg2),
                        "S"(arg3), "D"(arg4)
                      : "memory");
    return ret;
}

static inline void leonos_exit(void)
{
    (void) leonos_syscall0(LEONOS_SYS_EXIT);
    for (;;) {
    }
}

static inline leonos_u32 leonos_write(const char *text)
{
    return leonos_syscall1(LEONOS_SYS_WRITE, (leonos_u32) text);
}

static inline leonos_u32 leonos_fb_info(struct leonos_fb_info *info)
{
    return leonos_syscall1(LEONOS_SYS_FB_INFO, (leonos_u32) info);
}

static inline leonos_u32 leonos_fb_fill(leonos_u32 x,
                                        leonos_u32 y,
                                        leonos_u32 w,
                                        leonos_u32 h,
                                        leonos_u32 color)
{
    return leonos_syscall5(LEONOS_SYS_FB_FILL, x, y, w, h, color);
}

static inline leonos_u32 leonos_fb_present(void)
{
    return leonos_syscall0(LEONOS_SYS_FB_PRESENT);
}

static inline leonos_u32 leonos_event_poll(struct leonos_event *event)
{
    return leonos_syscall1(LEONOS_SYS_EVENT_POLL, (leonos_u32) event);
}

#endif
