struct NetJpegHuff {
    u8 count;
    u8 symbol[256];
    u16 code[256];
    u8 size[256];
};

static const u8 net_jpeg_zigzag[64] = {
    0u, 1u, 8u, 16u, 9u, 2u, 3u, 10u,
    17u, 24u, 32u, 25u, 18u, 11u, 4u, 5u,
    12u, 19u, 26u, 33u, 40u, 48u, 41u, 34u,
    27u, 20u, 13u, 6u, 7u, 14u, 21u, 28u,
    35u, 42u, 49u, 56u, 57u, 50u, 43u, 36u,
    29u, 22u, 15u, 23u, 30u, 37u, 44u, 51u,
    58u, 59u, 52u, 45u, 38u, 31u, 39u, 46u,
    53u, 60u, 61u, 54u, 47u, 55u, 62u, 63u
};

static void net_browser_image_copy_text(char *dst, u32 dst_len, const char *src)
{
    net_append_capped(dst, 0u, dst_len, src);
}

static void net_browser_image_write_render_text(u8 render_slot,
                                                const char *prefix,
                                                const char *value)
{
    if (render_slot >= NET_BROWSER_RENDER_MAX) {
        return;
    }
    u32 out = 0u;
    for (u32 i = 0u; prefix[i] != 0 && out < NET_BROWSER_RENDER_TEXT_MAX - 1u; i += 1u) {
        net_browser_render_text[render_slot][out++] = prefix[i];
    }
    for (u32 i = 0u; value[i] != 0 && out < NET_BROWSER_RENDER_TEXT_MAX - 1u; i += 1u) {
        char ch = value[i];
        if ((u8) ch < 32u || (u8) ch > 126u) {
            ch = ' ';
        }
        net_browser_render_text[render_slot][out++] = ch;
    }
    net_browser_render_text[render_slot][out] = 0;
}

static void net_browser_image_refresh_render_labels(u8 image)
{
    if (image >= NET_BROWSER_IMAGE_MAX) {
        return;
    }
    for (u32 r = 0u; r < NET_BROWSER_RENDER_MAX; r += 1u) {
        if (net_browser_render_image[r] == image) {
            net_browser_image_write_render_text((u8) r, "IMAGE ",
                                                net_browser_image_label[image]);
        }
    }
}

static void net_browser_image_set_label(u8 image, u8 format, u8 status,
                                        const char *label)
{
    if (image >= NET_BROWSER_IMAGE_MAX) {
        return;
    }
    net_browser_image_format[image] = format;
    net_browser_image_status[image] = status;
    net_browser_image_copy_text(net_browser_image_label[image],
                                NET_BROWSER_IMAGE_LABEL_MAX, label);
    net_browser_image_refresh_render_labels(image);
}

static void net_browser_image_append_dec(char *dst, u32 dst_len, u32 *pos, u32 value)
{
    char tmp[12];
    u32 len = 0u;
    if (value == 0u) {
        tmp[len++] = '0';
    } else {
        char rev[12];
        while (value != 0u && len < sizeof(rev)) {
            rev[len++] = (char) ('0' + (value % 10u));
            value /= 10u;
        }
        for (u32 i = 0u; i < len; i += 1u) {
            tmp[i] = rev[len - 1u - i];
        }
    }
    tmp[len] = 0;
    *pos = net_append_capped(dst, *pos, dst_len, tmp);
}

static void net_browser_image_set_decoded(u8 image, u8 format,
                                          u16 width, u16 height)
{
    char label[NET_BROWSER_IMAGE_LABEL_MAX];
    const char *name = format == NET_BROWSER_IMAGE_FORMAT_PNG ? "PNG " : "JPEG ";
    u32 pos = net_append_capped(label, 0u, sizeof(label), name);
    net_browser_image_append_dec(label, sizeof(label), &pos, width);
    pos = net_append_capped(label, pos, sizeof(label), "x");
    net_browser_image_append_dec(label, sizeof(label), &pos, height);
    pos = net_append_capped(label, pos, sizeof(label), " decoded");
    (void) pos;
    net_browser_image_width[image] = width;
    net_browser_image_height[image] = height;
    net_browser_image_set_label(image, format, NET_BROWSER_IMAGE_STATUS_DECODED,
                                label);
    if (net_browser_image_decode_count < 255u) {
        net_browser_image_decode_count += 1u;
    }
    if (format == NET_BROWSER_IMAGE_FORMAT_PNG &&
        net_browser_image_png_count < 255u) {
        net_browser_image_png_count += 1u;
    }
    if (format == NET_BROWSER_IMAGE_FORMAT_JPEG &&
        net_browser_image_jpeg_count < 255u) {
        net_browser_image_jpeg_count += 1u;
    }
    serial_print("LeonOS net browser image ");
    serial_print(name);
    serial_print("decoded slot ");
    serial_print_dec(image);
    serial_print(" ");
    serial_print_dec(width);
    serial_print("x");
    serial_print_dec(height);
    serial_print("\r\n");
}

static void net_browser_image_set_unsupported(u8 image, u8 format,
                                              const char *reason)
{
    char label[NET_BROWSER_IMAGE_LABEL_MAX];
    const char *name = "format";
    if (format == NET_BROWSER_IMAGE_FORMAT_PNG) {
        name = "PNG";
    } else if (format == NET_BROWSER_IMAGE_FORMAT_JPEG) {
        name = "JPEG";
    } else if (format == NET_BROWSER_IMAGE_FORMAT_OTHER) {
        name = "resource";
    }
    u32 pos = net_append_capped(label, 0u, sizeof(label), name);
    pos = net_append_capped(label, pos, sizeof(label), " unsupported: ");
    pos = net_append_capped(label, pos, sizeof(label), reason);
    (void) pos;
    net_browser_image_set_label(image, format, NET_BROWSER_IMAGE_STATUS_UNSUPPORTED,
                                label);
    if (net_browser_image_unsupported_count < 255u) {
        net_browser_image_unsupported_count += 1u;
    }
    serial_print("LeonOS net browser image unsupported slot ");
    serial_print_dec(image);
    serial_print(" ");
    serial_print(label);
    serial_print("\r\n");
}

static void net_browser_image_set_unsupported_size(u8 image, u8 format,
                                                   const char *reason,
                                                   u16 width, u16 height)
{
    char label[NET_BROWSER_IMAGE_LABEL_MAX];
    const char *name = format == NET_BROWSER_IMAGE_FORMAT_PNG ? "PNG " : "JPEG ";
    u32 pos = net_append_capped(label, 0u, sizeof(label), name);
    net_browser_image_append_dec(label, sizeof(label), &pos, width);
    pos = net_append_capped(label, pos, sizeof(label), "x");
    net_browser_image_append_dec(label, sizeof(label), &pos, height);
    pos = net_append_capped(label, pos, sizeof(label), " unsupported: ");
    pos = net_append_capped(label, pos, sizeof(label), reason);
    (void) pos;
    net_browser_image_set_label(image, format, NET_BROWSER_IMAGE_STATUS_UNSUPPORTED,
                                label);
    if (net_browser_image_unsupported_count < 255u) {
        net_browser_image_unsupported_count += 1u;
    }
    serial_print("LeonOS net browser image unsupported slot ");
    serial_print_dec(image);
    serial_print(" ");
    serial_print(label);
    serial_print("\r\n");
}

static void net_browser_image_reset_resource_body(void)
{
    net_browser_resource_body_len = 0u;
    net_browser_resource_body_overflow = 0u;
    net_browser_fetch_image_index = 0xFFu;
}

