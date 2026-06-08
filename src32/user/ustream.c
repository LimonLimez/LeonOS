#include "leonos_user.h"

static void write_u32(leonos_u32 value)
{
    char buf[12];
    leonos_u32 i = 0u;
    if (value == 0u) {
        leonos_write("0");
        return;
    }
    while (value != 0u && i < sizeof(buf)) {
        buf[i++] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
    while (i != 0u) {
        char ch[2] = { buf[--i], 0 };
        leonos_write(ch);
    }
}

static void write_text_prefix(const char *data, leonos_u32 len)
{
    char line[97];
    leonos_u32 out = 0u;
    leonos_u32 in_tag = 0u;
    for (leonos_u32 i = 0u; i < len && out + 1u < sizeof(line); i += 1u) {
        char ch = data[i];
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
    leonos_write("USTREAM preview ");
    leonos_write(line);
    leonos_write("\r\n");
}

int leonos_user_main(void)
{
    static const char url[] = "https://www.google.com/";
    static char chunk[1024];
    struct leonos_net_fetch_meta meta;
    leonos_u32 handle;
    leonos_u32 state = 0u;
    leonos_u32 total = 0u;
    leonos_u32 chunks = 0u;
    leonos_u32 previewed = 0u;

    leonos_write("USTREAM user HTTPS stream\r\n");
    handle = leonos_net_stream_open(url);
    if (handle == 0u) {
        leonos_write("USTREAM open failed\r\n");
        return 1;
    }
    leonos_write("USTREAM open handle ");
    write_u32(handle);
    leonos_write("\r\n");

    for (leonos_u32 polls = 0u; polls < 20000u; polls += 1u) {
        state = leonos_net_stream_poll(handle);
        if ((state & LEONOS_NET_STREAM_HAS_DATA) != 0u) {
            leonos_u32 got;
            do {
                got = leonos_net_stream_read(handle, chunk, sizeof(chunk));
                if (got != 0u) {
                    total += got;
                    chunks += 1u;
                    if (chunks <= 4u || (chunks & 15u) == 0u) {
                        leonos_write("USTREAM chunk ");
                        write_u32(chunks);
                        leonos_write(" bytes ");
                        write_u32(got);
                        leonos_write(" total ");
                        write_u32(total);
                        leonos_write("\r\n");
                    }
                    if (!previewed) {
                        write_text_prefix(chunk, got);
                        previewed = 1u;
                    }
                }
            } while (got != 0u);
        }
        if ((state & LEONOS_NET_STREAM_DONE) != 0u) {
            break;
        }
        leonos_yield();
    }

    leonos_write("USTREAM final state ");
    write_u32(state);
    leonos_write("\r\n");
    if (!leonos_net_stream_meta(handle, &meta)) {
        leonos_write("USTREAM meta failed\r\n");
        (void) leonos_net_stream_close(handle);
        return 1;
    }
    leonos_write("USTREAM meta status ");
    write_u32(meta.status_code);
    leonos_write(" len ");
    write_u32(meta.body_len);
    leonos_write(" flags ");
    write_u32(meta.flags);
    leonos_write(" type ");
    leonos_write(meta.content_type[0] != 0 ? meta.content_type : "unknown");
    leonos_write("\r\n");
    leonos_write("USTREAM total ");
    write_u32(total);
    leonos_write(" chunks ");
    write_u32(chunks);
    leonos_write("\r\n");
    (void) leonos_net_stream_close(handle);

    if ((state & LEONOS_NET_STREAM_OK) == 0u ||
        meta.status_code != 200u ||
        total == 0u) {
        return 1;
    }
    return 0;
}
