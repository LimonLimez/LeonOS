#ifndef LEONOS_USER_H
#define LEONOS_USER_H

typedef unsigned int leonos_u32;

enum leonos_syscall {
    LEONOS_SYS_EXIT = 0,
    LEONOS_SYS_WRITE = 1,
    LEONOS_SYS_FB_INFO = 2,
    LEONOS_SYS_FB_FILL = 3,
    LEONOS_SYS_FB_PRESENT = 4,
    LEONOS_SYS_EVENT_POLL = 5,
    LEONOS_SYS_BROWSER_OPEN = 6,
    LEONOS_SYS_YIELD = 7,
    LEONOS_SYS_MILLIS = 8,
    LEONOS_SYS_MALLOC = 9,
    LEONOS_SYS_FREE = 10,
    LEONOS_SYS_NET_FETCH = 11,
    LEONOS_SYS_NET_FETCH_META = 12,
    LEONOS_SYS_NET_STREAM_OPEN = 13,
    LEONOS_SYS_NET_STREAM_POLL = 14,
    LEONOS_SYS_NET_STREAM_READ = 15,
    LEONOS_SYS_NET_STREAM_META = 16,
    LEONOS_SYS_NET_STREAM_CLOSE = 17,
    LEONOS_SYS_FB_TEXT = 18,
    LEONOS_SYS_FB_BLIT = 19
};

enum leonos_net_stream_state {
    LEONOS_NET_STREAM_OPEN = 0x00000001u,
    LEONOS_NET_STREAM_ACTIVE = 0x00000002u,
    LEONOS_NET_STREAM_DONE = 0x00000004u,
    LEONOS_NET_STREAM_OK = 0x00000008u,
    LEONOS_NET_STREAM_ERROR = 0x00000010u,
    LEONOS_NET_STREAM_HAS_DATA = 0x00000020u
};

enum leonos_net_fetch_flags {
    LEONOS_NET_FETCH_FLAG_HEADERS = 0x00000001u,
    LEONOS_NET_FETCH_FLAG_CHUNKED = 0x00000002u,
    LEONOS_NET_FETCH_FLAG_GZIP = 0x00000004u,
    LEONOS_NET_FETCH_FLAG_TRUNCATED = 0x00000008u
};

enum leonos_event_type {
    LEONOS_EVENT_NONE = 0,
    LEONOS_EVENT_TIMER = 1,
    LEONOS_EVENT_KEYBOARD = 2,
    LEONOS_EVENT_MOUSE = 3,
    LEONOS_EVENT_MOUSE_BUTTON = 4
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

struct leonos_fb_blit {
    leonos_u32 dst_x;
    leonos_u32 dst_y;
    leonos_u32 width;
    leonos_u32 height;
    leonos_u32 src_width;
    leonos_u32 src_height;
    leonos_u32 src_stride;
    const void *pixels;
    leonos_u32 flags;
    leonos_u32 src_x;
    leonos_u32 src_y;
    leonos_u32 src_clip_width;
    leonos_u32 src_clip_height;
};

struct leonos_net_fetch_meta {
    leonos_u32 body_len;
    leonos_u32 status_code;
    leonos_u32 flags;
    char content_type[64];
    char location[512];
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

static inline leonos_u32 leonos_syscall2(leonos_u32 number,
                                         leonos_u32 arg0,
                                         leonos_u32 arg1)
{
    leonos_u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "0"(number), "b"(arg0), "c"(arg1)
                      : "memory");
    return ret;
}

static inline leonos_u32 leonos_syscall3(leonos_u32 number,
                                         leonos_u32 arg0,
                                         leonos_u32 arg1,
                                         leonos_u32 arg2)
{
    leonos_u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "0"(number), "b"(arg0), "c"(arg1), "d"(arg2)
                      : "memory");
    return ret;
}

static inline leonos_u32 leonos_syscall4(leonos_u32 number,
                                         leonos_u32 arg0,
                                         leonos_u32 arg1,
                                         leonos_u32 arg2,
                                         leonos_u32 arg3)
{
    leonos_u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "0"(number), "b"(arg0), "c"(arg1), "d"(arg2),
                        "S"(arg3)
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

static inline leonos_u32 leonos_fb_text(leonos_u32 x,
                                        leonos_u32 y,
                                        const char *text,
                                        leonos_u32 color)
{
    return leonos_syscall4(LEONOS_SYS_FB_TEXT, x, y,
                           (leonos_u32) text, color);
}

static inline leonos_u32 leonos_fb_blit(const struct leonos_fb_blit *blit)
{
    return leonos_syscall1(LEONOS_SYS_FB_BLIT, (leonos_u32) blit);
}

static inline leonos_u32 leonos_event_poll(struct leonos_event *event)
{
    return leonos_syscall1(LEONOS_SYS_EVENT_POLL, (leonos_u32) event);
}

static inline leonos_u32 leonos_browser_open(const char *url)
{
    return leonos_syscall1(LEONOS_SYS_BROWSER_OPEN, (leonos_u32) url);
}

static inline leonos_u32 leonos_yield(void)
{
    return leonos_syscall0(LEONOS_SYS_YIELD);
}

static inline leonos_u32 leonos_millis(void)
{
    return leonos_syscall0(LEONOS_SYS_MILLIS);
}

static inline void *leonos_malloc(leonos_u32 size)
{
    leonos_u32 ptr = leonos_syscall1(LEONOS_SYS_MALLOC, size);
    return ptr != 0u ? (void *) ptr : 0;
}

static inline void leonos_free(void *ptr)
{
    (void) leonos_syscall1(LEONOS_SYS_FREE, (leonos_u32) ptr);
}

static inline leonos_u32 leonos_net_fetch(const char *url, void *buffer,
                                            leonos_u32 max_len)
{
    return leonos_syscall3(LEONOS_SYS_NET_FETCH, (leonos_u32) url,
                           (leonos_u32) buffer, max_len);
}

static inline leonos_u32 leonos_net_fetch_meta(
        struct leonos_net_fetch_meta *meta)
{
    return leonos_syscall1(LEONOS_SYS_NET_FETCH_META, (leonos_u32) meta);
}

static inline leonos_u32 leonos_net_stream_open(const char *url)
{
    return leonos_syscall1(LEONOS_SYS_NET_STREAM_OPEN, (leonos_u32) url);
}

static inline leonos_u32 leonos_net_stream_poll(leonos_u32 handle)
{
    return leonos_syscall1(LEONOS_SYS_NET_STREAM_POLL, handle);
}

static inline leonos_u32 leonos_net_stream_read(leonos_u32 handle,
                                                void *buffer,
                                                leonos_u32 max_len)
{
    return leonos_syscall3(LEONOS_SYS_NET_STREAM_READ, handle,
                           (leonos_u32) buffer, max_len);
}

static inline leonos_u32 leonos_net_stream_meta(
        leonos_u32 handle, struct leonos_net_fetch_meta *meta)
{
    return leonos_syscall2(LEONOS_SYS_NET_STREAM_META, handle,
                           (leonos_u32) meta);
}

static inline leonos_u32 leonos_net_stream_close(leonos_u32 handle)
{
    return leonos_syscall1(LEONOS_SYS_NET_STREAM_CLOSE, handle);
}

#endif