static void net_browser_image_reset(void)
{
    net_browser_image_slot_count = 0u;
    net_browser_image_decode_count = 0u;
    net_browser_image_unsupported_count = 0u;
    net_browser_image_png_count = 0u;
    net_browser_image_jpeg_count = 0u;
    net_browser_direct_image_active = 0u;
    net_browser_direct_image_slot = 0xFFu;
    net_browser_direct_image_format = NET_BROWSER_IMAGE_FORMAT_UNKNOWN;
    net_browser_image_reset_resource_body();
    for (u32 i = 0u; i < NET_BROWSER_IMAGE_MAX; i += 1u) {
        net_browser_image_status[i] = NET_BROWSER_IMAGE_STATUS_EMPTY;
        net_browser_image_format[i] = NET_BROWSER_IMAGE_FORMAT_UNKNOWN;
        net_browser_image_width[i] = 0u;
        net_browser_image_height[i] = 0u;
        net_browser_image_url[i][0] = 0;
        net_browser_image_label[i][0] = 0;
        for (u32 p = 0u; p < NET_BROWSER_IMAGE_PIXELS_MAX; p += 1u) {
            net_browser_image_pixels[i][p] = 0x00FFFFFFu;
        }
    }
}

static u8 net_browser_image_format_from_url(const char *url)
{
    u32 len = 0u;
    while (len < NET_BROWSER_RESOURCE_URL_MAX - 1u &&
           url[len] != 0 &&
           url[len] != '?' &&
           url[len] != '#') {
        len += 1u;
    }
    if (len >= 4u) {
        char a = (char) net_ascii_lower((u8) url[len - 4u]);
        char b = (char) net_ascii_lower((u8) url[len - 3u]);
        char c = (char) net_ascii_lower((u8) url[len - 2u]);
        char d = (char) net_ascii_lower((u8) url[len - 1u]);
        if (a == '.' && b == 'p' && c == 'n' && d == 'g') {
            return NET_BROWSER_IMAGE_FORMAT_PNG;
        }
        if (a == '.' && b == 'j' && c == 'p' && d == 'g') {
            return NET_BROWSER_IMAGE_FORMAT_JPEG;
        }
    }
    if (len >= 5u) {
        char a = (char) net_ascii_lower((u8) url[len - 5u]);
        char b = (char) net_ascii_lower((u8) url[len - 4u]);
        char c = (char) net_ascii_lower((u8) url[len - 3u]);
        char d = (char) net_ascii_lower((u8) url[len - 2u]);
        char e = (char) net_ascii_lower((u8) url[len - 1u]);
        if (a == '.' && b == 'j' && c == 'p' && d == 'e' && e == 'g') {
            return NET_BROWSER_IMAGE_FORMAT_JPEG;
        }
    }
    return NET_BROWSER_IMAGE_FORMAT_OTHER;
}

static u8 net_browser_image_find_by_url(const char *url)
{
    for (u32 i = 0u; i < net_browser_image_slot_count; i += 1u) {
        if (net_text_is(net_browser_image_url[i], url)) {
            return (u8) i;
        }
    }
    return 0xFFu;
}

static u8 net_browser_image_add_pre_normalized(const char *url, const char *alt)
{
    u8 existing = net_browser_image_find_by_url(url);
    if (existing != 0xFFu) {
        return existing;
    }
    if (net_browser_image_slot_count >= NET_BROWSER_IMAGE_MAX) {
        return 0xFFu;
    }
    u8 slot = net_browser_image_slot_count++;
    net_append_capped(net_browser_image_url[slot], 0u,
                      NET_BROWSER_RESOURCE_URL_MAX, url);
    net_browser_image_status[slot] = NET_BROWSER_IMAGE_STATUS_PENDING;
    net_browser_image_format[slot] = net_browser_image_format_from_url(url);
    if (alt != 0 && alt[0] != 0) {
        char label[NET_BROWSER_IMAGE_LABEL_MAX];
        u32 pos = net_append_capped(label, 0u, sizeof(label), "pending ");
        pos = net_append_capped(label, pos, sizeof(label), alt);
        (void) pos;
        net_browser_image_copy_text(net_browser_image_label[slot],
                                    NET_BROWSER_IMAGE_LABEL_MAX, label);
    } else {
        net_browser_image_copy_text(net_browser_image_label[slot],
                                    NET_BROWSER_IMAGE_LABEL_MAX, "pending");
    }
    return slot;
}

static void net_browser_image_bind_resource_url(u8 image, const char *url)
{
    if (image >= NET_BROWSER_IMAGE_MAX) {
        return;
    }
    for (u32 r = 0u; r < net_browser_resource_count; r += 1u) {
        if (net_text_is(net_browser_resource_url[r], url)) {
            net_browser_resource_image[r] = image;
        }
    }
}

static u8 net_browser_render_add_image_block(u8 image_index)
{
    if (net_browser_render_count >= NET_BROWSER_RENDER_MAX) {
        return 0xFFu;
    }
    u8 slot = net_browser_render_count++;
    net_browser_render_kind[slot] = NET_BROWSER_RENDER_KIND_IMAGE;
    net_browser_render_flags[slot] = NET_BROWSER_RENDER_FLAG_BOX;
    net_browser_css_copy_active_to_slot(slot);
    net_browser_css_apply_tag_to_slot(slot);
    net_browser_render_link[slot] = 0xFFu;
    net_browser_render_control[slot] = 0xFFu;
    net_browser_render_image[slot] = image_index;
    if (image_index < NET_BROWSER_IMAGE_MAX) {
        net_browser_image_write_render_text(slot, "IMAGE ",
                                            net_browser_image_label[image_index]);
    } else {
        net_browser_image_write_render_text(slot, "IMAGE ", "untracked");
    }
    net_browser_render_break();
    return slot;
}

static u8 net_browser_first_same_host_resource_slot(void)
{
    for (u32 r = 0u; r < net_browser_resource_count; r += 1u) {
        if (net_browser_same_host_path(net_browser_resource_url[r]) != 0) {
            return (u8) r;
        }
    }
    return 0xFFu;
}

static void net_browser_image_prepare_resource_fetch(void)
{
    net_browser_resource_body_len = 0u;
    net_browser_resource_body_overflow = 0u;
    net_browser_fetch_image_index = 0xFFu;
    u8 slot = net_browser_first_same_host_resource_slot();
    if (slot != 0xFFu) {
        net_browser_fetch_image_index = net_browser_resource_image[slot];
    }
}

static void net_browser_image_note_resource_body(const u8 *data, u32 len)
{
    if (net_browser_fetch_image_index == 0xFFu || len == 0u) {
        return;
    }
    if (net_browser_resource_body_len + len > NET_BROWSER_IMAGE_RAW_MAX) {
        net_browser_resource_body_overflow = 1u;
        u32 room = NET_BROWSER_IMAGE_RAW_MAX - net_browser_resource_body_len;
        if (room != 0u) {
            mem_copy(net_browser_resource_body + net_browser_resource_body_len,
                     data, room);
            net_browser_resource_body_len = NET_BROWSER_IMAGE_RAW_MAX;
        }
        return;
    }
    mem_copy(net_browser_resource_body + net_browser_resource_body_len,
             data, len);
    net_browser_resource_body_len += len;
}

