#include "leonos_user.h"

static void write_u32(leonos_u32 value)
{
    char buf[12];
    leonos_u32 i = 0;
    if (value == 0u) {
        leonos_write("0");
        return;
    }
    while (value != 0u && i < sizeof(buf)) {
        buf[i++] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
    while (i != 0u) {
        char c = buf[--i];
        char line[2] = { c, 0 };
        leonos_write(line);
    }
}

int leonos_user_main(void)
{
    struct leonos_fb_info fb;
    void *block;
    leonos_u32 start;
    leonos_u32 ticks = 0;

    leonos_write("UNETRUN NetSurf port runtime\r\n");
    block = leonos_malloc(4096u);
    if (!block) {
        leonos_write("malloc failed\r\n");
        return 1;
    }
    leonos_free(block);

    if (!leonos_fb_info(&fb)) {
        return 1;
    }

    start = leonos_millis();
    while (ticks < 40u) {
        leonos_u32 x = 40u + (ticks * 17u) % (fb.width - 80u);
        leonos_u32 y = 80u + (ticks * 11u) % (fb.height - 160u);
        leonos_fb_fill(x, y, 48u, 24u, 0x001A73E8u);
        leonos_yield();
        ticks += 1u;
    }
    leonos_fb_present();
    leonos_write("UNETRUN runtime ticks ");
    write_u32(ticks);
    leonos_write(" elapsed ");
    write_u32(leonos_millis() - start);
    leonos_write("ms\r\n");
    leonos_write("UNETRUN opening LeonOS browser\r\n");
    if (!leonos_browser_open("https://www.example.com/")) {
        return 1;
    }
    leonos_exit();
    return 0;
}
