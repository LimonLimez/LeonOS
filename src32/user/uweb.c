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
        char line[2] = { buf[--i], 0 };
        leonos_write(line);
    }
}

static void write_snippet(const char *html, leonos_u32 len)
{
    leonos_u32 out = 0;
    leonos_u32 i = 0;
    leonos_u32 in_tag = 0;
    char line[96];
    while (i < len && out + 1u < sizeof(line)) {
        char ch = html[i++];
        if (ch == '<') {
            in_tag = 1u;
            continue;
        }
        if (ch == '>') {
            in_tag = 0u;
            continue;
        }
        if (in_tag) {
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
        if (ch < 32 || ch > 126) {
            continue;
        }
        if (out != 0u && ch == ' ' && line[out - 1u] == ' ') {
            continue;
        }
        line[out++] = ch;
    }
    line[out] = 0;
    leonos_write("preview ");
    leonos_write(line);
    leonos_write("\r\n");
}

int leonos_user_main(void)
{
    static const char url[] = "https://www.example.com/";
    char *body;
    leonos_u32 nbytes;
    struct leonos_fb_info fb;

    leonos_write("UWEB user HTTPS fetch + browser\r\n");
    body = (char *) leonos_malloc(16384u);
    if (!body) {
        leonos_write("malloc failed\r\n");
        return 1;
    }

    if (leonos_fb_info(&fb)) {
        leonos_fb_fill(24u, 24u, fb.width - 48u, 48u, 0x001A73E8u);
        leonos_fb_present();
    }

    nbytes = leonos_net_fetch(url, body, 16384u);
    leonos_write("fetched ");
    write_u32(nbytes);
    leonos_write(" bytes\r\n");
    if (nbytes == 0u) {
        leonos_free(body);
        return 1;
    }

    write_snippet(body, nbytes);
    leonos_free(body);

    if (!leonos_browser_open(url)) {
        leonos_write("browser open failed\r\n");
        return 1;
    }
    leonos_exit();
    return 0;
}