static void net_browser_image_note_direct_body(const u8 *data, u32 len)
{
    if (!net_browser_direct_image_active || len == 0u) {
        return;
    }
    if (net_browser_resource_body_len + len > NET_BROWSER_IMAGE_RAW_MAX) {
        net_browser_resource_body_overflow = 1u;
        u32 room = NET_BROWSER_IMAGE_RAW_MAX - net_browser_resource_body_len;
        if (room != 0u) {
            mem_copy(net_browser_resource_body + net_browser_resource_body_len,
                     data, room);
            net_browser_resource_body_len = NET_BROWSER_IMAGE_RAW_MAX;
        }
        return;
    }
    mem_copy(net_browser_resource_body + net_browser_resource_body_len,
             data, len);
    net_browser_resource_body_len += len;
}

static u8 net_png_paeth(u8 a, u8 b, u8 c)
{
    i32 p = (i32) a + (i32) b - (i32) c;
    i32 pa = p - (i32) a;
    i32 pb = p - (i32) b;
    i32 pc = p - (i32) c;
    if (pa < 0) { pa = -pa; }
    if (pb < 0) { pb = -pb; }
    if (pc < 0) { pc = -pc; }
    if (pa <= pb && pa <= pc) { return a; }
    if (pb <= pc) { return b; }
    return c;
}

static u8 net_png_inflate_stored(const u8 *src, u32 src_len,
                                 u8 *dst, u32 dst_max, u32 *dst_len)
{
    *dst_len = 0u;
    if (src_len < 2u) {
        return 0u;
    }
    u8 cmf = src[0];
    u8 flg = src[1];
    if ((cmf & 0x0Fu) != 8u || ((u32) cmf * 256u + flg) % 31u != 0u) {
        return 0u;
    }
    u32 bit = 16u;
    while (bit + 3u <= src_len * 8u) {
        u8 final = (src[bit / 8u] >> (bit % 8u)) & 1u;
        bit += 1u;
        u8 type = 0u;
        for (u32 i = 0u; i < 2u; i += 1u) {
            type |= (u8) (((src[bit / 8u] >> (bit % 8u)) & 1u) << i);
            bit += 1u;
        }
        if (type != 0u) {
            return 0u;
        }
        bit = (bit + 7u) & ~7u;
        u32 pos = bit / 8u;
        if (pos + 4u > src_len) {
            return 0u;
        }
        u16 len = (u16) src[pos] | ((u16) src[pos + 1u] << 8);
        u16 nlen = (u16) src[pos + 2u] | ((u16) src[pos + 3u] << 8);
        if ((u16) (len ^ 0xFFFFu) != nlen || pos + 4u + len > src_len ||
            *dst_len + len > dst_max) {
            return 0u;
        }
        mem_copy(dst + *dst_len, src + pos + 4u, len);
        *dst_len += len;
        bit = (pos + 4u + len) * 8u;
        if (final) {
            return 1u;
        }
    }
    return 0u;
}

static u8 net_browser_decode_png(u8 image, const u8 *data, u32 len)
{
    static const u8 sig[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    if (len < 33u || !mem_equal(data, sig, 8u)) {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                          "bad signature");
        return 0u;
    }
    u32 pos = 8u;
    u16 width = 0u;
    u16 height = 0u;
    u8 bit_depth = 0u;
    u8 color_type = 0u;
    u8 interlace = 0u;
    u32 idat_len = 0u;
    while (pos + 8u <= len) {
        u32 chunk_len = read_be32(data + pos);
        const u8 *type = data + pos + 4u;
        pos += 8u;
        if (pos + chunk_len + 4u > len) {
            net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                              "truncated chunk");
            return 0u;
        }
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            if (chunk_len < 13u) {
                return 0u;
            }
            u32 w32 = read_be32(data + pos);
            u32 h32 = read_be32(data + pos + 4u);
            if (w32 == 0u || h32 == 0u ||
                w32 > NET_BROWSER_IMAGE_SIDE_MAX ||
                h32 > NET_BROWSER_IMAGE_SIDE_MAX) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                                  "too large");
                return 0u;
            }
            width = (u16) w32;
            height = (u16) h32;
            bit_depth = data[pos + 8u];
            color_type = data[pos + 9u];
            interlace = data[pos + 12u];
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (idat_len + chunk_len > NET_BROWSER_IMAGE_RAW_MAX) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                                  "IDAT too large");
                return 0u;
            }
            mem_copy(net_browser_png_idat + idat_len, data + pos, chunk_len);
            idat_len += chunk_len;
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            break;
        }
        pos += chunk_len + 4u;
    }
    if (width == 0u || height == 0u || idat_len == 0u) {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                          "missing image data");
        return 0u;
    }
    if (bit_depth != 8u || (color_type != 2u && color_type != 6u) || interlace != 0u) {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                          "only 8-bit RGB/RGBA noninterlaced");
        return 0u;
    }
    u8 bpp = color_type == 6u ? 4u : 3u;
    u32 row_len = (u32) width * bpp;
    u32 raw_need = (row_len + 1u) * height;
    u32 raw_len = 0u;
    if (raw_need > NET_BROWSER_PNG_RAW_MAX ||
        !net_png_inflate_stored(net_browser_png_idat, idat_len,
                                net_browser_png_raw, NET_BROWSER_PNG_RAW_MAX,
                                &raw_len) ||
        raw_len < raw_need) {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                          "deflate mode not stored");
        return 0u;
    }
    mem_zero(net_browser_png_prev_row, sizeof(net_browser_png_prev_row));
    u32 raw = 0u;
    for (u32 y = 0u; y < height; y += 1u) {
        u8 filter = net_browser_png_raw[raw++];
        for (u32 x = 0u; x < row_len; x += 1u) {
            u8 val = net_browser_png_raw[raw++];
            u8 left = x >= bpp ? net_browser_png_row[x - bpp] : 0u;
            u8 up = net_browser_png_prev_row[x];
            u8 up_left = x >= bpp ? net_browser_png_prev_row[x - bpp] : 0u;
            if (filter == 1u) {
                val = (u8) (val + left);
            } else if (filter == 2u) {
                val = (u8) (val + up);
            } else if (filter == 3u) {
                val = (u8) (val + (u8) (((u16) left + up) / 2u));
            } else if (filter == 4u) {
                val = (u8) (val + net_png_paeth(left, up, up_left));
            } else if (filter != 0u) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                                  "unknown filter");
                return 0u;
            }
            net_browser_png_row[x] = val;
        }
        for (u32 x = 0u; x < width; x += 1u) {
            u32 p = x * bpp;
            u32 r = net_browser_png_row[p];
            u32 g = net_browser_png_row[p + 1u];
            u32 b = net_browser_png_row[p + 2u];
            if (bpp == 4u) {
                u32 a = net_browser_png_row[p + 3u];
                r = (r * a + 255u * (255u - a)) / 255u;
                g = (g * a + 255u * (255u - a)) / 255u;
                b = (b * a + 255u * (255u - a)) / 255u;
            }
            net_browser_image_pixels[image][y * NET_BROWSER_IMAGE_SIDE_MAX + x] =
                (r << 16) | (g << 8) | b;
        }
        mem_copy(net_browser_png_prev_row, net_browser_png_row, row_len);
    }
    net_browser_image_set_decoded(image, NET_BROWSER_IMAGE_FORMAT_PNG,
                                  width, height);
    return 1u;
}

struct NetJpegComp {
    u8 id;
    u8 h;
    u8 v;
    u8 tq;
    u8 td;
    u8 ta;
    i32 dc;
};

struct NetJpegBits {
    const u8 *data;
    u32 len;
    u32 pos;
    u8 bit;
};

static const i32 net_jpeg_idct_basis[8][8] = {
    { 724, 724, 724, 724, 724, 724, 724, 724 },
    { 1004, 851, 569, 200, -200, -569, -851, -1004 },
    { 946, 392, -392, -946, -946, -392, 392, 946 },
    { 851, -200, -1004, -569, 569, 1004, 200, -851 },
    { 724, -724, -724, 724, 724, -724, -724, 724 },
    { 569, -1004, 200, 851, -851, -200, 1004, -569 },
    { 392, -946, 946, -392, -392, 946, -946, 392 },
    { 200, -569, 851, -1004, 1004, -851, 569, -200 }
};

static u8 net_jpeg_build_huff(struct NetJpegHuff *h, const u8 *bits,
                              const u8 *vals, u32 vals_len)
{
    h->count = 0u;
    u16 code = 0u;
    u32 val_pos = 0u;
    for (u8 size = 1u; size <= 16u; size += 1u) {
        u8 n = bits[size];
        for (u8 i = 0u; i < n; i += 1u) {
            if (val_pos >= vals_len || h->count == 255u) {
                return 0u;
            }
            h->symbol[h->count] = vals[val_pos++];
            h->code[h->count] = code;
            h->size[h->count] = size;
            h->count += 1u;
            code += 1u;
        }
        code <<= 1;
    }
    return val_pos == vals_len ? 1u : 0u;
}

static u8 net_jpeg_next_bit(struct NetJpegBits *bits, u8 *out)
{
    if (bits->pos >= bits->len) {
        return 0u;
    }
    u8 byte = bits->data[bits->pos];
    if (bits->bit == 0u) {
        if (byte == 0xFFu && bits->pos + 1u < bits->len) {
            if (bits->data[bits->pos + 1u] == 0x00u) {
                bits->pos += 1u;
                byte = 0xFFu;
            } else {
                return 0u;
            }
        }
    }
    *out = (u8) ((byte >> (7u - bits->bit)) & 1u);
    bits->bit += 1u;
    if (bits->bit == 8u) {
        bits->bit = 0u;
        bits->pos += 1u;
    }
    return 1u;
}

static u8 net_jpeg_read_bits(struct NetJpegBits *bits, u8 count, u16 *out)
{
    u16 value = 0u;
    for (u8 i = 0u; i < count; i += 1u) {
        u8 bit = 0u;
        if (!net_jpeg_next_bit(bits, &bit)) {
            return 0u;
        }
        value = (u16) ((value << 1) | bit);
    }
    *out = value;
    return 1u;
}

static u8 net_jpeg_huff_decode(struct NetJpegBits *bits,
                               const struct NetJpegHuff *h, u8 *symbol)
{
    u16 code = 0u;
    for (u8 size = 1u; size <= 16u; size += 1u) {
        u8 bit = 0u;
        if (!net_jpeg_next_bit(bits, &bit)) {
            return 0u;
        }
        code = (u16) ((code << 1) | bit);
        for (u32 i = 0u; i < h->count; i += 1u) {
            if (h->size[i] == size && h->code[i] == code) {
                *symbol = h->symbol[i];
                return 1u;
            }
        }
    }
    return 0u;
}

static i32 net_jpeg_extend(u16 value, u8 bits)
{
    if (bits == 0u) {
        return 0;
    }
    u16 threshold = (u16) (1u << (bits - 1u));
    if (value >= threshold) {
        return (i32) value;
    }
    return (i32) value + 1 - (i32) (1u << bits);
}

static u8 net_jpeg_decode_block(struct NetJpegBits *bits,
                                const struct NetJpegHuff *dc_huff,
                                const struct NetJpegHuff *ac_huff,
                                const u16 *qtable,
                                i32 *prev_dc,
                                i32 out[64])
{
    for (u32 i = 0u; i < 64u; i += 1u) {
        out[i] = 0;
    }
    u8 sym = 0u;
    if (!net_jpeg_huff_decode(bits, dc_huff, &sym) || sym > 11u) {
        return 0u;
    }
    u16 raw = 0u;
    if (!net_jpeg_read_bits(bits, sym, &raw)) {
        return 0u;
    }
    *prev_dc += net_jpeg_extend(raw, sym);
    out[0] = (*prev_dc) * (i32) qtable[0];
    u8 k = 1u;
    while (k < 64u) {
        if (!net_jpeg_huff_decode(bits, ac_huff, &sym)) {
            return 0u;
        }
        u8 run = (u8) (sym >> 4u);
        u8 size = (u8) (sym & 0x0Fu);
        if (size == 0u) {
            if (run == 15u) {
                k = (u8) (k + 16u);
                continue;
            }
            break;
        }
        k = (u8) (k + run);
        if (k >= 64u || size > 10u) {
            return 0u;
        }
        if (!net_jpeg_read_bits(bits, size, &raw)) {
            return 0u;
        }
        u8 pos = net_jpeg_zigzag[k];
        out[pos] = net_jpeg_extend(raw, size) * (i32) qtable[pos];
        k += 1u;
    }
    return 1u;
}

static u8 net_jpeg_clamp_sample(i32 value)
{
    if (value < 0) {
        return 0u;
    }
    if (value > 255) {
        return 255u;
    }
    return (u8) value;
}

static i32 net_jpeg_round_shift22(i64 value)
{
    if (value >= 0) {
        return (i32) ((value + (1LL << 21)) >> 22);
    }
    return (i32) (-(((-value) + (1LL << 21)) >> 22));
}

static void net_jpeg_idct_block(const i32 in[64], u8 out[64])
{
    for (u8 y = 0u; y < 8u; y += 1u) {
        for (u8 x = 0u; x < 8u; x += 1u) {
            i64 sum = 0;
            for (u8 v = 0u; v < 8u; v += 1u) {
                for (u8 u = 0u; u < 8u; u += 1u) {
                    sum += (i64) in[v * 8u + u] *
                           (i64) net_jpeg_idct_basis[u][x] *
                           (i64) net_jpeg_idct_basis[v][y];
                }
            }
            i32 sample = 128 + net_jpeg_round_shift22(sum);
            out[y * 8u + x] = net_jpeg_clamp_sample(sample);
        }
    }
}

static u8 net_jpeg_comp_index(const struct NetJpegComp comps[3], u8 count, u8 id)
{
    for (u8 i = 0u; i < count; i += 1u) {
        if (comps[i].id == id) {
            return i;
        }
    }
    return 0xFFu;
}

static u32 net_jpeg_next_marker(const u8 *data, u32 len, u32 pos)
{
    while (pos + 1u < len) {
        if (data[pos] == 0xFFu && data[pos + 1u] != 0x00u) {
            return pos;
        }
        pos += 1u;
    }
    return len;
}

static u8 net_browser_decode_jpeg(u8 image, const u8 *data, u32 len)
{
    if (len < 4u || data[0] != 0xFFu || data[1] != 0xD8u) {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                          "bad signature");
        return 0u;
    }
    u16 qtables[4][64];
    u8 qvalid[4] = { 0u, 0u, 0u, 0u };
    struct NetJpegHuff huff_dc[4];
    struct NetJpegHuff huff_ac[4];
    u8 hvalid_dc[4] = { 0u, 0u, 0u, 0u };
    u8 hvalid_ac[4] = { 0u, 0u, 0u, 0u };
    struct NetJpegComp comps[3];
    u8 comp_count = 0u;
    u16 width = 0u;
    u16 height = 0u;
    u32 pos = 2u;
    u32 scan_pos = 0u;
    for (u32 i = 0u; i < 4u; i += 1u) {
        huff_dc[i].count = 0u;
        huff_ac[i].count = 0u;
    }
    while (pos + 4u <= len) {
        while (pos < len && data[pos] == 0xFFu) {
            pos += 1u;
        }
        if (pos >= len) {
            break;
        }
        u8 marker = data[pos++];
        if (marker == 0xD9u) {
            break;
        }
        if (marker == 0xDAu) {
            if (pos + 2u > len) {
                return 0u;
            }
            u16 seg_len = read_be16(data + pos);
            if (seg_len < 8u || pos + seg_len > len) {
                return 0u;
            }
            u8 n = data[pos + 2u];
            if (n != comp_count || n == 0u || n > 3u) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                                  "scan component mismatch");
                return 0u;
            }
            u32 p = pos + 3u;
            for (u8 i = 0u; i < n; i += 1u) {
                u8 ci = net_jpeg_comp_index(comps, comp_count, data[p]);
                if (ci == 0xFFu) {
                    return 0u;
                }
                comps[ci].td = (u8) (data[p + 1u] >> 4u);
                comps[ci].ta = (u8) (data[p + 1u] & 0x0Fu);
                p += 2u;
            }
            if (data[p] != 0u || data[p + 1u] != 0x3Fu || data[p + 2u] != 0u) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                                  "progressive scan");
                return 0u;
            }
            scan_pos = pos + seg_len;
            break;
        }
        if (pos + 2u > len) {
            break;
        }
        u16 seg_len = read_be16(data + pos);
        if (seg_len < 2u || pos + seg_len > len) {
            return 0u;
        }
        const u8 *seg = data + pos + 2u;
        u32 seg_data_len = seg_len - 2u;
        if (marker == 0xDBu) {
            u32 p = 0u;
            while (p < seg_data_len) {
                u8 pq_tq = seg[p++];
                u8 precision = (u8) (pq_tq >> 4u);
                u8 tq = (u8) (pq_tq & 0x0Fu);
                if (precision != 0u || tq >= 4u || p + 64u > seg_data_len) {
                    net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                                      "quant table precision");
                    return 0u;
                }
                for (u8 i = 0u; i < 64u; i += 1u) {
                    qtables[tq][net_jpeg_zigzag[i]] = seg[p + i];
                }
                qvalid[tq] = 1u;
                p += 64u;
            }
        } else if (marker == 0xC4u) {
            u32 p = 0u;
            while (p + 17u <= seg_data_len) {
                u8 tc_th = seg[p++];
                u8 tc = (u8) (tc_th >> 4u);
                u8 th = (u8) (tc_th & 0x0Fu);
                if (tc > 1u || th >= 4u) {
                    return 0u;
                }
                u8 bits[17];
                bits[0] = 0u;
                u32 total = 0u;
                for (u8 i = 1u; i <= 16u; i += 1u) {
                    bits[i] = seg[p++];
                    total += bits[i];
                }
                if (p + total > seg_data_len) {
                    return 0u;
                }
                if (tc == 0u) {
                    if (!net_jpeg_build_huff(&huff_dc[th], bits, seg + p, total)) {
                        return 0u;
                    }
                    hvalid_dc[th] = 1u;
                } else {
                    if (!net_jpeg_build_huff(&huff_ac[th], bits, seg + p, total)) {
                        return 0u;
                    }
                    hvalid_ac[th] = 1u;
                }
                p += total;
            }
        } else if (marker == 0xC0u) {
            if (seg_data_len < 6u || seg[0] != 8u) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                                  "not 8-bit baseline");
                return 0u;
            }
            height = read_be16(seg + 1u);
            width = read_be16(seg + 3u);
            comp_count = seg[5u];
            if (width == 0u || height == 0u ||
                comp_count == 0u || comp_count > 3u ||
                seg_data_len < 6u + (u32) comp_count * 3u) {
                net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                                  "invalid frame");
                return 0u;
            }
            if (width > NET_BROWSER_IMAGE_SIDE_MAX ||
                height > NET_BROWSER_IMAGE_SIDE_MAX) {
                net_browser_image_set_unsupported_size(image,
                    NET_BROWSER_IMAGE_FORMAT_JPEG, "too large", width, height);
                return 0u;
            }
            u32 p = 6u;
            for (u8 i = 0u; i < comp_count; i += 1u) {
                comps[i].id = seg[p++];
                comps[i].h = (u8) (seg[p] >> 4u);
                comps[i].v = (u8) (seg[p] & 0x0Fu);
                p += 1u;
                comps[i].tq = seg[p++];
                comps[i].td = 0u;
                comps[i].ta = 0u;
                comps[i].dc = 0;
                if (comps[i].h == 0u || comps[i].v == 0u ||
                    comps[i].h > 2u || comps[i].v > 2u ||
                    comps[i].tq >= 4u) {
                    net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                                      "sampling not supported");
                    return 0u;
                }
            }
        } else if (marker == 0xC2u) {
            net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                              "progressive JPEG");
            return 0u;
        }
        pos += seg_len;
    }
    if (scan_pos == 0u || width == 0u || height == 0u) {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                          "missing scan");
        return 0u;
    }
    u8 max_h = 1u;
    u8 max_v = 1u;
    for (u8 i = 0u; i < comp_count; i += 1u) {
        if (!qvalid[comps[i].tq] ||
            !hvalid_dc[comps[i].td] ||
            !hvalid_ac[comps[i].ta]) {
            net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                              "missing tables");
            return 0u;
        }
        if (comps[i].h > max_h) { max_h = comps[i].h; }
        if (comps[i].v > max_v) { max_v = comps[i].v; }
    }
    u32 entropy_end = net_jpeg_next_marker(data, len, scan_pos);
    struct NetJpegBits bits;
    bits.data = data + scan_pos;
    bits.len = entropy_end - scan_pos;
    bits.pos = 0u;
    bits.bit = 0u;
    u8 sample[3][4][64];
    i32 coeff[64];
    u32 mcu_w = (u32) max_h * 8u;
    u32 mcu_h = (u32) max_v * 8u;
    for (u32 my = 0u; my < height; my += mcu_h) {
        for (u32 mx = 0u; mx < width; mx += mcu_w) {
            for (u8 ci = 0u; ci < comp_count; ci += 1u) {
                u8 block = 0u;
                for (u8 by = 0u; by < comps[ci].v; by += 1u) {
                    for (u8 bx = 0u; bx < comps[ci].h; bx += 1u) {
                        if (!net_jpeg_decode_block(&bits,
                                                   &huff_dc[comps[ci].td],
                                                   &huff_ac[comps[ci].ta],
                                                   qtables[comps[ci].tq],
                                                   &comps[ci].dc,
                                                   coeff)) {
                            net_browser_image_set_unsupported(image,
                                NET_BROWSER_IMAGE_FORMAT_JPEG,
                                "entropy decode failed");
                            return 0u;
                        }
                        net_jpeg_idct_block(coeff, sample[ci][block]);
                        block += 1u;
                    }
                }
            }
            for (u32 py = 0u; py < mcu_h && my + py < height; py += 1u) {
                for (u32 px = 0u; px < mcu_w && mx + px < width; px += 1u) {
                    i32 values[3] = { 128, 128, 128 };
                    for (u8 ci = 0u; ci < comp_count; ci += 1u) {
                        u32 local_x = (px * comps[ci].h) / max_h;
                        u32 local_y = (py * comps[ci].v) / max_v;
                        u8 bx = (u8) (local_x / 8u);
                        u8 by = (u8) (local_y / 8u);
                        u8 block = (u8) (by * comps[ci].h + bx);
                        values[ci] = sample[ci][block][(local_y % 8u) * 8u +
                                                       (local_x % 8u)];
                    }
                    i32 r = values[0];
                    i32 g = values[0];
                    i32 b = values[0];
                    if (comp_count >= 3u) {
                        i32 cb = values[1] - 128;
                        i32 cr = values[2] - 128;
                        r = values[0] + ((91881 * cr) >> 16);
                        g = values[0] - ((22554 * cb + 46802 * cr) >> 16);
                        b = values[0] + ((116130 * cb) >> 16);
                    }
                    net_browser_image_pixels[image][(my + py) * NET_BROWSER_IMAGE_SIDE_MAX +
                                                     (mx + px)] =
                        ((u32) net_jpeg_clamp_sample(r) << 16) |
                        ((u32) net_jpeg_clamp_sample(g) << 8) |
                        (u32) net_jpeg_clamp_sample(b);
                }
            }
        }
    }
    net_browser_image_set_decoded(image, NET_BROWSER_IMAGE_FORMAT_JPEG,
                                  width, height);
    return 1u;
}

static void net_browser_image_decode_slot(u8 image, const u8 *data, u32 len)
{
    if (image >= NET_BROWSER_IMAGE_MAX || data == 0 || len == 0u) {
        return;
    }
    if (len >= 8u && data[0] == 137u && data[1] == 'P' &&
        data[2] == 'N' && data[3] == 'G') {
        (void) net_browser_decode_png(image, data, len);
    } else if (len >= 2u && data[0] == 0xFFu && data[1] == 0xD8u) {
        (void) net_browser_decode_jpeg(image, data, len);
    } else {
        net_browser_image_set_unsupported(image, NET_BROWSER_IMAGE_FORMAT_OTHER,
                                          "not PNG/JPEG");
    }
}

static void net_browser_image_decode_current_resource(void)
{
    if (net_browser_fetch_image_index == 0xFFu) {
        return;
    }
    u8 image = net_browser_fetch_image_index;
    if (net_browser_resource_body_overflow) {
        net_browser_image_set_unsupported(image,
            net_browser_image_format[image],
            "body too large");
        return;
    }
    net_browser_image_decode_slot(image, net_browser_resource_body,
                                  net_browser_resource_body_len);
}

static void net_browser_image_start_direct_document(u8 format)
{
    if (format != NET_BROWSER_IMAGE_FORMAT_PNG &&
        format != NET_BROWSER_IMAGE_FORMAT_JPEG) {
        return;
    }
    net_browser_image_reset_resource_body();
    net_browser_render_reset();
    net_browser_direct_image_format = format;
    const char *label = format == NET_BROWSER_IMAGE_FORMAT_PNG
        ? "direct PNG document"
        : "direct JPEG document";
    u8 image = net_browser_image_add_pre_normalized(net_browser_current_url,
                                                    label);
    if (image == 0xFFu) {
        return;
    }
    net_browser_direct_image_active = 1u;
    net_browser_direct_image_slot = image;
    net_browser_text_ready = 0u;
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_TITLE, 0u,
                                       "", "Image Document");
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_HEADING,
                                       NET_BROWSER_RENDER_FLAG_LARGE |
                                       NET_BROWSER_RENDER_FLAG_BOLD,
                                       "", "Image Document");
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_TEXT, 0u,
                                       "URL ", net_browser_current_url);
    (void) net_browser_render_add_image_block(image);
    serial_print("LeonOS net browser direct image document ");
    serial_print(format == NET_BROWSER_IMAGE_FORMAT_PNG ? "PNG " : "JPEG ");
    serial_print(net_browser_current_url);
    serial_print("\r\n");
}

static void net_browser_image_finalize_direct_document(void)
{
    if (!net_browser_direct_image_active) {
        return;
    }
    u8 image = net_browser_direct_image_slot;
    net_browser_direct_image_active = 0u;
    if (image == 0xFFu || image >= NET_BROWSER_IMAGE_MAX) {
        return;
    }
    if (net_browser_resource_body_len == 0u) {
        net_browser_image_set_unsupported(image, net_browser_direct_image_format,
                                          "empty body");
    } else {
        net_browser_image_decode_slot(image, net_browser_resource_body,
                                      net_browser_resource_body_len);
        if (net_browser_image_status[image] == NET_BROWSER_IMAGE_STATUS_PENDING) {
            net_browser_image_set_unsupported(image, net_browser_direct_image_format,
                                              net_browser_resource_body_overflow
                                                  ? "body too large"
                                                  : "decode failed");
        }
    }
    net_browser_text_ready = 1u;
    serial_print("LeonOS net browser direct image summary status ");
    serial_print_dec(net_browser_image_status[image]);
    serial_print(" bytes ");
    serial_print_dec(net_browser_resource_body_len);
    serial_print(" overflow ");
    serial_print(net_browser_resource_body_overflow ? "yes" : "no");
    serial_print("\r\n");
    dirty = 1;
}

static const u8 net_browser_selftest_png[] = {
    0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x0Du, 0x49u, 0x48u, 0x44u, 0x52u,
    0x00u, 0x00u, 0x00u, 0x08u, 0x00u, 0x00u, 0x00u, 0x08u, 0x08u, 0x06u, 0x00u, 0x00u, 0x00u, 0xC4u, 0x0Fu, 0xBEu,
    0x8Bu, 0x00u, 0x00u, 0x01u, 0x13u, 0x49u, 0x44u, 0x41u, 0x54u, 0x78u, 0x01u, 0x01u, 0x08u, 0x01u, 0xF7u, 0xFEu,
    0x00u, 0x23u, 0x5Au, 0xBEu, 0xFFu, 0x3Bu, 0x5Au, 0xB6u, 0xFFu, 0x53u, 0x5Au, 0xAEu, 0xFFu, 0x6Bu, 0x5Au, 0xA6u,
    0xFFu, 0x83u, 0x5Au, 0x9Eu, 0xFFu, 0x9Bu, 0x5Au, 0x96u, 0xFFu, 0xB3u, 0x5Au, 0x8Eu, 0xFFu, 0xCBu, 0x5Au, 0x86u,
    0xFFu, 0x00u, 0x23u, 0x6Cu, 0xB6u, 0xFFu, 0x3Bu, 0x6Cu, 0xAEu, 0xFFu, 0x53u, 0x6Cu, 0xA6u, 0xFFu, 0x6Bu, 0x6Cu,
    0x9Eu, 0xFFu, 0x83u, 0x6Cu, 0x96u, 0xFFu, 0x9Bu, 0x6Cu, 0x8Eu, 0xFFu, 0xB3u, 0x6Cu, 0x86u, 0xFFu, 0xCBu, 0x6Cu,
    0x7Eu, 0xFFu, 0x00u, 0x23u, 0x7Eu, 0xAEu, 0xFFu, 0x3Bu, 0x7Eu, 0xA6u, 0xFFu, 0x53u, 0x7Eu, 0x9Eu, 0xFFu, 0x6Bu,
    0x7Eu, 0x96u, 0xFFu, 0x83u, 0x7Eu, 0x8Eu, 0xFFu, 0x9Bu, 0x7Eu, 0x86u, 0xFFu, 0xB3u, 0x7Eu, 0x7Eu, 0xFFu, 0xCBu,
    0x7Eu, 0x76u, 0xFFu, 0x00u, 0x23u, 0x90u, 0xA6u, 0xFFu, 0x3Bu, 0x90u, 0x9Eu, 0xFFu, 0x53u, 0x90u, 0x96u, 0xFFu,
    0x6Bu, 0x90u, 0x8Eu, 0xFFu, 0x83u, 0x90u, 0x86u, 0xFFu, 0x9Bu, 0x90u, 0x7Eu, 0xFFu, 0xB3u, 0x90u, 0x76u, 0xFFu,
    0xCBu, 0x90u, 0x6Eu, 0xFFu, 0x00u, 0x23u, 0xA2u, 0x9Eu, 0xFFu, 0x3Bu, 0xA2u, 0x96u, 0xFFu, 0x53u, 0xA2u, 0x8Eu,
    0xFFu, 0x6Bu, 0xA2u, 0x86u, 0xFFu, 0x83u, 0xA2u, 0x7Eu, 0xFFu, 0x9Bu, 0xA2u, 0x76u, 0xFFu, 0xB3u, 0xA2u, 0x6Eu,
    0xFFu, 0xCBu, 0xA2u, 0x66u, 0xFFu, 0x00u, 0x23u, 0xB4u, 0x96u, 0xFFu, 0x3Bu, 0xB4u, 0x8Eu, 0xFFu, 0x53u, 0xB4u,
    0x86u, 0xFFu, 0x6Bu, 0xB4u, 0x7Eu, 0xFFu, 0x83u, 0xB4u, 0x76u, 0xFFu, 0x9Bu, 0xB4u, 0x6Eu, 0xFFu, 0xB3u, 0xB4u,
    0x66u, 0xFFu, 0xCBu, 0xB4u, 0x5Eu, 0xFFu, 0x00u, 0x23u, 0xC6u, 0x8Eu, 0xFFu, 0x3Bu, 0xC6u, 0x86u, 0xFFu, 0x53u,
    0xC6u, 0x7Eu, 0xFFu, 0x6Bu, 0xC6u, 0x76u, 0xFFu, 0x83u, 0xC6u, 0x6Eu, 0xFFu, 0x9Bu, 0xC6u, 0x66u, 0xFFu, 0xB3u,
    0xC6u, 0x5Eu, 0xFFu, 0xCBu, 0xC6u, 0x56u, 0xFFu, 0x00u, 0x23u, 0xD8u, 0x86u, 0xFFu, 0x3Bu, 0xD8u, 0x7Eu, 0xFFu,
    0x53u, 0xD8u, 0x76u, 0xFFu, 0x6Bu, 0xD8u, 0x6Eu, 0xFFu, 0x83u, 0xD8u, 0x66u, 0xFFu, 0x9Bu, 0xD8u, 0x5Eu, 0xFFu,
    0xB3u, 0xD8u, 0x56u, 0xFFu, 0xCBu, 0xD8u, 0x4Eu, 0xFFu, 0x05u, 0x25u, 0xA5u, 0x41u, 0x73u, 0x0Du, 0xA6u, 0x49u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4Eu, 0x44u, 0xAEu, 0x42u, 0x60u, 0x82u
};

static const u8 net_browser_selftest_jpeg[] = {
    0xFFu, 0xD8u, 0xFFu, 0xE0u, 0x00u, 0x10u, 0x4Au, 0x46u, 0x49u, 0x46u, 0x00u, 0x01u, 0x01u, 0x01u, 0x00u, 0x60u,
    0x00u, 0x60u, 0x00u, 0x00u, 0xFFu, 0xDBu, 0x00u, 0x43u, 0x00u, 0x03u, 0x02u, 0x02u, 0x03u, 0x02u, 0x02u, 0x03u,
    0x03u, 0x03u, 0x03u, 0x04u, 0x03u, 0x03u, 0x04u, 0x05u, 0x08u, 0x05u, 0x05u, 0x04u, 0x04u, 0x05u, 0x0Au, 0x07u,
    0x07u, 0x06u, 0x08u, 0x0Cu, 0x0Au, 0x0Cu, 0x0Cu, 0x0Bu, 0x0Au, 0x0Bu, 0x0Bu, 0x0Du, 0x0Eu, 0x12u, 0x10u, 0x0Du,
    0x0Eu, 0x11u, 0x0Eu, 0x0Bu, 0x0Bu, 0x10u, 0x16u, 0x10u, 0x11u, 0x13u, 0x14u, 0x15u, 0x15u, 0x15u, 0x0Cu, 0x0Fu,
    0x17u, 0x18u, 0x16u, 0x14u, 0x18u, 0x12u, 0x14u, 0x15u, 0x14u, 0xFFu, 0xDBu, 0x00u, 0x43u, 0x01u, 0x03u, 0x04u,
    0x04u, 0x05u, 0x04u, 0x05u, 0x09u, 0x05u, 0x05u, 0x09u, 0x14u, 0x0Du, 0x0Bu, 0x0Du, 0x14u, 0x14u, 0x14u, 0x14u,
    0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u,
    0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u,
    0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0x14u, 0xFFu, 0xC0u,
    0x00u, 0x11u, 0x08u, 0x00u, 0x08u, 0x00u, 0x08u, 0x03u, 0x01u, 0x22u, 0x00u, 0x02u, 0x11u, 0x01u, 0x03u, 0x11u,
    0x01u, 0xFFu, 0xC4u, 0x00u, 0x1Fu, 0x00u, 0x00u, 0x01u, 0x05u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u,
    0x0Au, 0x0Bu, 0xFFu, 0xC4u, 0x00u, 0xB5u, 0x10u, 0x00u, 0x02u, 0x01u, 0x03u, 0x03u, 0x02u, 0x04u, 0x03u, 0x05u,
    0x05u, 0x04u, 0x04u, 0x00u, 0x00u, 0x01u, 0x7Du, 0x01u, 0x02u, 0x03u, 0x00u, 0x04u, 0x11u, 0x05u, 0x12u, 0x21u,
    0x31u, 0x41u, 0x06u, 0x13u, 0x51u, 0x61u, 0x07u, 0x22u, 0x71u, 0x14u, 0x32u, 0x81u, 0x91u, 0xA1u, 0x08u, 0x23u,
    0x42u, 0xB1u, 0xC1u, 0x15u, 0x52u, 0xD1u, 0xF0u, 0x24u, 0x33u, 0x62u, 0x72u, 0x82u, 0x09u, 0x0Au, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1Au, 0x25u, 0x26u, 0x27u, 0x28u, 0x29u, 0x2Au, 0x34u, 0x35u, 0x36u, 0x37u, 0x38u, 0x39u, 0x3Au,
    0x43u, 0x44u, 0x45u, 0x46u, 0x47u, 0x48u, 0x49u, 0x4Au, 0x53u, 0x54u, 0x55u, 0x56u, 0x57u, 0x58u, 0x59u, 0x5Au,
    0x63u, 0x64u, 0x65u, 0x66u, 0x67u, 0x68u, 0x69u, 0x6Au, 0x73u, 0x74u, 0x75u, 0x76u, 0x77u, 0x78u, 0x79u, 0x7Au,
    0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u, 0x89u, 0x8Au, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u, 0x98u, 0x99u,
    0x9Au, 0xA2u, 0xA3u, 0xA4u, 0xA5u, 0xA6u, 0xA7u, 0xA8u, 0xA9u, 0xAAu, 0xB2u, 0xB3u, 0xB4u, 0xB5u, 0xB6u, 0xB7u,
    0xB8u, 0xB9u, 0xBAu, 0xC2u, 0xC3u, 0xC4u, 0xC5u, 0xC6u, 0xC7u, 0xC8u, 0xC9u, 0xCAu, 0xD2u, 0xD3u, 0xD4u, 0xD5u,
    0xD6u, 0xD7u, 0xD8u, 0xD9u, 0xDAu, 0xE1u, 0xE2u, 0xE3u, 0xE4u, 0xE5u, 0xE6u, 0xE7u, 0xE8u, 0xE9u, 0xEAu, 0xF1u,
    0xF2u, 0xF3u, 0xF4u, 0xF5u, 0xF6u, 0xF7u, 0xF8u, 0xF9u, 0xFAu, 0xFFu, 0xC4u, 0x00u, 0x1Fu, 0x01u, 0x00u, 0x03u,
    0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u,
    0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Bu, 0xFFu, 0xC4u, 0x00u, 0xB5u, 0x11u, 0x00u,
    0x02u, 0x01u, 0x02u, 0x04u, 0x04u, 0x03u, 0x04u, 0x07u, 0x05u, 0x04u, 0x04u, 0x00u, 0x01u, 0x02u, 0x77u, 0x00u,
    0x01u, 0x02u, 0x03u, 0x11u, 0x04u, 0x05u, 0x21u, 0x31u, 0x06u, 0x12u, 0x41u, 0x51u, 0x07u, 0x61u, 0x71u, 0x13u,
    0x22u, 0x32u, 0x81u, 0x08u, 0x14u, 0x42u, 0x91u, 0xA1u, 0xB1u, 0xC1u, 0x09u, 0x23u, 0x33u, 0x52u, 0xF0u, 0x15u,
    0x62u, 0x72u, 0xD1u, 0x0Au, 0x16u, 0x24u, 0x34u, 0xE1u, 0x25u, 0xF1u, 0x17u, 0x18u, 0x19u, 0x1Au, 0x26u, 0x27u,
    0x28u, 0x29u, 0x2Au, 0x35u, 0x36u, 0x37u, 0x38u, 0x39u, 0x3Au, 0x43u, 0x44u, 0x45u, 0x46u, 0x47u, 0x48u, 0x49u,
    0x4Au, 0x53u, 0x54u, 0x55u, 0x56u, 0x57u, 0x58u, 0x59u, 0x5Au, 0x63u, 0x64u, 0x65u, 0x66u, 0x67u, 0x68u, 0x69u,
    0x6Au, 0x73u, 0x74u, 0x75u, 0x76u, 0x77u, 0x78u, 0x79u, 0x7Au, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u,
    0x89u, 0x8Au, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u, 0x98u, 0x99u, 0x9Au, 0xA2u, 0xA3u, 0xA4u, 0xA5u, 0xA6u,
    0xA7u, 0xA8u, 0xA9u, 0xAAu, 0xB2u, 0xB3u, 0xB4u, 0xB5u, 0xB6u, 0xB7u, 0xB8u, 0xB9u, 0xBAu, 0xC2u, 0xC3u, 0xC4u,
    0xC5u, 0xC6u, 0xC7u, 0xC8u, 0xC9u, 0xCAu, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u, 0xDAu, 0xE2u,
    0xE3u, 0xE4u, 0xE5u, 0xE6u, 0xE7u, 0xE8u, 0xE9u, 0xEAu, 0xF2u, 0xF3u, 0xF4u, 0xF5u, 0xF6u, 0xF7u, 0xF8u, 0xF9u,
    0xFAu, 0xFFu, 0xDAu, 0x00u, 0x0Cu, 0x03u, 0x01u, 0x00u, 0x02u, 0x11u, 0x03u, 0x11u, 0x00u, 0x3Fu, 0x00u, 0xD4u,
    0xF8u, 0x7Du, 0xF0u, 0x67u, 0xFDu, 0x57u, 0xEEu, 0x3Du, 0x3Bu, 0x51u, 0x45u, 0x15u, 0xF2u, 0x19u, 0xF7u, 0x10u,
    0xE6u, 0x1Fu, 0x5Du, 0x97u, 0xBEu, 0x5Fu, 0x03u, 0xE7u, 0xB8u, 0xEFu, 0xECu, 0x7Au, 0x7Eu, 0xF9u, 0xFFu, 0xD9u
};

static void net_browser_open_image_selftest(void)
{
    static const char html[] =
        "<!doctype html><html><head><title>Image Selftest</title></head><body>"
        "<h1>Image Selftest</h1>"
        "<p>Native decoder coverage: PNG, JPEG, and unsupported labels.</p>"
        "<img src=\"/fixtures/leonos-image-selftest.png\" alt=\"PNG fixture\">"
        "<img src=\"/fixtures/leonos-image-selftest.jpg\" alt=\"JPEG fixture\">"
        "<img src=\"/fixtures/vector.svg\" alt=\"SVG unsupported\">"
        "</body></html>";

    net_browser_history_save_scroll();
    net_append_capped(net_browser_current_url, 0u,
                      sizeof(net_browser_current_url),
                      "leonos://image-selftest");
    net_append_capped(net_browser_host, 0u, sizeof(net_browser_host),
                      "images.local");
    net_append_capped(net_browser_path, 0u, sizeof(net_browser_path),
                      "/image-selftest");
    if (!net_browser_history_suppress) {
        net_browser_history_record(net_browser_current_url);
    }
    net_browser_text_reset();
    net_browser_fetch_enabled = 0u;
    net_tls_fetch_kind = 0u;
    net_tls_connected = 0u;
    net_browser_status[0] = '2';
    net_browser_status[1] = '0';
    net_browser_status[2] = '0';
    net_browser_status[3] = 0;
    net_append_capped(net_browser_line, 0u, sizeof(net_browser_line),
                      "IMAGE SELFTEST");
    net_append_capped(net_browser_location, 0u, sizeof(net_browser_location),
                      net_browser_current_url);
    serial_print("LeonOS net browser image selftest open\r\n");
    net_browser_html_feed((const u8 *) html, sizeof(html) - 1u);
    for (u8 i = 0u; i < net_browser_image_slot_count; i += 1u) {
        if (net_browser_image_format[i] == NET_BROWSER_IMAGE_FORMAT_PNG) {
            net_browser_image_decode_slot(i, net_browser_selftest_png,
                                          sizeof(net_browser_selftest_png));
        } else if (net_browser_image_format[i] == NET_BROWSER_IMAGE_FORMAT_JPEG) {
            net_browser_image_decode_slot(i, net_browser_selftest_jpeg,
                                          sizeof(net_browser_selftest_jpeg));
        } else {
            net_browser_image_set_unsupported(i, NET_BROWSER_IMAGE_FORMAT_OTHER,
                                              "not PNG/JPEG");
        }
    }
    net_browser_text_ready = 1u;
    net_https_body_decoded_total32 = sizeof(html) - 1u;
    net_browser_maybe_print_structure();
    net_browser_finalize_primary_response("fixture");
    serial_print("LeonOS net browser image summary decoded ");
    serial_print_dec(net_browser_image_decode_count);
    serial_print(" unsupported ");
    serial_print_dec(net_browser_image_unsupported_count);
    serial_print(" png ");
    serial_print_dec(net_browser_image_png_count);
    serial_print(" jpeg ");
    serial_print_dec(net_browser_image_jpeg_count);
    serial_print("\r\n");
    dirty = 1;
}
