/* Included at the end of kernel.c — desktop shell / window manager. */

typedef enum {
    UI_SPRITE_CHROME_MIN = 0,
    UI_SPRITE_CHROME_MAX = 1,
    UI_SPRITE_CHROME_CLOSE = 2,
    UI_SPRITE_START = 3,
    UI_SPRITE_FOLDER = 4,
    UI_SPRITE_INFO = 5,
    UI_SPRITE_APPS = 6,
    UI_SPRITE_LOG = 7,
    UI_SPRITE_COUNT = 8
} UiSpriteId;

#define SHELL_WIN_MAX 8u
#define SHELL_WIN_NONE 0xFFu
#define SHELL_WIN_FILES 0u
#define SHELL_WIN_ABOUT 1u
#define SHELL_WIN_APPS 2u
#define SHELL_WIN_LOG 3u
#define SHELL_WIN_NET 4u

#define SHELL_APP_FILES 0u
#define SHELL_APP_APPS 1u
#define SHELL_APP_ABOUT 2u
#define SHELL_APP_LOG 3u
#define SHELL_APP_NET 4u
#define SHELL_APP_HELLO 5u
#define SHELL_APP_UHELLO 6u
#define SHELL_APP_WRITE 7u
#define SHELL_APP_UGFX 8u
#define SHELL_APP_UCDEMO 9u
#define SHELL_APP_UBROWSER 10u
#define SHELL_APP_UNETRUN 11u
#define SHELL_APP_UWEB 12u
#define SHELL_APP_NETSURF 13u
#define SHELL_APP_USTREAM 14u
#define SHELL_APP_UQJS 15u
#define SHELL_APP_COUNT 16u
#define SHELL_APP_START_COUNT 5u
#define SHELL_APP_PROGRAM_COUNT 1u

#define SHELL_ACTION_NONE 0u
#define SHELL_ACTION_HELLO 1u
#define SHELL_ACTION_UHELLO 2u
#define SHELL_ACTION_WRITE 3u
#define SHELL_ACTION_UGFX 4u
#define SHELL_ACTION_UCDEMO 5u
#define SHELL_ACTION_UBROWSER 6u
#define SHELL_ACTION_UNETRUN 7u
#define SHELL_ACTION_UWEB 8u
#define SHELL_ACTION_NETSURF 9u
#define SHELL_ACTION_USTREAM 10u
#define SHELL_ACTION_UQJS 11u
#define SHELL_LOG_MAX 6u

#define SHELL_WF_VISIBLE 1u
#define SHELL_WF_MINIMIZED 2u
#define SHELL_WF_MAXIMIZED 4u

#define SHELL_COL_WALL_TOP 0x00101A2Eu
#define SHELL_COL_TITLE_FOCUS 0x001A73E8u
#define SHELL_COL_TITLE_IDLE 0x003A4556u
#define SHELL_COL_CLIENT 0x00F7F9FCu
#define SHELL_COL_SIDEBAR 0x00EEF1F5u
#define SHELL_COL_BORDER 0x00CBD5E1u
#define SHELL_COL_CLOSE 0x00D93025u
#define SHELL_COL_MIN 0x00E8A317u
#define SHELL_COL_MAX 0x0034A853u
#define SHELL_COL_TASKBAR 0x001F2226u
#define SHELL_COL_START 0x001A73E8u
#define SHELL_COL_TAB_ACT 0x00FFFFFFu
#define SHELL_COL_TAB_IDLE 0x00DDE4EAu
#define SHELL_COL_TAB_ACCENT 0x001A73E8u
#define SHELL_COL_MENU_BG 0x00F8FAFCu
#define SHELL_COL_MENU_SIDE 0x00E8EEF4u
#define SHELL_COL_MENU_HILITE 0x00D6E8FFu
#define SHELL_COL_PANEL 0x00FFFFFFu
#define SHELL_COL_PANEL_ALT 0x00F1F5FAu
#define SHELL_COL_TEXT 0x00172533u
#define SHELL_COL_MUTED 0x005A6570u
#define SHELL_COL_SHADOW 0x00080C14u
#define SHELL_COL_PULSE 0x00FFFFFFu

struct ShellWin {
    u8 used;
    u8 type;
    u8 flags;
    u8 tab;
    i32 x;
    i32 y;
    u32 w;
    u32 h;
    i32 save_x;
    i32 save_y;
    u32 save_w;
    u32 save_h;
    char title[20];
};

struct ShellApp {
    u8 id;
    u8 win_type;
    u8 action;
    UiSpriteId icon;
    const char *name;
    const char *desc;
};

static const char ui_txt_files[] = "FILES";
static const char ui_txt_about[] = "ABOUT";
static const char ui_txt_apps[] = "APPS";
static const char ui_txt_log[] = "LOG";
static const char ui_txt_preview[] = "PREVIEW";
static const char ui_txt_path[] = "C:/LEONOS";
static const char ui_txt_select_file[] = "SELECT A FILE";
static const char ui_txt_open[] = "OPEN:";
static const char ui_txt_no_file[] = "NO FILE SELECTED";
static const char ui_txt_use_files[] = "USE FILES TAB";
static const char ui_txt_app[] = "APP:";
static const char ui_txt_fat32_ready[] = "FAT32 HDD READY";
static const char ui_txt_fat12_ready[] = "FAT12 READY";
static const char ui_txt_leonos_desktop[] = "LEONOS DESKTOP";
static const char ui_txt_kernel[] = "KERNEL";
static const char ui_txt_graphics[] = "GRAPHICS";
static const char ui_txt_32bit[] = "32-BIT PROTECTED MODE";
static const char ui_txt_fat32_rw[] = "FAT32 READ WRITE";
static const char ui_txt_backbuffer[] = "BACKBUFFER DESKTOP";
static const char ui_txt_leo1[] = "LEO1 RING0 + RING3";
static const char ui_txt_programs[] = "PROGRAMS";
static const char ui_txt_programs_desc[] = "ONE BROWSER APP";
static const char ui_txt_run[] = "RUN";
static const char ui_txt_open_badge[] = "OPEN";
static const char ui_txt_shell_log[] = "SHELL LOG";
static const char ui_txt_network[] = "BROWSER";
static const char ui_txt_network_desc[] = "REAL HTTPS";
static const char ui_txt_net_status[] = "STATUS";
static const char ui_txt_browser_url[] = "URL";
static const char ui_txt_browser_status[] = "STATUS";
static const char ui_txt_browser_line[] = "RESPONSE";
static const char ui_txt_browser_page[] = "PAGE";
static const char ui_txt_browser_info[] = "INFO";
static const char ui_txt_browser_source[] = "SOURCE";
static const char ui_txt_browser_location[] = "REDIRECT";
static const char ui_txt_browser_wait[] = "LOADING URL";
static const char ui_txt_net_mac[] = "MAC";
static const char ui_txt_net_ip[] = "IP";
static const char ui_txt_net_gateway[] = "GATEWAY";
static const char ui_txt_net_last[] = "LAST";
static const char ui_txt_net_rx[] = "RX FRAMES";
static const char ui_txt_net_tx[] = "TX FRAMES";
static const char ui_txt_net_arp[] = "ARP";
static const char ui_txt_net_icmp[] = "ICMP RX/TX";
static const char ui_txt_net_ready[] = "READY";
static const char ui_txt_net_no_nic[] = "NO RTL8139";
static const char ui_txt_mouse_on[] = "MOUSE ON";
static const char ui_txt_mouse_off[] = "MOUSE OFF";
static const char ui_txt_start_compact[] = "LEONOS START";
static const char ui_txt_leonos[] = "LEONOS";
static const char ui_txt_start_hint[] = "F3 apps  F8 run  F10 write  F11 browser";
static const char ui_msg_shell_key_start[] = "LeonOS shell key start";

static const char app_name_files[] = "Files";
static const char app_desc_files[] = "Browse FAT32 files";
static const char app_name_apps[] = "Programs";
static const char app_desc_apps[] = "Launch kernel apps";
static const char app_name_about[] = "About LeonOS";
static const char app_desc_about[] = "System build info";
static const char app_name_log[] = "Shell Log";
static const char app_desc_log[] = "Recent desktop events";
static const char app_name_net[] = "Browser";
static const char app_desc_net[] = "Open real HTTPS pages";
static const char app_name_hello[] = "Run HELLOAPP";
static const char app_desc_hello[] = "Ring-0 LEO1 app";
static const char app_name_uhello[] = "Run UHELLO";
static const char app_desc_uhello[] = "Ring-3 syscall app";
static const char app_name_write[] = "FAT32 Write";
static const char app_desc_write[] = "Create WRITE32.TXT";
static const char app_name_ugfx[] = "Run UGFX";
static const char app_desc_ugfx[] = "Ring-3 framebuffer app";
static const char app_name_ucdemo[] = "Run C Demo";
static const char app_desc_ucdemo[] = "Freestanding C user app";
static const char app_name_ubrowser[] = "LeonOS Browser";
static const char app_desc_ubrowser[] = "HTTPS browser launcher";
static const char app_name_unetrun[] = "NetSurf Runtime";
static const char app_desc_unetrun[] = "Port harness (yield/heap)";
static const char app_name_uweb[] = "User Web";
static const char app_desc_uweb[] = "HTTPS fetch + browser";
static const char app_name_netsurf[] = "NetSurf Port";
static const char app_desc_netsurf[] = "Real engine smoke app";
static const char app_name_ustream[] = "Net Stream";
static const char app_desc_ustream[] = "Chunked user HTTPS API";
static const char app_name_uqjs[] = "QuickJS Core";
static const char app_desc_uqjs[] = "Modern JS proof app";

static const struct ShellApp shell_apps[SHELL_APP_COUNT] = {
    { SHELL_APP_FILES, SHELL_WIN_FILES, SHELL_ACTION_NONE, UI_SPRITE_FOLDER,
      app_name_files, app_desc_files },
    { SHELL_APP_APPS, SHELL_WIN_APPS, SHELL_ACTION_NONE, UI_SPRITE_APPS,
      app_name_apps, app_desc_apps },
    { SHELL_APP_ABOUT, SHELL_WIN_ABOUT, SHELL_ACTION_NONE, UI_SPRITE_INFO,
      app_name_about, app_desc_about },
    { SHELL_APP_LOG, SHELL_WIN_LOG, SHELL_ACTION_NONE, UI_SPRITE_LOG,
      app_name_log, app_desc_log },
    { SHELL_APP_NET, SHELL_WIN_NET, SHELL_ACTION_NONE, UI_SPRITE_INFO,
      app_name_net, app_desc_net },
    { SHELL_APP_HELLO, SHELL_WIN_NONE, SHELL_ACTION_HELLO, UI_SPRITE_APPS,
      app_name_hello, app_desc_hello },
    { SHELL_APP_UHELLO, SHELL_WIN_NONE, SHELL_ACTION_UHELLO, UI_SPRITE_APPS,
      app_name_uhello, app_desc_uhello },
    { SHELL_APP_WRITE, SHELL_WIN_NONE, SHELL_ACTION_WRITE, UI_SPRITE_LOG,
      app_name_write, app_desc_write },
    { SHELL_APP_UGFX, SHELL_WIN_NONE, SHELL_ACTION_UGFX, UI_SPRITE_APPS,
      app_name_ugfx, app_desc_ugfx },
    { SHELL_APP_UCDEMO, SHELL_WIN_NONE, SHELL_ACTION_UCDEMO, UI_SPRITE_APPS,
      app_name_ucdemo, app_desc_ucdemo },
    { SHELL_APP_UBROWSER, SHELL_WIN_NONE, SHELL_ACTION_UBROWSER, UI_SPRITE_INFO,
      app_name_ubrowser, app_desc_ubrowser },
    { SHELL_APP_UNETRUN, SHELL_WIN_NONE, SHELL_ACTION_UNETRUN, UI_SPRITE_INFO,
      app_name_unetrun, app_desc_unetrun },
    { SHELL_APP_UWEB, SHELL_WIN_NONE, SHELL_ACTION_UWEB, UI_SPRITE_INFO,
      app_name_uweb, app_desc_uweb },
    { SHELL_APP_NETSURF, SHELL_WIN_NONE, SHELL_ACTION_NETSURF, UI_SPRITE_INFO,
      app_name_netsurf, app_desc_netsurf },
    { SHELL_APP_USTREAM, SHELL_WIN_NONE, SHELL_ACTION_USTREAM, UI_SPRITE_INFO,
      app_name_ustream, app_desc_ustream },
    { SHELL_APP_UQJS, SHELL_WIN_NONE, SHELL_ACTION_UQJS, UI_SPRITE_INFO,
      app_name_uqjs, app_desc_uqjs },
};

static const u8 shell_start_app_ids[SHELL_APP_START_COUNT] = {
    SHELL_APP_FILES,
    SHELL_APP_APPS,
    SHELL_APP_NET,
    SHELL_APP_ABOUT,
    SHELL_APP_LOG
};

static const u8 shell_program_app_ids[SHELL_APP_PROGRAM_COUNT] = {
    SHELL_APP_NET
};

static struct ShellWin shell_wins[SHELL_WIN_MAX];
static u8 shell_z[SHELL_WIN_MAX];
static u8 shell_z_count;
static u8 shell_focus_idx;
static u8 shell_start_open;
static u8 shell_dragging;
static u8 shell_drag_win;
static u8 shell_drag_moved;
static u8 shell_key_consumed;
static u8 shell_browser_info_open;
static u8 shell_browser_url_editing;
static u16 shell_browser_url_edit_len;
static char shell_browser_url_edit[NET_BROWSER_RESOURCE_URL_MAX];

static i32 shell_drag_off_x;
static i32 shell_drag_off_y;
static u8 shell_hover_id;
static u8 shell_k_ctrl;
static u8 shell_k_alt;
static u8 shell_k_shift;
static char shell_log_lines[SHELL_LOG_MAX][48];
static u8 shell_log_count;
static u8 shell_pulse;

static struct {
    u32 taskbar_y;
    u32 taskbar_h;
    u32 title_h;
    u32 tab_h;
    u32 btn_w;
    u32 btn_h;
    u32 start_menu_h;
    u32 start_menu_w;
    u32 menu_row_h;
    u32 menu_header_h;
    u8 compact;
} shellm;

static u8 shell_compact_mode(void)
{
    return g_boot->framebuffer.height <= 720u;
}

static void shell_ui_pulse(void)
{
    shell_pulse = 90u;
}

static void shell_metrics_init(void)
{
    shellm.compact = shell_compact_mode();
    shellm.taskbar_h = shellm.compact ? sy(52u) : sy(48u);
    if (shellm.taskbar_h < 40u) {
        shellm.taskbar_h = 40u;
    }
    shellm.taskbar_y = g_boot->framebuffer.height - shellm.taskbar_h;

    shellm.title_h = shellm.compact ? sy(48u) : sy(42u);
    if (shellm.title_h < 36u) {
        shellm.title_h = 36u;
    }
    shellm.tab_h = shellm.compact ? sy(36u) : sy(32u);
    if (shellm.tab_h < 28u) {
        shellm.tab_h = 28u;
    }
    shellm.btn_w = shellm.compact ? sx(72u) : sx(54u);
    if (shellm.btn_w < 40u) {
        shellm.btn_w = 40u;
    }
    shellm.btn_h = shellm.title_h - 8u;
    if (shellm.btn_h < 28u) {
        shellm.btn_h = 28u;
    }
    shellm.menu_row_h = shellm.compact ? sy(58u) : sy(46u);
    if (shellm.menu_row_h < 38u) {
        shellm.menu_row_h = 38u;
    }
    shellm.menu_header_h = shellm.compact ? sy(86u) : sy(64u);
    if (shellm.menu_header_h < 58u) {
        shellm.menu_header_h = 58u;
    }
    shellm.start_menu_w = shellm.compact ? sx(520u) : sx(420u);
    if (shellm.start_menu_w < 320u) {
        shellm.start_menu_w = 320u;
    }
    if (shellm.start_menu_w > g_boot->framebuffer.width - sx(12u)) {
        shellm.start_menu_w = g_boot->framebuffer.width - sx(12u);
    }
    shellm.start_menu_h = shellm.menu_header_h + shellm.menu_row_h * SHELL_APP_START_COUNT + sy(12u);
}

static void shell_files_default_rect(i32 *out_x, i32 *out_y, u32 *out_w, u32 *out_h)
{
    u32 margin_x = sx(12u);
    u32 margin_y = sy(10u);
    u32 margin_bottom = sy(8u);
    *out_x = (i32) margin_x;
    *out_y = (i32) margin_y;
    *out_w = g_boot->framebuffer.width - margin_x * 2u;
    *out_h = shellm.taskbar_y - margin_y - margin_bottom;
    if (g_boot->framebuffer.height >= 1000u) {
        u32 cap_w = (g_boot->framebuffer.width * 90u) / 100u;
        u32 cap_h = ((shellm.taskbar_y - margin_y) * 90u) / 100u;
        if (*out_w > cap_w) {
            *out_w = cap_w;
            *out_x = (i32) ((g_boot->framebuffer.width - cap_w) / 2u);
        }
        if (*out_h > cap_h) {
            *out_h = cap_h;
        }
    }
}

static void shell_log_push(const char *line)
{
    if (shell_log_count == SHELL_LOG_MAX) {
        for (u8 row = 1u; row < SHELL_LOG_MAX; row += 1) {
            for (u8 col = 0u; col < 48u; col += 1) {
                shell_log_lines[row - 1u][col] = shell_log_lines[row][col];
            }
        }
        shell_log_count -= 1u;
    }
    u32 i = 0;
    for (; line[i] != 0 && i < 47u; i += 1) {
        shell_log_lines[shell_log_count][i] = line[i];
    }
    shell_log_lines[shell_log_count][i] = 0;
    shell_log_count += 1;
}

static void shell_serial_event(enum ShellSerialMsg msg)
{
    shell_serial_emit(msg);
    shell_log_push(shell_serial_msgs[(u32) msg]);
}

static void shell_win_title(u8 type, char *out)
{
    if (type == SHELL_WIN_FILES) {
        copy_name(out, ui_txt_files);
    } else if (type == SHELL_WIN_ABOUT) {
        copy_name(out, ui_txt_about);
    } else if (type == SHELL_WIN_APPS) {
        copy_name(out, ui_txt_apps);
    } else if (type == SHELL_WIN_LOG) {
        copy_name(out, ui_txt_log);
    } else if (type == SHELL_WIN_NET) {
        copy_name(out, ui_txt_network);
    } else {
        out[0] = 0;
    }
}

static UiSpriteId shell_win_icon(u8 type)
{
    if (type == SHELL_WIN_FILES) {
        return UI_SPRITE_FOLDER;
    }
    if (type == SHELL_WIN_ABOUT) {
        return UI_SPRITE_INFO;
    }
    if (type == SHELL_WIN_APPS) {
        return UI_SPRITE_APPS;
    }
    if (type == SHELL_WIN_LOG) {
        return UI_SPRITE_LOG;
    }
    if (type == SHELL_WIN_NET) {
        return UI_SPRITE_INFO;
    }
    return UI_SPRITE_FOLDER;
}

static u8 shell_win_slot(void)
{
    for (u8 i = 0; i < SHELL_WIN_MAX; i += 1) {
        if (!shell_wins[i].used) {
            return i;
        }
    }
    return 0xFFu;
}

static void shell_raise(u8 index)
{
    u8 pos = 0xFFu;
    for (u8 i = 0; i < shell_z_count; i += 1) {
        if (shell_z[i] == index) {
            pos = i;
            break;
        }
    }
    if (pos == 0xFFu) {
        return;
    }
    for (u8 i = pos; i + 1u < shell_z_count; i += 1) {
        shell_z[i] = shell_z[i + 1u];
    }
    shell_z[shell_z_count - 1u] = index;
}

static void shell_focus(u8 index)
{
    if (!shell_wins[index].used ||
        (shell_wins[index].flags & SHELL_WF_MINIMIZED) != 0) {
        return;
    }
    shell_focus_idx = index;
    shell_raise(index);
    dirty = 1;
}

static u8 shell_top_visible_window(void)
{
    for (i32 zi = (i32) shell_z_count - 1; zi >= 0; zi -= 1) {
        u8 wi = shell_z[(u8) zi];
        if (shell_wins[wi].used &&
            (shell_wins[wi].flags & SHELL_WF_VISIBLE) != 0 &&
            (shell_wins[wi].flags & SHELL_WF_MINIMIZED) == 0) {
            return wi;
        }
    }
    return 0xFFu;
}

static u8 shell_create(u8 type, i32 x, i32 y, u32 w, u32 h)
{
    u8 index = shell_win_slot();
    if (index == 0xFFu) {
        return 0xFFu;
    }
    struct ShellWin *win = &shell_wins[index];
    win->used = 1;
    win->type = type;
    win->flags = SHELL_WF_VISIBLE;
    win->tab = 0;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->save_x = x;
    win->save_y = y;
    win->save_w = w;
    win->save_h = h;
    shell_win_title(type, win->title);
    shell_z[shell_z_count++] = index;
    shell_focus(index);
    return index;
}

static void shell_init(void)
{
    shell_metrics_init();
    shell_z_count = 0;
    shell_start_open = 0;
    shell_dragging = 0;
    shell_drag_moved = 0;
    shell_hover_id = 0;
    shell_log_count = 0;

    i32 fx;
    i32 fy;
    u32 fw;
    u32 fh;
    shell_files_default_rect(&fx, &fy, &fw, &fh);
    u8 files = shell_create(SHELL_WIN_FILES, fx, fy, fw, fh);
    shell_focus(files);
}

static void ui_fill_round_rect(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 color);

static void shell_draw_vector_icon_colored(u32 x, u32 y, u32 w, u32 h, UiSpriteId id,
                                           u32 glyph, u32 ink, u32 accent)
{
    u32 cx = x + w / 2u;
    u32 cy = y + h / 2u;
    u32 size = (w < h ? w : h);
    if (size < 10u) {
        size = 10u;
    }

    if (id == UI_SPRITE_START || id == UI_SPRITE_APPS) {
        u32 pane = size / 4u;
        if (pane < 5u) {
            pane = 5u;
        }
        u32 gap = pane / 2u;
        if (gap < 3u) {
            gap = 3u;
        }
        u32 left = cx - pane - gap / 2u;
        u32 top = cy - pane - gap / 2u;
        fill_rect(left, top, pane, pane, glyph);
        fill_rect(left + pane + gap, top, pane, pane, glyph);
        fill_rect(left, top + pane + gap, pane, pane, glyph);
        fill_rect(left + pane + gap, top + pane + gap, pane, pane, glyph);
    } else if (id == UI_SPRITE_FOLDER) {
        u32 fw = (size * 7u) / 10u;
        u32 fh = (size * 5u) / 10u;
        u32 fx = cx - fw / 2u;
        u32 fy = cy - fh / 2u + 1u;
        fill_rect(fx, fy, fw / 2u, 3u, 0x00F9C846u);
        fill_rect(fx, fy + 3u, fw, fh, 0x00FFD95Au);
        fill_rect(fx, fy + fh, fw, 2u, 0x00D8A11Eu);
    } else if (id == UI_SPRITE_INFO) {
        u32 r = size / 3u;
        ui_fill_round_rect(cx - r, cy - r, r * 2u, r * 2u, r, accent);
        draw_char(cx - 3u, cy - 8u, 'I', glyph);
    } else if (id == UI_SPRITE_LOG) {
        u32 lw = (size * 6u) / 10u;
        u32 lh = 2u;
        u32 lx = cx - lw / 2u;
        fill_rect(lx, cy - 7u, lw, lh, ink);
        fill_rect(lx, cy - 1u, lw, lh, ink);
        fill_rect(lx, cy + 5u, lw, lh, ink);
        fill_rect(lx - 5u, cy - 7u, 2u, lh, accent);
        fill_rect(lx - 5u, cy - 1u, 2u, lh, accent);
        fill_rect(lx - 5u, cy + 5u, 2u, lh, accent);
    } else {
        u32 box = size / 2u;
        if (box < 8u) {
            box = 8u;
        }
        ui_fill_round_rect(cx - box / 2u, cy - box / 2u, box, box, 3u, glyph);
    }
}

static void shell_draw_vector_icon(u32 x, u32 y, u32 w, u32 h, UiSpriteId id)
{
    shell_draw_vector_icon_colored(x, y, w, h, id,
                                   0x00FFFFFFu, 0x002D3748u, 0x001A73E8u);
}

static void draw_text_clip(u32 x, u32 y, const char *text, u32 color, u32 max_px)
{
    u32 advance = gui_char_advance();
    u32 limit = x + max_px;
    for (u32 i = 0; text[i] != 0; i += 1) {
        if (x + i * advance + advance > limit) {
            break;
        }
        draw_char(x + i * advance, y, text[i], color);
    }
}

static u32 shell_brighten(u32 color)
{
    u32 r = ((color >> 16) & 0xFFu);
    u32 g = ((color >> 8) & 0xFFu);
    u32 b = (color & 0xFFu);
    if (r < 240u) {
        r += 16u;
    }
    if (g < 240u) {
        g += 16u;
    }
    if (b < 240u) {
        b += 16u;
    }
    return (r << 16) | (g << 8) | b;
}

static u32 shell_darken(u32 color)
{
    u32 r = ((color >> 16) & 0xFFu);
    u32 g = ((color >> 8) & 0xFFu);
    u32 b = (color & 0xFFu);
    r = r > 18u ? r - 18u : 0u;
    g = g > 18u ? g - 18u : 0u;
    b = b > 18u ? b - 18u : 0u;
    return (r << 16) | (g << 8) | b;
}

static u32 shell_mix(u32 a, u32 b, u32 b_weight)
{
    if (b_weight > 100u) {
        b_weight = 100u;
    }
    u32 a_weight = 100u - b_weight;
    u32 ar = (a >> 16) & 0xFFu;
    u32 ag = (a >> 8) & 0xFFu;
    u32 ab = a & 0xFFu;
    u32 br = (b >> 16) & 0xFFu;
    u32 bg = (b >> 8) & 0xFFu;
    u32 bb = b & 0xFFu;
    u32 r = (ar * a_weight + br * b_weight) / 100u;
    u32 g = (ag * a_weight + bg * b_weight) / 100u;
    u32 bl = (ab * a_weight + bb * b_weight) / 100u;
    return (r << 16) | (g << 8) | bl;
}

static void ui_fill_round_rect(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 color)
{
    if (radius < 2u) {
        radius = 2u;
    }
    if (radius * 2u > w) {
        radius = w / 2u;
    }
    if (radius * 2u > h) {
        radius = h / 2u;
    }
    fill_rect(x + radius, y, w - radius * 2u, h, color);
    fill_rect(x, y + radius, w, h - radius * 2u, color);
    fill_rect(x + 1u, y + 1u, radius, radius, color);
    fill_rect(x + w - radius - 1u, y + 1u, radius, radius, color);
    fill_rect(x + 1u, y + h - radius - 1u, radius, radius, color);
    fill_rect(x + w - radius - 1u, y + h - radius - 1u, radius, radius, color);
}

static void shell_draw_chrome_btn(u32 x, u32 y, u32 w, u32 h, u32 color, u8 hover, u8 pressed,
                                  UiSpriteId icon)
{
    u32 c = color;
    if (pressed) {
        c = shell_darken(color);
    } else if (hover) {
        c = shell_brighten(color);
    }
    if (w > 4u && h > 4u) {
        fill_rect(x + 2u, y + 3u, w - 2u, h - 2u, 0x00142638u);
    }
    ui_fill_round_rect(x, y, w, h, 5u, c);
    if (w > 4u && h > 4u) {
        fill_rect(x + 2u, y + 2u, w - 4u, 1u, shell_mix(c, 0x00FFFFFFu, 48u));
        fill_rect(x + 2u, y + h - 2u, w - 4u, 1u, shell_darken(c));
    }
    u32 glyph = 0x00FFFFFFu;
    u32 cx = x + w / 2u;
    u32 cy = y + h / 2u;
    u32 thick = 2u;

    if (icon == UI_SPRITE_CHROME_MIN) {
        u32 line_w = w / 3u;
        if (line_w < 10u) {
            line_w = 10u;
        }
        fill_rect(cx - line_w / 2u, cy + h / 7u, line_w, thick, glyph);
    } else if (icon == UI_SPRITE_CHROME_MAX) {
        u32 box = h / 3u;
        if (box < 10u) {
            box = 10u;
        }
        u32 bx = cx - box / 2u;
        u32 by = cy - box / 2u;
        fill_rect(bx, by, box, thick, glyph);
        fill_rect(bx, by + box - thick, box, thick, glyph);
        fill_rect(bx, by, thick, box, glyph);
        fill_rect(bx + box - thick, by, thick, box, glyph);
    } else if (icon == UI_SPRITE_CHROME_CLOSE) {
        u32 size = h / 3u;
        if (size < 11u) {
            size = 11u;
        }
        i32 start_x = (i32) (cx - size / 2u);
        i32 start_y = (i32) (cy - size / 2u);
        for (u32 i = 0u; i < size; i += 1u) {
            fill_rect((u32) (start_x + (i32) i), (u32) (start_y + (i32) i), thick, thick, glyph);
            fill_rect((u32) (start_x + (i32) (size - 1u - i)), (u32) (start_y + (i32) i),
                      thick, thick, glyph);
        }
    }
}

static u32 shell_btn_x(const struct ShellWin *win, u8 which)
{
    u32 x = (u32) win->x + win->w - shellm.btn_w * (3u - which);
    return x;
}

static void shell_draw_titlebar(const struct ShellWin *win, u8 focused)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y;
    u32 w = win->w;
    u32 h = shellm.title_h;
    u32 title_color = focused ? SHELL_COL_TITLE_FOCUS : SHELL_COL_TITLE_IDLE;

    fill_rect(x + 5u, y + 5u, w - 2u, h + 1u, SHELL_COL_SHADOW);
    fill_rect(x + 2u, y + h, w, 3u, 0x00111A28u);
    ui_fill_round_rect(x, y, w, h, 6u, title_color);
    fill_rect(x + 2u, y + 1u, w - 4u, 1u, shell_mix(title_color, 0x00FFFFFFu, 34u));
    fill_rect(x, y + h - 3u, w, 3u, focused ? 0x00155CB0u : 0x002E3848u);
    shell_draw_vector_icon(x + sx(8u), y, sx(28u), h, shell_win_icon(win->type));
    draw_text_clip(x + sx(38u), y + (h - gui_line_height()) / 2u, win->title,
                   0x00FFFFFFu, w - shellm.btn_w * 3u - sx(48u));

    u8 hover_min = (shell_hover_id == (u8) (0x10u | (win - shell_wins))) ? 1u : 0u;
    u8 hover_max = (shell_hover_id == (u8) (0x20u | (win - shell_wins))) ? 1u : 0u;
    u8 hover_close = (shell_hover_id == (u8) (0x30u | (win - shell_wins))) ? 1u : 0u;
    u32 by = y + (h - shellm.btn_h) / 2u;
    shell_draw_chrome_btn(shell_btn_x(win, 0), by, shellm.btn_w, shellm.btn_h,
                          SHELL_COL_MIN, hover_min, 0, UI_SPRITE_CHROME_MIN);
    shell_draw_chrome_btn(shell_btn_x(win, 1), by, shellm.btn_w, shellm.btn_h,
                          SHELL_COL_MAX, hover_max, 0, UI_SPRITE_CHROME_MAX);
    shell_draw_chrome_btn(shell_btn_x(win, 2), by, shellm.btn_w, shellm.btn_h,
                          SHELL_COL_CLOSE, hover_close, 0, UI_SPRITE_CHROME_CLOSE);
}

static void shell_files_sidebar_w(u32 client_w, u8 tab, u32 *sidebar_w, u32 *content_x)
{
    if (tab != 0u) {
        *sidebar_w = 0;
        *content_x = 0;
        return;
    }
    *sidebar_w = (client_w * 36u) / 100u;
    if (*sidebar_w < sx(140u)) {
        *sidebar_w = sx(140u);
    }
    if (*sidebar_w > sx(280u)) {
        *sidebar_w = sx(280u);
    }
    if (*sidebar_w + sx(120u) > client_w) {
        *sidebar_w = client_w - sx(120u);
    }
    *content_x = *sidebar_w + sx(2u);
}

static void shell_draw_files_client(const struct ShellWin *win)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h + shellm.tab_h;
    u32 w = win->w;
    u32 status_h = gui_line_height() + sy(10u);
    if (status_h < 24u) {
        status_h = 24u;
    }
    u32 h = win->h - shellm.title_h - shellm.tab_h - status_h;
    u32 client_y = y;
    u32 addr_h = gui_line_height() + sy(10u);
    if (addr_h < 28u) {
        addr_h = 28u;
    }
    u32 body_y = client_y + addr_h;
    u32 body_h = h > addr_h ? h - addr_h : sy(100u);
    u32 sidebar_w;
    u32 content_x;
    shell_files_sidebar_w(w, win->tab, &sidebar_w, &content_x);

    fill_rect(x, client_y, w, addr_h, 0x00E8EEF2u);
    fill_rect(x, client_y + addr_h - 1u, w, 1u, 0x00D3DDE8u);
    ui_fill_round_rect(x + sx(8u), client_y + sy(5u), w - sx(16u),
                       addr_h > sy(10u) ? addr_h - sy(10u) : addr_h,
                       4u, 0x00FFFFFFu);
    draw_text_clip(x + sx(10u), client_y + (addr_h - gui_line_height()) / 2u,
                   ui_txt_path, SHELL_COL_TEXT, w - sx(20u));

    if (win->tab == 0u) {
        fill_rect(x, body_y, sidebar_w, body_h, SHELL_COL_SIDEBAR);
        fill_rect(x + sidebar_w - 1u, body_y, 1u, body_h, 0x00D1DBE8u);
        if (content_x + sx(80u) < w) {
            fill_rect(x + content_x, body_y, w - content_x, body_h, SHELL_COL_CLIENT);
            ui_fill_round_rect(x + content_x + sx(12u), body_y + sy(54u),
                               w - content_x - sx(24u), sy(86u), 6u, SHELL_COL_PANEL);
            fill_rect(x + content_x + sx(12u), body_y + sy(54u),
                      w - content_x - sx(24u), 1u, 0x00E1E8F0u);
            draw_text_clip(x + content_x + sx(12u), body_y + sy(20u),
                           ui_txt_select_file, SHELL_COL_MUTED, w - content_x - sx(24u));
        }
        u32 list_y = body_y + sy(8u);
        u32 row_h = gui_line_height() + sy(8u);
        if (row_h < 26u) {
            row_h = 26u;
        }
        u32 list_bottom = body_y + body_h - sy(4u);
        u32 text_max = sidebar_w > sx(16u) ? sidebar_w - sx(16u) : sidebar_w;
        for (u32 i = 0; i < file_count; i += 1) {
            u32 ry = list_y + i * row_h;
            if (ry + row_h > list_bottom) {
                break;
            }
            if (i == selected_file) {
                ui_fill_round_rect(x + sx(4u), ry - 1u, sidebar_w - sx(8u),
                                   row_h - 2u, 5u, 0x00CCE8FFu);
            } else if ((i & 1u) != 0u) {
                fill_rect(x + sx(4u), ry, sidebar_w - sx(8u), row_h - 2u, 0x00F6F8FBu);
            }
            draw_text_clip(x + sx(10u), ry, files[i].name, SHELL_COL_TEXT, text_max);
        }
    } else {
        fill_rect(x, body_y, w, body_h, SHELL_COL_CLIENT);
        ui_fill_round_rect(x + sx(12u), body_y + sy(34u), w - sx(24u),
                           body_h > sy(82u) ? body_h - sy(82u) : body_h,
                           6u, SHELL_COL_PANEL);
        if (file_loaded) {
            draw_text_clip(x + sx(12u), body_y + sy(10u), ui_txt_open, SHELL_COL_TEXT, sx(80u));
            draw_text_clip(x + sx(70u), body_y + sy(10u), open_name, SHELL_COL_TITLE_FOCUS,
                           w - sx(90u));
            u32 px = x + sx(12u);
            u32 py = body_y + sy(40u);
            u32 line_step = sy(24u);
            if (line_step < 18u) {
                line_step = 18u;
            }
            u32 bottom = body_y + body_h - sy(8u);
            u32 right = x + w - sx(12u);
            for (u32 i = 0; i < file_buffer_size && i < 420u; i += 1) {
                char ch = (char) file_buffer[i];
                if (ch == '\r') {
                    continue;
                }
                if (ch == '\n') {
                    px = x + sx(12u);
                    py += line_step;
                    continue;
                }
                if ((u8) ch < 32u) {
                    ch = '.';
                }
                if (py > bottom) {
                    break;
                }
                draw_char(px, py, ch, SHELL_COL_TEXT);
                px += gui_char_advance();
                if (px > right) {
                    px = x + sx(12u);
                    py += line_step;
                }
            }
        } else {
            draw_text_clip(x + sx(12u), body_y + sy(40u),
                           ui_txt_no_file, SHELL_COL_TEXT, w - sx(24u));
            draw_text_clip(x + sx(12u), body_y + sy(72u),
                           ui_txt_use_files, SHELL_COL_MUTED, w - sx(24u));
        }
        if (app_message_set) {
            draw_text_clip(x + sx(12u), body_y + body_h - sy(36u), ui_txt_app,
                           SHELL_COL_TEXT, sx(50u));
            draw_text_clip(x + sx(56u), body_y + body_h - sy(36u), app_message,
                           0x0016A085u, w - sx(70u));
        }
    }

    u32 status_y = client_y + h;
    fill_rect(x, status_y, w, status_h, 0x00E8EEF2u);
    fill_rect(x, status_y, w, 1u, 0x00D3DDE8u);
    draw_text_clip(x + sx(10u), status_y + (status_h - gui_line_height()) / 2u,
                   using_fat32 ? ui_txt_fat32_ready : ui_txt_fat12_ready,
                   SHELL_COL_TEXT, w - sx(20u));
}

static void shell_draw_tabs(const struct ShellWin *win)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 th = shellm.tab_h;
    u32 tab_w = sx(140u);
    if (tab_w * 2u > w) {
        tab_w = w / 2u;
    }
    fill_rect(x, y, w, th, SHELL_COL_TAB_IDLE);
    fill_rect(x, y, w, 1u, 0x00EEF4FAu);
    ui_fill_round_rect(x + 2u, y + 2u, tab_w - 4u, th - 2u, 5u,
                       win->tab == 0u ? SHELL_COL_TAB_ACT : SHELL_COL_TAB_IDLE);
    ui_fill_round_rect(x + tab_w + 2u, y + 2u, tab_w - 4u, th - 2u, 5u,
                       win->tab == 1u ? SHELL_COL_TAB_ACT : SHELL_COL_TAB_IDLE);
    if (win->tab == 0u) {
        fill_rect(x + 2u, y + th - 4u, tab_w - 4u, 4u, SHELL_COL_TAB_ACCENT);
    } else {
        fill_rect(x + tab_w + 2u, y + th - 4u, tab_w - 4u, 4u, SHELL_COL_TAB_ACCENT);
    }
    fill_rect(x, y + th - 1u, w, 1u, SHELL_COL_BORDER);
    draw_text(x + sx(16u), y + (th - gui_line_height()) / 2u, ui_txt_files, SHELL_COL_TEXT);
    draw_text(x + tab_w + sx(16u), y + (th - gui_line_height()) / 2u,
              ui_txt_preview, SHELL_COL_TEXT);
}

static void shell_draw_about_client(const struct ShellWin *win)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 h = win->h - shellm.title_h;
    fill_rect(x, y, w, h, SHELL_COL_CLIENT);
    fill_rect(x, y, w, sy(52u), 0x00EAF2FFu);
    fill_rect(x, y + sy(52u) - 1u, w, 1u, 0x00D3DDE8u);
    shell_draw_vector_icon(x + sx(14u), y + sy(8u), sx(36u), sy(36u), UI_SPRITE_INFO);
    draw_text_clip(x + sx(58u), y + sy(16u), ui_txt_leonos_desktop,
                   SHELL_COL_TEXT, w - sx(72u));

    u32 card_x = x + sx(16u);
    u32 card_y = y + sy(72u);
    u32 card_w = w > sx(32u) ? w - sx(32u) : w;
    u32 card_h = sy(46u);
    if (card_h < 34u) {
        card_h = 34u;
    }
    for (u8 i = 0u; i < 4u; i += 1u) {
        const char *left = ui_txt_kernel;
        const char *right = ui_txt_32bit;
        if (i == 1u) {
            left = ui_txt_files;
            right = ui_txt_fat32_rw;
        } else if (i == 2u) {
            left = ui_txt_graphics;
            right = ui_txt_backbuffer;
        } else if (i == 3u) {
            left = ui_txt_apps;
            right = ui_txt_leo1;
        }
        ui_fill_round_rect(card_x, card_y, card_w, card_h, 5u,
                           (i & 1u) ? SHELL_COL_PANEL_ALT : SHELL_COL_PANEL);
        fill_rect(card_x, card_y + card_h - 1u, card_w, 1u, SHELL_COL_BORDER);
        draw_text_clip(card_x + sx(12u), card_y + (card_h - gui_line_height()) / 2u,
                       left, 0x001A73E8u, sx(120u));
        draw_text_clip(card_x + sx(150u), card_y + (card_h - gui_line_height()) / 2u,
                       right, SHELL_COL_TEXT, card_w - sx(164u));
        card_y += card_h + sy(8u);
    }
}

static u32 shell_apps_row_h(void)
{
    u32 row_h = gui_line_height() + (shellm.compact ? sy(4u) : sy(22u));
    u32 min_h = shellm.compact ? 44u : 50u;
    if (row_h < min_h) {
        row_h = min_h;
    }
    return row_h;
}

static u32 shell_apps_row_gap(void)
{
    u32 gap = shellm.compact ? sy(4u) : sy(6u);
    if (gap < 3u) {
        gap = 3u;
    }
    return gap;
}

static u8 shell_apps_row_at(const struct ShellWin *win, i32 mx, i32 my)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 row_x = x + sx(12u);
    u32 row_w = win->w > sx(24u) ? win->w - sx(24u) : win->w;
    u32 row_y = y + (shellm.compact ? sy(42u) : sy(56u));
    u32 row_h = shell_apps_row_h();
    u32 stride = row_h + shell_apps_row_gap();
    if (mx < (i32) row_x || mx >= (i32) (row_x + row_w) || my < (i32) row_y) {
        return 0xFFu;
    }
    u32 rel = (u32) my - row_y;
    u32 row = rel / stride;
    if (row >= SHELL_APP_PROGRAM_COUNT || (rel - row * stride) >= row_h) {
        return 0xFFu;
    }
    return (u8) row;
}

static void shell_draw_apps_client(const struct ShellWin *win)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 h = win->h - shellm.title_h;
    fill_rect(x, y, w, h, SHELL_COL_CLIENT);
    fill_rect(x, y, w, shellm.compact ? sy(42u) : sy(52u), 0x00EAF2FFu);
    fill_rect(x, y + (shellm.compact ? sy(42u) : sy(52u)) - 1u,
              w, 1u, 0x00D3DDE8u);
    shell_draw_vector_icon_colored(x + sx(12u), y + sy(8u), sx(32u), sy(32u),
                                   UI_SPRITE_APPS, 0x001A73E8u, 0x002D3748u, 0x001A73E8u);
    draw_text(x + sx(52u), y + sy(14u), ui_txt_programs, SHELL_COL_TEXT);
    if (!shellm.compact) {
        draw_text_clip(x + sx(190u), y + sy(14u), ui_txt_programs_desc,
                       SHELL_COL_MUTED, w - sx(210u));
    }

    u32 row_h = shell_apps_row_h();
    u32 gap = shell_apps_row_gap();
    u32 row_x = x + sx(12u);
    u32 row_w = w > sx(24u) ? w - sx(24u) : w;
    u32 row_y = y + (shellm.compact ? sy(42u) : sy(56u));
    for (u8 i = 0u; i < SHELL_APP_PROGRAM_COUNT; i += 1u) {
        if (row_y + row_h > y + h - sy(8u)) {
            break;
        }
        const struct ShellApp *app = &shell_apps[shell_program_app_ids[i]];
        u32 bg = (i & 1u) ? 0x00EEF4FBu : SHELL_COL_PANEL;
        ui_fill_round_rect(row_x, row_y, row_w, row_h, 6u, bg);
        fill_rect(row_x, row_y + row_h - 1u, row_w, 1u, SHELL_COL_BORDER);
        shell_draw_vector_icon_colored(row_x + sx(10u), row_y, sx(36u), row_h,
                                       app->icon, 0x001A73E8u,
                                       0x002D3748u, 0x001A73E8u);
        draw_text_clip(row_x + sx(56u), row_y + (row_h - gui_line_height()) / 2u,
                       app->name, SHELL_COL_TEXT, row_w - sx(150u));
        if (!shellm.compact) {
            draw_text_clip(row_x + sx(230u), row_y + (row_h - gui_line_height()) / 2u,
                           app->desc, SHELL_COL_MUTED, row_w - sx(330u));
        }
        ui_fill_round_rect(row_x + row_w - sx(82u), row_y + (row_h - sy(28u)) / 2u,
                           sx(70u), sy(28u), 5u, 0x00E8F1FFu);
        draw_text_clip(row_x + row_w - sx(74u), row_y + (row_h - gui_line_height()) / 2u,
                       app->win_type == SHELL_WIN_NONE ? ui_txt_run : ui_txt_open_badge,
                       0x001A73E8u, sx(66u));
        row_y += row_h + gap;
    }
}

static void shell_draw_log_client(const struct ShellWin *win)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 h = win->h - shellm.title_h;
    fill_rect(x, y, w, h, SHELL_COL_CLIENT);
    fill_rect(x, y, w, sy(48u), 0x00EAF2FFu);
    fill_rect(x, y + sy(48u) - 1u, w, 1u, 0x00D3DDE8u);
    shell_draw_vector_icon(x + sx(12u), y + sy(8u), sx(32u), sy(32u), UI_SPRITE_LOG);
    draw_text(x + sx(52u), y + sy(14u), ui_txt_shell_log, SHELL_COL_TEXT);
    for (u32 i = 0; i < shell_log_count; i += 1) {
        u32 ry = y + sy(58u) + i * sy(30u);
        ui_fill_round_rect(x + sx(12u), ry - sy(4u), w - sx(24u), sy(28u),
                           5u, (i & 1u) ? SHELL_COL_PANEL_ALT : SHELL_COL_PANEL);
        draw_text_clip(x + sx(20u), ry,
                       shell_log_lines[i], SHELL_COL_TEXT, w - sx(32u));
    }
}

static void shell_draw_net_row_text(u32 x, u32 y, u32 w, const char *label, const char *value)
{
    u32 h = gui_line_height() + sy(12u);
    if (h < 30u) {
        h = 30u;
    }
    ui_fill_round_rect(x, y, w, h, 5u, SHELL_COL_PANEL);
    fill_rect(x, y + h - 1u, w, 1u, SHELL_COL_BORDER);
    draw_text_clip(x + sx(12u), y + (h - gui_line_height()) / 2u, label,
                   SHELL_COL_TITLE_FOCUS, sx(150u));
    draw_text_clip(x + sx(168u), y + (h - gui_line_height()) / 2u, value,
                   SHELL_COL_TEXT, w - sx(184u));
}

static u32 shell_browser_wrapped_line_count(const char *text, u32 cols)
{
    if (cols == 0u) {
        return 0u;
    }
    u32 lines = 0u;
    u32 i = 0u;
    while (text[i] != 0) {
        while (text[i] == ' ') {
            i += 1u;
        }
        u32 len = 0u;
        u32 last_space = 0xFFFFFFFFu;
        u32 start_i = i;
        while (text[i] != 0 && text[i] != '\n' && len < cols) {
            if (text[i] == ' ') {
                last_space = len;
            }
            len += 1u;
            i += 1u;
        }
        if (text[i] != 0 && text[i] != '\n' &&
            len >= cols && last_space != 0xFFFFFFFFu && last_space > 0u) {
            u32 rewind = len - last_space - 1u;
            i -= rewind;
        }
        if (len == 0u && text[i] == '\n') {
            i += 1u;
        }
        lines += 1u;
        if (text[i] == '\n') {
            i += 1u;
        }
        if (i == start_i && text[i] != 0) {
            i += 1u;
        }
    }
    if (lines == 0u) {
        lines = 1u;
    }
    return lines;
}

static const char *shell_browser_title(const struct ShellWin *win)
{
    (void) win;
    for (u32 i = 0u; i < net_browser_render_count; i += 1u) {
        if (net_browser_render_kind[i] == NET_BROWSER_RENDER_KIND_TITLE &&
            net_browser_render_text[i][0] != 0) {
            return net_browser_render_text[i];
        }
    }
    return "Browser";
}

static const char *shell_browser_text(const struct ShellWin *win)
{
    (void) win;
    return net_browser_text;
}

static u32 *shell_browser_scroll_ref(const struct ShellWin *win)
{
    (void) win;
    return &net_browser_scroll;
}

static const char *shell_browser_url(const struct ShellWin *win)
{
    (void) win;
    if (shell_browser_url_editing) {
        return shell_browser_url_edit;
    }
    return net_browser_current_url;
}

static void shell_browser_begin_url_edit(u8 clear)
{
    shell_browser_url_edit_len = 0u;
    if (!clear) {
        for (u32 i = 0u;
             net_browser_current_url[i] != 0 &&
             shell_browser_url_edit_len < NET_BROWSER_RESOURCE_URL_MAX - 1u;
             i += 1u) {
            shell_browser_url_edit[shell_browser_url_edit_len++] =
                net_browser_current_url[i];
        }
    }
    shell_browser_url_edit[shell_browser_url_edit_len] = 0;
    shell_browser_url_editing = 1u;
    dirty = 1;
}

static void shell_browser_url_append(char ch)
{
    if (shell_browser_url_edit_len >= NET_BROWSER_RESOURCE_URL_MAX - 1u) {
        return;
    }
    shell_browser_url_edit[shell_browser_url_edit_len++] = ch;
    shell_browser_url_edit[shell_browser_url_edit_len] = 0;
}

static char shell_browser_url_scancode_char(u8 scancode)
{
    static const char row_q[] = "qwertyuiop";
    static const char row_a[] = "asdfghjkl";
    static const char row_z[] = "zxcvbnm";
    if (scancode >= 0x10u && scancode <= 0x19u) {
        char ch = row_q[scancode - 0x10u];
        return shell_k_shift ? (char) (ch - ('a' - 'A')) : ch;
    }
    if (scancode >= 0x1Eu && scancode <= 0x26u) {
        char ch = row_a[scancode - 0x1Eu];
        return shell_k_shift ? (char) (ch - ('a' - 'A')) : ch;
    }
    if (scancode >= 0x2Cu && scancode <= 0x32u) {
        char ch = row_z[scancode - 0x2Cu];
        return shell_k_shift ? (char) (ch - ('a' - 'A')) : ch;
    }
    if (scancode >= 0x02u && scancode <= 0x0Bu) {
        static const char digits[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return shell_k_shift ? shifted[scancode - 0x02u]
                             : digits[scancode - 0x02u];
    }
    if (scancode == 0x0Cu) { return shell_k_shift ? '_' : '-'; }
    if (scancode == 0x0Du) { return shell_k_shift ? '+' : '='; }
    if (scancode == 0x1Au) { return shell_k_shift ? '{' : '['; }
    if (scancode == 0x1Bu) { return shell_k_shift ? '}' : ']'; }
    if (scancode == 0x27u) { return shell_k_shift ? ':' : ';'; }
    if (scancode == 0x28u) { return shell_k_shift ? '"' : '\''; }
    if (scancode == 0x29u) { return shell_k_shift ? '~' : '`'; }
    if (scancode == 0x2Bu) { return shell_k_shift ? '|' : '\\'; }
    if (scancode == 0x33u) { return shell_k_shift ? '<' : ','; }
    if (scancode == 0x34u) { return shell_k_shift ? '>' : '.'; }
    if (scancode == 0x35u) { return shell_k_shift ? '?' : '/'; }
    return 0;
}

static u8 shell_browser_url_key(u8 scancode)
{
    if (!shell_browser_url_editing) {
        return 0u;
    }
    if (scancode == 0x01u) {
        shell_browser_url_editing = 0u;
        dirty = 1;
        return 1u;
    }
    if (scancode == 0x1Cu) {
        shell_browser_url_editing = 0u;
        net_browser_open_url(shell_browser_url_edit);
        return 1u;
    }
    if (scancode == 0x0Eu) {
        if (shell_browser_url_edit_len != 0u) {
            shell_browser_url_edit_len -= 1u;
            shell_browser_url_edit[shell_browser_url_edit_len] = 0;
        }
        dirty = 1;
        return 1u;
    }
    char ch = shell_browser_url_scancode_char(scancode);
    if (ch != 0) {
        shell_browser_url_append(ch);
        dirty = 1;
        return 1u;
    }
    return 1u;
}

static const char *shell_browser_status_text(const struct ShellWin *win)
{
    (void) win;
    return net_browser_state;
}

static const char *shell_browser_response_text(const struct ShellWin *win)
{
    (void) win;
    return net_browser_text_ready
        ? net_browser_line
        : net_last_event;
}

static u32 shell_browser_toolbar_h(void)
{
    u32 h = gui_line_height() + sy(26u);
    if (h < 46u) {
        h = 46u;
    }
    return h;
}

static u32 shell_browser_button_size(void)
{
    u32 toolbar_h = shell_browser_toolbar_h();
    u32 b = toolbar_h > sy(14u) ? toolbar_h - sy(14u) : toolbar_h;
    if (b < 30u) {
        b = 30u;
    }
    if (b > 42u) {
        b = 42u;
    }
    return b;
}

static void shell_draw_browser_button(u32 x, u32 y, u32 s, char icon, u8 active, u8 selected)
{
    u32 bg = active ? 0x00FFFFFFu : 0x00E6EBF2u;
    if (selected) {
        bg = 0x00D6E8FFu;
    }
    ui_fill_round_rect(x, y, s, s, 5u, bg);
    fill_rect(x, y + s - 1u, s, 1u, SHELL_COL_BORDER);
    u32 ink = active ? SHELL_COL_TEXT : SHELL_COL_MUTED;
    if (icon == '<') {
        u32 cx = x + s / 2u;
        u32 cy = y + s / 2u;
        fill_rect(cx - 5u, cy, 12u, 2u, ink);
        fill_rect(cx - 6u, cy - 1u, 2u, 2u, ink);
        fill_rect(cx - 4u, cy - 3u, 2u, 2u, ink);
        fill_rect(cx - 2u, cy - 5u, 2u, 2u, ink);
        fill_rect(cx - 4u, cy + 3u, 2u, 2u, ink);
        fill_rect(cx - 2u, cy + 5u, 2u, 2u, ink);
    } else if (icon == '>') {
        u32 cx = x + s / 2u;
        u32 cy = y + s / 2u;
        fill_rect(cx - 7u, cy, 12u, 2u, ink);
        fill_rect(cx + 4u, cy - 1u, 2u, 2u, ink);
        fill_rect(cx + 2u, cy - 3u, 2u, 2u, ink);
        fill_rect(cx, cy - 5u, 2u, 2u, ink);
        fill_rect(cx + 2u, cy + 3u, 2u, 2u, ink);
        fill_rect(cx, cy + 5u, 2u, 2u, ink);
    } else {
        draw_char(x + (s - gui_char_advance()) / 2u,
                  y + (s - gui_line_height()) / 2u,
                  icon, ink);
    }
}

static u32 shell_browser_render_gap(u8 kind, u8 flags)
{
    if ((flags & (NET_BROWSER_RENDER_FLAG_BOX |
                  NET_BROWSER_RENDER_FLAG_INPUT |
                  NET_BROWSER_RENDER_FLAG_BUTTON)) != 0u) {
        return 1u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_HEADING ||
        kind == NET_BROWSER_RENDER_KIND_TITLE ||
        kind == NET_BROWSER_RENDER_KIND_IMAGE ||
        kind == NET_BROWSER_RENDER_KIND_TABLE ||
        kind == NET_BROWSER_RENDER_KIND_QUOTE ||
        kind == NET_BROWSER_RENDER_KIND_META ||
        kind == NET_BROWSER_RENDER_KIND_EMBED ||
        kind == NET_BROWSER_RENDER_KIND_DIALOG) {
        return 1u;
    }
    return 0u;
}

static u32 shell_browser_image_render_lines(u8 kind, u8 image_index)
{
    if (kind == NET_BROWSER_RENDER_KIND_IMAGE &&
        image_index < NET_BROWSER_IMAGE_MAX &&
        net_browser_image_status[image_index] == NET_BROWSER_IMAGE_STATUS_DECODED) {
        return 4u;
    }
    return 0u;
}

static u32 shell_browser_css_block_lines(u8 slot, u32 line_h)
{
    if (slot < NET_BROWSER_RENDER_MAX &&
        (net_browser_render_css_flags[slot] & NET_BROWSER_CSS_STYLE_HEIGHT) != 0u &&
        line_h != 0u) {
        u32 h = net_browser_render_css_height[slot];
        if (h > line_h) {
            u32 lines = (h + line_h - 1u) / line_h;
            if (lines > 8u) {
                lines = 8u;
            }
            return lines;
        }
    }
    return 1u;
}

static u32 shell_browser_render_total_lines(u32 cols, u32 line_h)
{
    if (net_browser_render_count == 0u) {
        return shell_browser_wrapped_line_count(shell_browser_text(0), cols);
    }
    u32 lines = 0u;
    for (u32 b = 0u; b < net_browser_render_count; b += 1u) {
        if (net_browser_render_text[b][0] == 0 ||
            (net_browser_render_flags[b] & NET_BROWSER_RENDER_FLAG_HIDDEN) != 0u) {
            continue;
        }
        u32 image_lines = shell_browser_image_render_lines(net_browser_render_kind[b],
                                                           net_browser_render_image[b]);
        if (image_lines != 0u) {
            lines += image_lines;
        } else {
            lines += shell_browser_wrapped_line_count(net_browser_render_text[b], cols) *
                     shell_browser_css_block_lines((u8) b, line_h);
        }
        lines += shell_browser_render_gap(net_browser_render_kind[b],
                                          net_browser_render_flags[b]);
    }
    if (lines == 0u) {
        lines = 1u;
    }
    return lines;
}

static u32 shell_browser_render_color(u8 kind, u8 slot)
{
    if (slot < NET_BROWSER_RENDER_MAX &&
        (net_browser_render_css_flags[slot] & NET_BROWSER_CSS_STYLE_COLOR) != 0u) {
        return net_browser_render_css_color[slot];
    }
    if (kind == NET_BROWSER_RENDER_KIND_LINK) {
        return 0x001A5FB4u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_HEADING) {
        return 0x001D3557u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_TITLE ||
        kind == NET_BROWSER_RENDER_KIND_IMAGE) {
        return SHELL_COL_MUTED;
    }
    if (kind == NET_BROWSER_RENDER_KIND_TABLE) {
        return 0x001F5132u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_QUOTE) {
        return 0x003B4658u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_META) {
        return 0x00666B73u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_EMBED) {
        return 0x0041557Au;
    }
    if (kind == NET_BROWSER_RENDER_KIND_DIALOG) {
        return 0x002E3A59u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_CSS) {
        return 0x006A4C93u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_JS) {
        return 0x00724E00u;
    }
    return SHELL_COL_TEXT;
}

static u32 shell_text_pixel_width(const char *text, u32 max_px)
{
    u32 advance = gui_char_advance();
    u32 px = 0u;
    for (u32 i = 0u; text[i] != 0 && px + advance <= max_px; i += 1u) {
        px += advance;
    }
    return px;
}

static u8 shell_browser_render_is_block_like(u8 kind, u8 flags)
{
    return (flags & (NET_BROWSER_RENDER_FLAG_BOX |
                     NET_BROWSER_RENDER_FLAG_INPUT |
                     NET_BROWSER_RENDER_FLAG_BUTTON)) != 0u ||
           kind == NET_BROWSER_RENDER_KIND_HEADING ||
           kind == NET_BROWSER_RENDER_KIND_TITLE ||
           kind == NET_BROWSER_RENDER_KIND_IMAGE ||
           kind == NET_BROWSER_RENDER_KIND_TABLE ||
           kind == NET_BROWSER_RENDER_KIND_QUOTE ||
           kind == NET_BROWSER_RENDER_KIND_META ||
           kind == NET_BROWSER_RENDER_KIND_EMBED ||
           kind == NET_BROWSER_RENDER_KIND_DIALOG ||
           kind == NET_BROWSER_RENDER_KIND_CSS ||
           kind == NET_BROWSER_RENDER_KIND_JS;
}

static void shell_browser_box_metrics(u8 kind, u8 flags, u8 slot,
                                      u32 *margin_x, u32 *padding_x,
                                      u32 *border_px)
{
    *margin_x = 0u;
    *padding_x = 0u;
    *border_px = 0u;
    if (!shell_browser_render_is_block_like(kind, flags)) {
        return;
    }
    *margin_x = sx(8u);
    if (*margin_x < 4u) {
        *margin_x = 4u;
    }
    *padding_x = sx(10u);
    if (*padding_x < 6u) {
        *padding_x = 6u;
    }
    *border_px = 1u;
    if (kind == NET_BROWSER_RENDER_KIND_HEADING ||
        kind == NET_BROWSER_RENDER_KIND_TITLE) {
        *margin_x = sx(4u);
        *padding_x = sx(6u);
    }
    if (slot < NET_BROWSER_RENDER_MAX) {
        u16 css = net_browser_render_css_flags[slot];
        if ((css & NET_BROWSER_CSS_STYLE_MARGIN) != 0u) {
            *margin_x = net_browser_render_css_margin[slot];
            if (*margin_x > sx(72u)) {
                *margin_x = sx(72u);
            }
        }
        if ((css & NET_BROWSER_CSS_STYLE_PADDING) != 0u) {
            *padding_x = net_browser_render_css_padding[slot];
            if (*padding_x > sx(64u)) {
                *padding_x = sx(64u);
            }
        }
        if ((css & NET_BROWSER_CSS_STYLE_BORDER) != 0u) {
            *border_px = net_browser_render_css_border[slot];
            if (*border_px == 0u) {
                *border_px = 1u;
            }
            if (*border_px > 6u) {
                *border_px = 6u;
            }
        }
    }
}

static void shell_browser_frame_rect(u32 x, u32 y, u32 w, u32 box_h,
                                     u32 fill, u32 border, u32 border_px)
{
    u32 top = y > sy(2u) ? y - sy(2u) : y;
    if (border_px == 0u) {
        border_px = 1u;
    }
    ui_fill_round_rect(x, top, w, box_h, 5u, fill);
    for (u32 b = 0u; b < border_px && b < w && b < box_h; b += 1u) {
        fill_rect(x, top + b, w, 1u, border);
        fill_rect(x, top + box_h - 1u - b, w, 1u, border);
        fill_rect(x + b, top, 1u, box_h, border);
        if (w > b) {
            fill_rect(x + w - 1u - b, top, 1u, box_h, border);
        }
    }
}

static u32 shell_draw_browser_image_preview(u8 image, u32 x, u32 y, u32 max_w,
                                            u32 line_h)
{
    if (image >= NET_BROWSER_IMAGE_MAX ||
        net_browser_image_status[image] != NET_BROWSER_IMAGE_STATUS_DECODED ||
        net_browser_image_width[image] == 0u ||
        net_browser_image_height[image] == 0u ||
        max_w < sx(18u) || line_h < sy(14u)) {
        return 0u;
    }
    u32 thumb_h = line_h > sy(8u) ? line_h - sy(8u) : line_h;
    if (thumb_h < 12u) {
        thumb_h = 12u;
    }
    if (thumb_h > sy(42u)) {
        thumb_h = sy(42u);
    }
    u32 thumb_w = (thumb_h * net_browser_image_width[image]) /
                  net_browser_image_height[image];
    if (thumb_w == 0u) {
        thumb_w = thumb_h;
    }
    if (thumb_w > max_w) {
        thumb_w = max_w;
    }
    u32 top = y + (line_h > thumb_h ? (line_h - thumb_h) / 2u : 0u);
    fill_rect(x, top, thumb_w, thumb_h, 0x00FFFFFFu);
    for (u32 dy = 0u; dy < thumb_h; dy += 1u) {
        u32 src_y = (dy * net_browser_image_height[image]) / thumb_h;
        if (src_y >= net_browser_image_height[image]) {
            src_y = net_browser_image_height[image] - 1u;
        }
        for (u32 dx = 0u; dx < thumb_w; dx += 1u) {
            u32 src_x = (dx * net_browser_image_width[image]) / thumb_w;
            if (src_x >= net_browser_image_width[image]) {
                src_x = net_browser_image_width[image] - 1u;
            }
            u32 color = net_browser_image_pixels[image]
                [src_y * NET_BROWSER_IMAGE_SIDE_MAX + src_x];
            write_pixel((i32) (x + dx), (i32) (top + dy), color);
        }
    }
    fill_rect(x, top, thumb_w, 1u, 0x0095A1B2u);
    fill_rect(x, top + thumb_h - 1u, thumb_w, 1u, 0x0095A1B2u);
    fill_rect(x, top, 1u, thumb_h, 0x0095A1B2u);
    fill_rect(x + thumb_w - 1u, top, 1u, thumb_h, 0x0095A1B2u);
    return thumb_w;
}

static void shell_draw_browser_image_large(u8 image, const char *label,
                                           u32 x, u32 y, u32 w, u32 h)
{
    if (image >= NET_BROWSER_IMAGE_MAX ||
        net_browser_image_status[image] != NET_BROWSER_IMAGE_STATUS_DECODED) {
        return;
    }
    u32 margin_x = sx(8u);
    if (margin_x < 4u) {
        margin_x = 4u;
    }
    u32 box_x = x + margin_x;
    u32 box_w = w > margin_x * 2u ? w - margin_x * 2u : w;
    shell_browser_frame_rect(box_x, y + sy(2u), box_w,
                             h > sy(4u) ? h - sy(4u) : h,
                             0x00F8FAFCu, 0x00C9D3DFu, 1u);
    u32 pad = sx(12u);
    if (pad < 8u) {
        pad = 8u;
    }
    u32 thumb_h = h > sy(24u) ? h - sy(24u) : h;
    if (thumb_h > sy(96u)) {
        thumb_h = sy(96u);
    }
    if (thumb_h < sy(28u)) {
        thumb_h = sy(28u);
    }
    u32 thumb_w = (thumb_h * net_browser_image_width[image]) /
                  net_browser_image_height[image];
    if (thumb_w < sx(28u)) {
        thumb_w = sx(28u);
    }
    if (thumb_w > box_w / 3u) {
        thumb_w = box_w / 3u;
    }
    u32 thumb_x = box_x + pad;
    u32 thumb_y = y + (h > thumb_h ? (h - thumb_h) / 2u : 0u);
    for (u32 dy = 0u; dy < thumb_h; dy += 1u) {
        u32 src_y = (dy * net_browser_image_height[image]) / thumb_h;
        if (src_y >= net_browser_image_height[image]) {
            src_y = net_browser_image_height[image] - 1u;
        }
        for (u32 dx = 0u; dx < thumb_w; dx += 1u) {
            u32 src_x = (dx * net_browser_image_width[image]) / thumb_w;
            if (src_x >= net_browser_image_width[image]) {
                src_x = net_browser_image_width[image] - 1u;
            }
            u32 color = net_browser_image_pixels[image]
                [src_y * NET_BROWSER_IMAGE_SIDE_MAX + src_x];
            write_pixel((i32) (thumb_x + dx), (i32) (thumb_y + dy), color);
        }
    }
    fill_rect(thumb_x, thumb_y, thumb_w, 1u, 0x0095A1B2u);
    fill_rect(thumb_x, thumb_y + thumb_h - 1u, thumb_w, 1u, 0x0095A1B2u);
    fill_rect(thumb_x, thumb_y, 1u, thumb_h, 0x0095A1B2u);
    fill_rect(thumb_x + thumb_w - 1u, thumb_y, 1u, thumb_h, 0x0095A1B2u);
    u32 text_x = thumb_x + thumb_w + sx(14u);
    u32 text_w = box_x + box_w > text_x + sx(8u)
        ? box_x + box_w - text_x - sx(8u)
        : 0u;
    draw_text_clip(text_x, y + sy(16u), label, SHELL_COL_TEXT, text_w);
    draw_text_clip(text_x, y + sy(16u) + gui_line_height() + sy(6u),
                   "native decoded pixels", SHELL_COL_MUTED, text_w);
}

static void shell_draw_browser_render_line(u32 x, u32 y, u32 w, u32 line_h,
                                           const char *line, u8 kind, u8 flags,
                                           u8 image_index, u8 slot,
                                           u32 block_h)
{
    u32 color = shell_browser_render_color(kind, slot);
    u32 margin_x = 0u;
    u32 padding_x = 0u;
    u32 border_px = 0u;
    u16 css = slot < NET_BROWSER_RENDER_MAX ? net_browser_render_css_flags[slot] : 0u;
    u32 css_bg = 0u;
    if ((css & NET_BROWSER_CSS_STYLE_BG) != 0u) {
        css_bg = net_browser_render_css_bg[slot];
    }
    shell_browser_box_metrics(kind, flags, slot, &margin_x, &padding_x, &border_px);
    u32 avail_x = x + margin_x;
    u32 avail_w = w > margin_x * 2u ? w - margin_x * 2u : w;
    if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
        net_browser_render_css_width[slot] != 0u &&
        net_browser_render_css_width[slot] < avail_w) {
        avail_w = net_browser_render_css_width[slot];
    }
    if (block_h < line_h) {
        block_h = line_h;
    }
    u32 draw_x = avail_x + padding_x + border_px;
    u32 draw_w = avail_w > (padding_x + border_px) * 2u
        ? avail_w - (padding_x + border_px) * 2u
        : avail_w;
    if ((flags & NET_BROWSER_RENDER_FLAG_BUTTON) != 0u) {
        u32 text_w = shell_text_pixel_width(line, avail_w);
        u32 box_w = text_w + sx(36u);
        if (box_w < sx(96u)) {
            box_w = sx(96u);
        }
        if (box_w > avail_w) {
            box_w = avail_w;
        }
        if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
            net_browser_render_css_width[slot] > box_w &&
            net_browser_render_css_width[slot] <= avail_w) {
            box_w = net_browser_render_css_width[slot];
        }
        u32 box_x = (flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u ?
                    avail_x + (avail_w - box_w) / 2u : avail_x;
        shell_browser_frame_rect(box_x, y, box_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x002B7DE9u,
                                 0x001A5FB4u, border_px);
        color = 0x00FFFFFFu;
        draw_x = box_x + sx(12u);
        draw_w = box_w > sx(24u) ? box_w - sx(24u) : box_w;
        if ((flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u && text_w < draw_w) {
            draw_x += (draw_w - text_w) / 2u;
            draw_w = text_w + gui_char_advance();
        }
    } else if ((flags & NET_BROWSER_RENDER_FLAG_INPUT) != 0u) {
        u32 text_w = shell_text_pixel_width(line, avail_w);
        u32 box_w = text_w + sx(34u);
        if (box_w < sx(180u)) {
            box_w = sx(180u);
        }
        if (box_w > avail_w) {
            box_w = avail_w;
        }
        if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
            net_browser_render_css_width[slot] > box_w &&
            net_browser_render_css_width[slot] <= avail_w) {
            box_w = net_browser_render_css_width[slot];
        }
        u32 box_x = (flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u ?
                    avail_x + (avail_w - box_w) / 2u : avail_x;
        shell_browser_frame_rect(box_x, y, box_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00FFFFFFu,
                                 SHELL_COL_BORDER, border_px);
        draw_x = box_x + sx(10u);
        draw_w = box_w > sx(20u) ? box_w - sx(20u) : box_w;
    } else if ((flags & NET_BROWSER_RENDER_FLAG_BOX) != 0u) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00F6F8FAu,
                                 SHELL_COL_BORDER, border_px);
    } else if ((flags & NET_BROWSER_RENDER_FLAG_MONO) != 0u) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00F4F7FAu,
                                 0x00CAD3DEu, border_px);
    } else if ((flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u) {
        u32 text_w = shell_text_pixel_width(line, avail_w);
        if (text_w < avail_w) {
            draw_x = avail_x + (avail_w - text_w) / 2u;
            draw_w = text_w + gui_char_advance();
        }
    }
    if (kind == NET_BROWSER_RENDER_KIND_HEADING) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00EAF2FFu,
                                 0x00C9DCF8u, border_px);
    } else if (kind == NET_BROWSER_RENDER_KIND_IMAGE) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00EEF2F6u,
                                 0x00C9D3DFu, border_px);
        u32 preview_w = shell_draw_browser_image_preview(
            image_index, draw_x, y, draw_w > sx(96u) ? sx(96u) : draw_w,
            line_h);
        if (preview_w != 0u) {
            draw_x += preview_w + sx(10u);
            draw_w = draw_w > preview_w + sx(10u)
                ? draw_w - preview_w - sx(10u)
                : 0u;
        }
    } else if (kind == NET_BROWSER_RENDER_KIND_TABLE) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00EEF8F1u,
                                 0x00B8DCC4u, border_px);
        fill_rect(avail_x + sx(14u), y > sy(2u) ? y - sy(2u) : y,
                  1u, block_h, 0x00B8DCC4u);
    } else if (kind == NET_BROWSER_RENDER_KIND_QUOTE) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00F4F6FAu,
                                 0x00CBD3DFu, border_px);
        fill_rect(avail_x,
                  y > sy(2u) ? y - sy(2u) : y, sx(3u), block_h,
                  0x007D8CA3u);
        draw_x += sx(8u);
        draw_w = draw_w > sx(8u) ? draw_w - sx(8u) : draw_w;
    } else if (kind == NET_BROWSER_RENDER_KIND_META) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00F8F9FBu,
                                 0x00DFE4EAu, border_px);
    } else if (kind == NET_BROWSER_RENDER_KIND_EMBED) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00EEF3FFu,
                                 0x00C8D5EFu, border_px);
        fill_rect(avail_x,
                  y > sy(2u) ? y - sy(2u) : y, sx(3u), block_h,
                  0x00648BE8u);
        draw_x += sx(8u);
        draw_w = draw_w > sx(8u) ? draw_w - sx(8u) : draw_w;
    } else if (kind == NET_BROWSER_RENDER_KIND_DIALOG) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00F1F4FAu,
                                 0x00AEB8CCu, border_px);
    } else if (kind == NET_BROWSER_RENDER_KIND_CSS) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00F0EAF7u,
                                 0x00D9C7E8u, border_px);
    } else if (kind == NET_BROWSER_RENDER_KIND_JS) {
        shell_browser_frame_rect(avail_x, y, avail_w, block_h,
                                 (css & NET_BROWSER_CSS_STYLE_BG) != 0u ? css_bg : 0x00FFF4D8u,
                                 0x00E9D39Cu, border_px);
    }
    draw_text_clip(draw_x, y, line, color, draw_w);
    if (kind == NET_BROWSER_RENDER_KIND_HEADING ||
        (flags & NET_BROWSER_RENDER_FLAG_BOLD) != 0u ||
        (flags & NET_BROWSER_RENDER_FLAG_LARGE) != 0u ||
        (flags & NET_BROWSER_RENDER_FLAG_BUTTON) != 0u) {
        draw_text_clip(draw_x + 1u, y, line, color, draw_w > 1u ? draw_w - 1u : draw_w);
    } else if (kind == NET_BROWSER_RENDER_KIND_LINK) {
        u32 underline = shell_text_pixel_width(line, draw_w);
        if (underline != 0u) {
            fill_rect(draw_x, y + gui_line_height() + 1u, underline, 1u, color);
        }
    }
}

static void shell_draw_browser_render_block(const char *text, u8 kind, u8 flags,
                                            u8 image_index,
                                            u8 slot,
                                            u32 content_x, u32 content_y,
                                            u32 content_w, u32 line_h,
                                            u32 cols, u32 scroll,
                                            u32 visible_lines,
                                            u32 *logical_line, u32 *drawn)
{
    char line[136];
    u32 i = 0u;
    while (text[i] != 0) {
        while (text[i] == ' ') {
            i += 1u;
        }
        u32 len = 0u;
        u32 last_space = 0xFFFFFFFFu;
        u32 start_i = i;
        while (text[i] != 0 && text[i] != '\n' &&
               len < cols && len < sizeof(line) - 1u) {
            line[len] = text[i];
            if (text[i] == ' ') {
                last_space = len;
            }
            len += 1u;
            i += 1u;
        }
        if (text[i] != 0 && text[i] != '\n' &&
            len >= cols && last_space != 0xFFFFFFFFu && last_space > 0u) {
            u32 rewind = len - last_space - 1u;
            i -= rewind;
            len = last_space;
        }
        if (len == 0u && text[i] == '\n') {
            i += 1u;
        }
        line[len] = 0;
        u32 block_lines = shell_browser_css_block_lines(slot, line_h);
        if (*logical_line >= scroll && *drawn < visible_lines) {
            shell_draw_browser_render_line(content_x, content_y + (*drawn) * line_h,
                                           content_w, line_h, line, kind, flags,
                                           image_index, slot,
                                           block_lines * line_h);
            u32 visible = block_lines;
            if (visible > visible_lines - *drawn) {
                visible = visible_lines - *drawn;
            }
            *drawn += visible;
        } else if (*logical_line < scroll &&
                   *logical_line + block_lines > scroll &&
                   *drawn < visible_lines) {
            *drawn += 1u;
        }
        *logical_line += block_lines;
        if (text[i] == '\n') {
            i += 1u;
        }
        if (i == start_i && text[i] != 0) {
            i += 1u;
        }
        if (*drawn >= visible_lines) {
            return;
        }
    }
}

static void shell_draw_browser_decoded_image_block(const char *text, u8 image_index,
                                                   u32 content_x, u32 content_y,
                                                   u32 content_w, u32 line_h,
                                                   u32 scroll, u32 visible_lines,
                                                   u32 *logical_line,
                                                   u32 *drawn)
{
    u32 block_lines = shell_browser_image_render_lines(NET_BROWSER_RENDER_KIND_IMAGE,
                                                       image_index);
    if (block_lines == 0u) {
        return;
    }
    u32 end_line = *logical_line + block_lines;
    if (end_line <= scroll) {
        *logical_line = end_line;
        return;
    }
    if (*drawn < visible_lines && *logical_line >= scroll) {
        shell_draw_browser_image_large(image_index, text,
                                       content_x,
                                       content_y + (*drawn) * line_h,
                                       content_w, block_lines * line_h);
    }
    u32 visible = block_lines;
    if (*logical_line < scroll) {
        visible -= scroll - *logical_line;
    }
    if (visible > visible_lines - *drawn) {
        visible = visible_lines - *drawn;
    }
    *drawn += visible;
    *logical_line = end_line;
}

static void shell_layout_browser_render_block(const char *text, u8 kind, u8 flags,
                                              u8 slot,
                                              u8 link_index,
                                              u8 control_index,
                                              u32 content_w, u32 line_h,
                                              u32 cols, u32 *logical_line)
{
    if (kind == NET_BROWSER_RENDER_KIND_IMAGE) {
        for (u32 b = 0u; b < NET_BROWSER_RENDER_MAX; b += 1u) {
            if (net_browser_render_kind[b] == kind &&
                net_text_is(net_browser_render_text[b], text)) {
                u32 image_lines = shell_browser_image_render_lines(
                    kind, net_browser_render_image[b]);
                if (image_lines != 0u) {
                    u32 margin_x = 0u;
                    u32 padding_x = 0u;
                    u32 border_px = 0u;
                    shell_browser_box_metrics(kind, flags, slot, &margin_x,
                                              &padding_x, &border_px);
                    u32 avail_w = content_w > margin_x * 2u
                        ? content_w - margin_x * 2u
                        : content_w;
                    net_browser_layout_record((u16) margin_x,
                                              (u16) ((*logical_line) * line_h),
                                              (u16) avail_w,
                                              (u16) (image_lines * line_h),
                                              kind, flags, link_index,
                                              control_index);
                    *logical_line += image_lines;
                    return;
                }
            }
        }
    }
    char line[136];
    u32 i = 0u;
    while (text[i] != 0) {
        while (text[i] == ' ') {
            i += 1u;
        }
        u32 len = 0u;
        u32 last_space = 0xFFFFFFFFu;
        u32 start_i = i;
        while (text[i] != 0 && text[i] != '\n' &&
               len < cols && len < sizeof(line) - 1u) {
            line[len] = text[i];
            if (text[i] == ' ') {
                last_space = len;
            }
            len += 1u;
            i += 1u;
        }
        if (text[i] != 0 && text[i] != '\n' &&
            len >= cols && last_space != 0xFFFFFFFFu && last_space > 0u) {
            net_browser_layout_note_wrap();
            u32 rewind = len - last_space - 1u;
            i -= rewind;
            len = last_space;
        } else if (text[i] != 0 && text[i] != '\n' && len >= cols) {
            net_browser_layout_note_wrap();
        }
        if (len == 0u && text[i] == '\n') {
            i += 1u;
        }
        line[len] = 0;

        u32 margin_x = 0u;
        u32 padding_x = 0u;
        u32 border_px = 0u;
        shell_browser_box_metrics(kind, flags, slot, &margin_x, &padding_x, &border_px);
        u32 avail_w = content_w > margin_x * 2u
            ? content_w - margin_x * 2u
            : content_w;
        u16 css = slot < NET_BROWSER_RENDER_MAX ? net_browser_render_css_flags[slot] : 0u;
        if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
            net_browser_render_css_width[slot] != 0u &&
            net_browser_render_css_width[slot] < avail_w) {
            avail_w = net_browser_render_css_width[slot];
        }
        u32 text_w = shell_text_pixel_width(line, avail_w);
        u32 box_w = text_w;
        u32 box_x = margin_x;
        if ((flags & NET_BROWSER_RENDER_FLAG_BUTTON) != 0u) {
            box_w = text_w + sx(36u);
            if (box_w < sx(96u)) {
                box_w = sx(96u);
            }
            if (box_w > avail_w) {
                box_w = avail_w;
            }
            if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
                net_browser_render_css_width[slot] > box_w &&
                net_browser_render_css_width[slot] <= avail_w) {
                box_w = net_browser_render_css_width[slot];
            }
            if ((flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u) {
                box_x = margin_x + (avail_w - box_w) / 2u;
            }
        } else if ((flags & NET_BROWSER_RENDER_FLAG_INPUT) != 0u) {
            box_w = text_w + sx(34u);
            if (box_w < sx(180u)) {
                box_w = sx(180u);
            }
            if (box_w > avail_w) {
                box_w = avail_w;
            }
            if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
                net_browser_render_css_width[slot] > box_w &&
                net_browser_render_css_width[slot] <= avail_w) {
                box_w = net_browser_render_css_width[slot];
            }
            if ((flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u) {
                box_x = margin_x + (avail_w - box_w) / 2u;
            }
        } else if (shell_browser_render_is_block_like(kind, flags)) {
            box_w = text_w + (padding_x + border_px) * 2u;
            if (box_w < sx(96u)) {
                box_w = sx(96u);
            }
            if (kind == NET_BROWSER_RENDER_KIND_TABLE ||
                kind == NET_BROWSER_RENDER_KIND_IMAGE ||
                kind == NET_BROWSER_RENDER_KIND_QUOTE ||
                kind == NET_BROWSER_RENDER_KIND_META ||
                kind == NET_BROWSER_RENDER_KIND_EMBED ||
                kind == NET_BROWSER_RENDER_KIND_DIALOG ||
                kind == NET_BROWSER_RENDER_KIND_CSS ||
                kind == NET_BROWSER_RENDER_KIND_JS ||
                (flags & NET_BROWSER_RENDER_FLAG_BOX) != 0u) {
                box_w = avail_w;
            } else if (box_w > avail_w) {
                box_w = avail_w;
            }
            if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
                net_browser_render_css_width[slot] != 0u &&
                net_browser_render_css_width[slot] <= avail_w) {
                box_w = net_browser_render_css_width[slot];
            }
            if ((flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u &&
                box_w < avail_w) {
                box_x = margin_x + (avail_w - box_w) / 2u;
            }
        } else {
            if (box_w == 0u) {
                box_w = gui_char_advance();
            }
            if (box_w > avail_w) {
                box_w = avail_w;
            }
            if ((flags & NET_BROWSER_RENDER_FLAG_CENTER) != 0u &&
                box_w < avail_w) {
                box_x = margin_x + (avail_w - box_w) / 2u;
            }
        }
        net_browser_layout_record((u16) box_x,
                                  (u16) ((*logical_line) * line_h),
                                  (u16) box_w,
                                  (u16) (shell_browser_css_block_lines(slot, line_h) * line_h),
                                  kind, flags, link_index,
                                  control_index);
        *logical_line += shell_browser_css_block_lines(slot, line_h);
        if (text[i] == '\n') {
            i += 1u;
        }
        if (i == start_i && text[i] != 0) {
            i += 1u;
        }
    }
}

static void shell_build_browser_layout(u32 content_w, u32 content_h,
                                       u32 line_h, u32 cols)
{
    net_browser_layout_reset((u16) content_w, (u16) content_h);
    if (net_browser_render_count == 0u) {
        return;
    }
    u32 logical_line = 0u;
    for (u32 b = 0u; b < net_browser_render_count; b += 1u) {
        if (net_browser_render_text[b][0] == 0 ||
            (net_browser_render_flags[b] & NET_BROWSER_RENDER_FLAG_HIDDEN) != 0u) {
            continue;
        }
        shell_layout_browser_render_block(net_browser_render_text[b],
                                          net_browser_render_kind[b],
                                          net_browser_render_flags[b],
                                          (u8) b,
                                          net_browser_render_link[b],
                                          net_browser_render_control[b],
                                          content_w, line_h, cols,
                                          &logical_line);
        logical_line += shell_browser_render_gap(net_browser_render_kind[b],
                                                 net_browser_render_flags[b]);
    }
    net_browser_layout_maybe_print();
}

static void shell_draw_browser_document(const struct ShellWin *win, u32 x, u32 y, u32 w, u32 h)
{
    if (w < sx(80u) || h < sy(80u)) {
        return;
    }

    ui_fill_round_rect(x, y, w, h, 6u, SHELL_COL_PANEL);
    fill_rect(x, y, w, 1u, SHELL_COL_BORDER);
    fill_rect(x, y + h - 1u, w, 1u, SHELL_COL_BORDER);
    fill_rect(x, y, 1u, h, SHELL_COL_BORDER);
    fill_rect(x + w - 1u, y, 1u, h, SHELL_COL_BORDER);

    u32 header_h = gui_line_height() + sy(16u);
    if (header_h < 34u) {
        header_h = 34u;
    }
    fill_rect(x + 1u, y + 1u, w - 2u, header_h, 0x00F1F7FFu);
    fill_rect(x + 1u, y + header_h, w - 2u, 1u, SHELL_COL_BORDER);
    draw_text_clip(x + sx(14u), y + (header_h - gui_line_height()) / 2u,
                   ui_txt_browser_page, SHELL_COL_TITLE_FOCUS, sx(80u));
    draw_text_clip(x + sx(84u), y + (header_h - gui_line_height()) / 2u,
                   shell_browser_title(win), SHELL_COL_TEXT, w - sx(120u));

    u32 content_x = x + sx(16u);
    u32 content_y = y + header_h + sy(12u);
    u32 content_w = w > sx(42u) ? w - sx(42u) : w;
    u32 content_h = h > header_h + sy(24u) ? h - header_h - sy(24u) : 0u;
    if (content_h < gui_line_height()) {
        return;
    }

    const char *text = shell_browser_text(win);
    u32 *scroll = shell_browser_scroll_ref(win);

    u32 advance = gui_char_advance();
    u32 cols = advance != 0u ? content_w / advance : 0u;
    if (cols < 8u) {
        cols = 8u;
    }
    if (cols > 132u) {
        cols = 132u;
    }

    u32 line_h = gui_line_height() + sy(5u);
    if (line_h < 24u) {
        line_h = 24u;
    }
    u32 visible_lines = content_h / line_h;
    if (visible_lines == 0u) {
        visible_lines = 1u;
    }

    shell_build_browser_layout(content_w, content_h, line_h, cols);

    u32 total_lines = shell_browser_render_total_lines(cols, line_h);
    u32 max_scroll = total_lines > visible_lines ? total_lines - visible_lines : 0u;
    if (*scroll > max_scroll) {
        *scroll = max_scroll;
    }

    if (max_scroll != 0u && w > sx(28u)) {
        u32 track_x = x + w - sx(14u);
        u32 track_y = content_y;
        u32 track_h = content_h;
        fill_rect(track_x, track_y, sx(4u), track_h, 0x00D8E0EAu);
        u32 thumb_h = (track_h * visible_lines) / total_lines;
        if (thumb_h < sy(24u)) {
            thumb_h = sy(24u);
        }
        if (thumb_h > track_h) {
            thumb_h = track_h;
        }
        u32 thumb_y = track_y;
        if (track_h > thumb_h) {
            thumb_y += ((track_h - thumb_h) * (*scroll)) / max_scroll;
        }
        fill_rect(track_x, thumb_y, sx(4u), thumb_h, SHELL_COL_TITLE_FOCUS);
    }

    if (net_browser_render_count != 0u) {
        u32 logical_line = 0u;
        u32 drawn = 0u;
        for (u32 b = 0u; b < net_browser_render_count && drawn < visible_lines; b += 1u) {
            if (net_browser_render_text[b][0] == 0 ||
                (net_browser_render_flags[b] & NET_BROWSER_RENDER_FLAG_HIDDEN) != 0u) {
                continue;
            }
            if (net_browser_render_kind[b] == NET_BROWSER_RENDER_KIND_IMAGE &&
                shell_browser_image_render_lines(net_browser_render_kind[b],
                                                 net_browser_render_image[b]) != 0u) {
                shell_draw_browser_decoded_image_block(net_browser_render_text[b],
                                                       net_browser_render_image[b],
                                                       content_x, content_y,
                                                       content_w, line_h,
                                                       *scroll, visible_lines,
                                                       &logical_line, &drawn);
                u32 gap_lines = shell_browser_render_gap(net_browser_render_kind[b],
                                                         net_browser_render_flags[b]);
                for (u32 g = 0u; g < gap_lines; g += 1u) {
                    if (logical_line >= *scroll && drawn < visible_lines) {
                        drawn += 1u;
                    }
                    logical_line += 1u;
                }
                continue;
            }
            shell_draw_browser_render_block(net_browser_render_text[b],
                                            net_browser_render_kind[b],
                                            net_browser_render_flags[b],
                                            net_browser_render_image[b],
                                            (u8) b,
                                            content_x, content_y, content_w,
                                            line_h, cols, *scroll,
                                            visible_lines, &logical_line, &drawn);
            u32 gap_lines = shell_browser_render_gap(net_browser_render_kind[b],
                                                     net_browser_render_flags[b]);
            for (u32 g = 0u; g < gap_lines; g += 1u) {
                if (logical_line >= *scroll && drawn < visible_lines) {
                    drawn += 1u;
                }
                logical_line += 1u;
            }
        }
        return;
    }

    char line[136];
    u32 logical_line = 0u;
    u32 drawn = 0u;
    u32 i = 0u;
    while (text[i] != 0) {
        while (text[i] == ' ') {
            i += 1u;
        }
        u32 len = 0u;
        u32 last_space = 0xFFFFFFFFu;
        u32 start_i = i;
        while (text[i] != 0 && text[i] != '\n' && len < cols && len < sizeof(line) - 1u) {
            line[len] = text[i];
            if (text[i] == ' ') {
                last_space = len;
            }
            len += 1u;
            i += 1u;
        }
        if (text[i] != 0 && text[i] != '\n' &&
            len >= cols && last_space != 0xFFFFFFFFu && last_space > 0u) {
            u32 rewind = len - last_space - 1u;
            i -= rewind;
            len = last_space;
        }
        if (len == 0u && text[i] == '\n') {
            i += 1u;
        }
        line[len] = 0;
        if (logical_line >= *scroll && drawn < visible_lines) {
            draw_text_clip(content_x, content_y + drawn * line_h, line,
                           SHELL_COL_TEXT, content_w);
            drawn += 1u;
        }
        logical_line += 1u;
        if (text[i] == '\n') {
            i += 1u;
        }
        if (i == start_i && text[i] != 0) {
            i += 1u;
        }
    }
}

static void shell_format_u16(char *out, u16 value)
{
    char tmp[6];
    u32 n = value;
    u32 len = 0u;
    if (n == 0u) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (n != 0u && len < sizeof(tmp)) {
        tmp[len++] = (char) ('0' + (n % 10u));
        n /= 10u;
    }
    for (u32 i = 0u; i < len; i += 1u) {
        out[i] = tmp[len - 1u - i];
    }
    out[len] = 0;
}

static void shell_draw_net_row_text_fit(u32 x, u32 *row_y, u32 row_h,
                                        u32 bottom, u32 w,
                                        const char *label,
                                        const char *value)
{
    if (*row_y + row_h <= bottom) {
        shell_draw_net_row_text(x, *row_y, w, label, value);
    }
    *row_y += row_h + sy(6u);
}

static void shell_draw_net_row_count_fit(u32 x, u32 *row_y, u32 row_h,
                                         u32 bottom, u32 w,
                                         const char *label, u16 value)
{
    char text[8];
    shell_format_u16(text, value);
    shell_draw_net_row_text_fit(x, row_y, row_h, bottom, w, label, text);
}

static const char *shell_browser_resource_type_text(void)
{
    if (net_browser_resource_count == 0u) {
        return "-";
    }
    if (net_browser_resource_type[0] == 'H') {
        return "HREF";
    }
    if (net_browser_resource_type[0] == 'S') {
        return "SRC";
    }
    if (net_browser_resource_type[0] == 'A') {
        return "ACTION";
    }
    return "?";
}

static const char *shell_browser_first_resource_text(void)
{
    if (net_browser_resource_count == 0u) {
        return "-";
    }
    const char *url = net_browser_resource_url[0];
    const char *path = net_browser_same_host_path(url);
    return path != 0 ? path : url;
}

static const char *shell_browser_clicked_link_text(void)
{
    if (net_browser_clicked_url[0] == 0) {
        return "-";
    }
    const char *url = net_browser_clicked_url;
    const char *path = net_browser_same_host_path(url);
    return path != 0 ? path : url;
}

static void shell_draw_browser_info_panel(const struct ShellWin *win, u32 x, u32 y, u32 w, u32 h)
{
    if (w < sx(150u) || h < sy(120u)) {
        return;
    }
    ui_fill_round_rect(x, y, w, h, 6u, 0x00F4F7FBu);
    fill_rect(x, y, 1u, h, SHELL_COL_BORDER);
    fill_rect(x, y, w, 1u, SHELL_COL_BORDER);
    draw_text_clip(x + sx(12u), y + sy(12u), ui_txt_browser_info,
                   SHELL_COL_TITLE_FOCUS, w - sx(24u));

    u32 row_y = y + sy(44u);
    u32 row_h = gui_line_height() + sy(14u);
    if (row_h < 34u) {
        row_h = 34u;
    }
    u32 row_w = w > sx(20u) ? w - sx(20u) : w;
    u32 row_x = x + sx(10u);
    u32 bottom = y + h;
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                ui_txt_browser_status,
                                shell_browser_status_text(win));
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "HTTP", net_browser_status);
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                ui_txt_browser_line,
                                shell_browser_response_text(win));
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                ui_txt_browser_source, "REAL HTTPS");
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "HOST", net_browser_host);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "BLOCKS", net_browser_render_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "DOM", net_browser_dom_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "DEPTH", net_browser_dom_max_depth);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "DTEXT", net_browser_dom_text_bytes);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "LAY", net_browser_layout_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "LLINE", net_browser_layout_line_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "LLINK", net_browser_layout_link_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "LCLK", net_browser_link_click_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "NAV", net_browser_navigation_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "HIST", net_browser_history_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "HIDX", net_browser_history_index);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "BACK", net_browser_history_back_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "FWD", net_browser_history_forward_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "RELOAD", net_browser_history_reload_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "STOP", net_browser_history_stop_count);
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "LTGT", shell_browser_clicked_link_text());
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "QUEUE", net_browser_resource_total);
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "RTYPE", shell_browser_resource_type_text());
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "FIRST", shell_browser_first_resource_text());
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "RSTEP", net_browser_resource_fetch_phase);
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "RSTAT", net_browser_resource_fetch_status);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "RBYTE", net_browser_resource_fetch_bytes);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "TAGS", net_browser_tag_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "ATTR", net_browser_attr_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "A", net_browser_anchor_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "HREF", net_browser_href_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "SRC", net_browser_src_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "ACTION", net_browser_action_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "LINK", net_browser_link_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "SCRIPT", net_browser_script_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "STYLE", net_browser_style_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "CSSR", net_browser_css_rule_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "CSSD", net_browser_css_decl_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "CSSB", net_browser_css_bytes);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "CSSS", net_browser_css_stored_rule_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "CSSM", net_browser_css_matched_rule_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "JSB", net_browser_js_bytes);
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "JSMODE", "NOJS");
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                "JSREQ", net_browser_js_required ? "YES" : "NO");
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "JSTK", net_browser_js_token_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "JSDOC", net_browser_js_document_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "JSLOC", net_browser_js_location_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "EVT", net_browser_event_attr_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "JSURL", net_browser_js_url_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "UNSUP", net_browser_unsupported_feature_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "IMG", net_browser_image_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "FORM", net_browser_form_count);
    shell_draw_net_row_count_fit(row_x, &row_y, row_h, bottom, row_w,
                                 "INPUT", net_browser_input_count);
    shell_draw_net_row_text_fit(row_x, &row_y, row_h, bottom, row_w,
                                ui_txt_net_status,
                                net_ready ? ui_txt_net_ready : ui_txt_net_no_nic);
}

static void shell_draw_net_client(const struct ShellWin *win)
{
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 h = win->h - shellm.title_h;
    fill_rect(x, y, w, h, SHELL_COL_CLIENT);

    u32 toolbar_h = shell_browser_toolbar_h();
    fill_rect(x, y, w, toolbar_h, 0x00EDF3FAu);
    fill_rect(x, y + toolbar_h - 1u, w, 1u, SHELL_COL_BORDER);

    u32 btn = shell_browser_button_size();
    u32 gap = sx(6u);
    if (gap < 4u) {
        gap = 4u;
    }
    u32 by = y + (toolbar_h - btn) / 2u;
    u32 bx = x + sx(10u);
    shell_draw_browser_button(bx, by, btn, '<', net_browser_can_back(), 0u);
    bx += btn + gap;
    shell_draw_browser_button(bx, by, btn, '>', net_browser_can_forward(), 0u);
    bx += btn + gap;
    shell_draw_browser_button(bx, by, btn, 'R', !net_browser_is_loading(), 0u);
    bx += btn + gap;
    shell_draw_browser_button(bx, by, btn, 'X', net_browser_is_loading(), 0u);
    bx += btn + gap;
    shell_draw_browser_button(bx, by, btn, 'H', !net_browser_at_home(), 0u);
    bx += btn + gap * 2u;

    u32 info_x = x + w - sx(10u) - btn;
    shell_draw_browser_button(info_x, by, btn, 'I', 1u, shell_browser_info_open);

    u32 url_x = bx;
    u32 url_w = info_x > url_x + gap ? info_x - url_x - gap : sx(80u);
    u32 url_bg = shell_browser_url_editing ? 0x00FFF7D6u : 0x00FFFFFFu;
    ui_fill_round_rect(url_x, by, url_w, btn, 7u, url_bg);
    fill_rect(url_x, by + btn - 1u, url_w, 1u, SHELL_COL_BORDER);
    draw_text_clip(url_x + sx(12u), by + (btn - gui_line_height()) / 2u,
                   shell_browser_url(win), SHELL_COL_TEXT, url_w - sx(24u));
    if (shell_browser_url_editing) {
        u32 caret_x = url_x + sx(14u) +
                      shell_text_pixel_width(shell_browser_url_edit,
                                             url_w > sx(36u) ? url_w - sx(36u) : url_w);
        if (caret_x > url_x + url_w - sx(14u)) {
            caret_x = url_x + url_w - sx(14u);
        }
        fill_rect(caret_x, by + sy(8u), 2u,
                  btn > sy(16u) ? btn - sy(16u) : btn, SHELL_COL_TITLE_FOCUS);
    }

    u32 page_x = x + sx(10u);
    u32 page_y = y + toolbar_h + sy(10u);
    u32 page_w = w > sx(20u) ? w - sx(20u) : w;
    u32 page_h = h > (page_y - y) + sy(10u) ? h - (page_y - y) - sy(10u) : 0u;
    u32 info_w = 0u;
    if (shell_browser_info_open && page_w > sx(420u)) {
        info_w = sx(300u);
        if (info_w > page_w / 2u) {
            info_w = page_w / 2u;
        }
    }
    if (info_w != 0u) {
        u32 doc_w = page_w > info_w + gap ? page_w - info_w - gap : page_w;
        shell_draw_browser_document(win, page_x, page_y, doc_w, page_h);
        shell_draw_browser_info_panel(win, page_x + doc_w + gap, page_y, info_w, page_h);
    } else {
        shell_draw_browser_document(win, page_x, page_y, page_w, page_h);
    }
}

static void shell_draw_window_body(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    if ((win->flags & SHELL_WF_MINIMIZED) != 0) {
        return;
    }
    u32 x = (u32) win->x;
    u32 y = (u32) win->y;
    u32 w = win->w;
    u32 h = win->h;
    if (w > 8u && h > shellm.title_h + 8u) {
        fill_rect(x + 5u, y + h - 2u, w - 2u, 4u, SHELL_COL_SHADOW);
        fill_rect(x + w - 2u, y + shellm.title_h + 5u, 4u, h - shellm.title_h - 5u,
                  SHELL_COL_SHADOW);
    }
    fill_rect(x, y + shellm.title_h, w, h - shellm.title_h, SHELL_COL_CLIENT);
    fill_rect(x, y + h - 2u, w, 2u, 0x00AAB8C7u);
    fill_rect(x, y, 2u, h, SHELL_COL_BORDER);
    fill_rect(x + w - 2u, y, 2u, h, 0x00AAB8C7u);

    if (win->type == SHELL_WIN_FILES) {
        shell_draw_tabs(win);
        shell_draw_files_client(win);
    } else if (win->type == SHELL_WIN_ABOUT) {
        shell_draw_about_client(win);
    } else if (win->type == SHELL_WIN_APPS) {
        shell_draw_apps_client(win);
    } else if (win->type == SHELL_WIN_LOG) {
        shell_draw_log_client(win);
    } else if (win->type == SHELL_WIN_NET) {
        shell_draw_net_client(win);
    }
}

static void shell_draw_window(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    if (!win->used ||
        (win->flags & SHELL_WF_VISIBLE) == 0 ||
        (win->flags & SHELL_WF_MINIMIZED) != 0) {
        return;
    }
    shell_draw_titlebar(win, shell_focus_idx == index);
    shell_draw_window_body(index);
}

static void shell_draw_start_menu(void);
static void shell_draw_taskbar(void);

static void shell_draw_window_frame_only(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    if (!win->used ||
        (win->flags & SHELL_WF_VISIBLE) == 0 ||
        (win->flags & SHELL_WF_MINIMIZED) != 0) {
        return;
    }

    u32 x = (u32) win->x;
    u32 y = (u32) win->y;
    u32 w = win->w;
    u32 h = win->h;
    shell_draw_titlebar(win, shell_focus_idx == index);
    if (w > 8u && h > shellm.title_h + 8u) {
        fill_rect(x + 5u, y + h - 2u, w - 2u, 4u, SHELL_COL_SHADOW);
        fill_rect(x + w - 2u, y + shellm.title_h + 5u, 4u,
                  h - shellm.title_h - 5u, SHELL_COL_SHADOW);
    }
    fill_rect(x, y + h - 2u, w, 2u, 0x00AAB8C7u);
    fill_rect(x, y, 2u, h, SHELL_COL_BORDER);
    fill_rect(x + w - 2u, y, 2u, h, 0x00AAB8C7u);
}

static void shell_draw_netsurf_overlay_chrome(void)
{
    u8 saw_net = 0u;
    for (u8 zi = 0u; zi < shell_z_count; zi += 1u) {
        u8 wi = shell_z[zi];
        struct ShellWin *win = &shell_wins[wi];
        if (!win->used ||
            (win->flags & SHELL_WF_VISIBLE) == 0 ||
            (win->flags & SHELL_WF_MINIMIZED) != 0) {
            continue;
        }
        if (win->type == SHELL_WIN_NET) {
            shell_draw_window_frame_only(wi);
            saw_net = 1u;
        } else if (saw_net) {
            shell_draw_window(wi);
        }
    }
    shell_draw_start_menu();
    shell_draw_taskbar();
}

static void shell_wallpaper(void)
{
    u32 height = g_boot->framebuffer.height;
    u32 width = g_boot->framebuffer.width;
    for (u32 row = 0; row < height; row += 1) {
        u32 blue = 70u + ((140u * row) / height);
        u32 green = 45u + ((70u * row) / height);
        u32 color = (0x0Bu << 16) | (green << 8) | blue;
        fill_rect(0, row, width, 1, color);
    }
    if (height > 260u && width > 520u) {
        u32 band_y = height / 4u;
        fill_rect(width / 2u, band_y, width / 2u, sy(2u), 0x00164262u);
        fill_rect(width / 2u + sx(80u), band_y + sy(34u), width / 3u, sy(2u), 0x001A4F75u);
        fill_rect(width - sx(330u), height - shellm.taskbar_h - sy(90u),
                  sx(250u), sy(2u), 0x001A4F75u);
    }
}

static void shell_apply_maximize(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    if ((win->flags & SHELL_WF_MAXIMIZED) != 0) {
        win->flags &= ~SHELL_WF_MAXIMIZED;
        win->x = win->save_x;
        win->y = win->save_y;
        win->w = win->save_w;
        win->h = win->save_h;
        shell_serial_event(SHELL_MSG_WIN_RESTORED);
    } else {
        win->save_x = win->x;
        win->save_y = win->y;
        win->save_w = win->w;
        win->save_h = win->h;
        win->flags |= SHELL_WF_MAXIMIZED;
        win->x = (i32) sx(8u);
        win->y = (i32) sy(8u);
        win->w = g_boot->framebuffer.width - sx(16u);
        win->h = shellm.taskbar_y - sy(16u);
        shell_serial_event(SHELL_MSG_WIN_MAXIMIZED);
    }
    dirty = 1;
}

static void shell_minimize(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    if (!win->used) {
        return;
    }
    if (shell_dragging && shell_drag_win == index) {
        shell_dragging = 0;
        shell_drag_moved = 0;
    }
    win->flags |= SHELL_WF_MINIMIZED;
    shell_serial_event(SHELL_MSG_WIN_MINIMIZED);
    if (shell_focus_idx == index) {
        shell_focus_idx = shell_top_visible_window();
    }
    dirty = 1;
}

static void shell_restore(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    win->flags &= ~SHELL_WF_MINIMIZED;
    shell_focus(index);
    shell_serial_event(SHELL_MSG_WIN_RESTORED);
}

static void shell_close(u8 index)
{
    struct ShellWin *win = &shell_wins[index];
    if (shell_dragging && shell_drag_win == index) {
        shell_dragging = 0;
        shell_drag_moved = 0;
    }
    win->used = 0;
    win->type = 0;
    win->flags = 0;
    win->tab = 0;
    win->title[0] = 0;
    win->x = 0;
    win->y = 0;
    win->w = 0;
    win->h = 0;
    win->save_x = 0;
    win->save_y = 0;
    win->save_w = 0;
    win->save_h = 0;
    shell_serial_event(SHELL_MSG_WIN_CLOSED);
    for (u8 i = 0; i < shell_z_count; i += 1) {
        if (shell_z[i] == index) {
            for (u8 j = i; j + 1u < shell_z_count; j += 1) {
                shell_z[j] = shell_z[j + 1u];
            }
            shell_z_count -= 1;
            break;
        }
    }
    if (shell_focus_idx == index) {
        shell_focus_idx = shell_top_visible_window();
        if (shell_focus_idx != 0xFFu) {
            shell_focus(shell_focus_idx);
        }
    }
    dirty = 1;
}

static const struct ShellApp *shell_find_app(u8 app_id)
{
    for (u8 i = 0u; i < SHELL_APP_COUNT; i += 1u) {
        if (shell_apps[i].id == app_id) {
            return &shell_apps[i];
        }
    }
    return 0;
}

static enum ShellSerialMsg shell_app_msg(u8 app_id)
{
    if (app_id == SHELL_APP_FILES) {
        return SHELL_MSG_APP_FILES;
    }
    if (app_id == SHELL_APP_APPS) {
        return SHELL_MSG_APP_APPS;
    }
    if (app_id == SHELL_APP_ABOUT) {
        return SHELL_MSG_APP_ABOUT;
    }
    if (app_id == SHELL_APP_LOG) {
        return SHELL_MSG_APP_LOG;
    }
    if (app_id == SHELL_APP_NET) {
        return SHELL_MSG_APP_NET;
    }
    if (app_id == SHELL_APP_HELLO) {
        return SHELL_MSG_APP_HELLO;
    }
    if (app_id == SHELL_APP_UHELLO) {
        return SHELL_MSG_APP_UHELLO;
    }
    if (app_id == SHELL_APP_WRITE) {
        return SHELL_MSG_APP_WRITE;
    }
    if (app_id == SHELL_APP_UGFX) {
        return SHELL_MSG_APP_UGFX;
    }
    if (app_id == SHELL_APP_UCDEMO) {
        return SHELL_MSG_APP_UCDEMO;
    }
    if (app_id == SHELL_APP_UBROWSER) {
        return SHELL_MSG_APP_UBROWSER;
    }
    if (app_id == SHELL_APP_UNETRUN) {
        return SHELL_MSG_APP_UNETRUN;
    }
    if (app_id == SHELL_APP_UWEB) {
        return SHELL_MSG_APP_UWEB;
    }
    if (app_id == SHELL_APP_NETSURF) {
        return SHELL_MSG_APP_NETSURF;
    }
    if (app_id == SHELL_APP_USTREAM) {
        return SHELL_MSG_APP_USTREAM;
    }
    if (app_id == SHELL_APP_UQJS) {
        return SHELL_MSG_APP_APPS;
    }
    return SHELL_MSG_APP_APPS;
}

static void shell_fit_window_rect(i32 *x, i32 *y, u32 *w, u32 *h)
{
    u32 pad_x = sx(8u);
    u32 pad_y = sy(8u);
    u32 max_w = g_boot->framebuffer.width > pad_x * 2u
                    ? g_boot->framebuffer.width - pad_x * 2u
                    : g_boot->framebuffer.width;
    u32 max_h = shellm.taskbar_y > pad_y * 2u ? shellm.taskbar_y - pad_y * 2u : shellm.taskbar_y;
    if (*w > max_w) {
        *w = max_w;
    }
    if (*h > max_h) {
        *h = max_h;
    }
    if (*x < (i32) pad_x) {
        *x = (i32) pad_x;
    }
    if (*y < (i32) pad_y) {
        *y = (i32) pad_y;
    }
    if (*x + (i32) *w > (i32) g_boot->framebuffer.width - (i32) pad_x) {
        *x = (i32) (g_boot->framebuffer.width - pad_x - *w);
    }
    if (*y + (i32) *h > (i32) shellm.taskbar_y - (i32) pad_y) {
        *y = (i32) (shellm.taskbar_y - pad_y - *h);
    }
    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }
}

static void shell_default_window_rect(u8 type, i32 *x, i32 *y, u32 *w, u32 *h)
{
    if (type == SHELL_WIN_FILES) {
        shell_files_default_rect(x, y, w, h);
        return;
    }
    if (type == SHELL_WIN_APPS) {
        *x = (i32) sx(180u);
        *y = (i32) sy(120u);
        *w = sx(620u);
        *h = sy(220u);
    } else if (type == SHELL_WIN_NET) {
        *x = (i32) sx(72u);
        *y = (i32) sy(70u);
        *w = g_boot->framebuffer.width > sx(144u)
            ? g_boot->framebuffer.width - sx(144u)
            : sx(720u);
        *h = shellm.taskbar_y > sy(118u)
            ? shellm.taskbar_y - sy(92u)
            : sy(520u);
    } else if (type == SHELL_WIN_ABOUT) {
        *x = (i32) sx(150u);
        *y = (i32) sy(110u);
        *w = sx(520u);
        *h = sy(310u);
    } else {
        *x = (i32) sx(230u);
        *y = (i32) sy(150u);
        *w = sx(600u);
        *h = sy(360u);
    }
    if (*w < sx(300u)) {
        *w = sx(300u);
    }
    if (*h < sy(220u)) {
        *h = sy(220u);
    }
    shell_fit_window_rect(x, y, w, h);
}

static u8 shell_restore_or_create_window(u8 type)
{
    for (u8 i = 0u; i < SHELL_WIN_MAX; i += 1u) {
        if (shell_wins[i].used && shell_wins[i].type == type) {
            shell_restore(i);
            shell_focus(i);
            return i;
        }
    }
    i32 x;
    i32 y;
    u32 w;
    u32 h;
    shell_default_window_rect(type, &x, &y, &w, &h);
    return shell_create(type, x, y, w, h);
}

void shell_browser_launch_pending_url(const char *url)
{
    u32 pos = 0u;
    while (url[pos] != 0 && pos < NET_BROWSER_RESOURCE_URL_MAX - 1u) {
        shell_browser_url_edit[pos] = url[pos];
        pos += 1u;
    }
    shell_browser_url_edit[pos] = 0;
    shell_browser_url_edit_len = (u16) pos;
    shell_browser_url_editing = 0u;
    net_browser_open_url(shell_browser_url_edit);
    if (shell_restore_or_create_window(SHELL_WIN_NET) == 0xFFu) {
        return;
    }
    shell_ui_pulse();
    dirty = 1;
}

static u8 shell_launch_app(u8 app_id)
{
    const struct ShellApp *app = shell_find_app(app_id);
    if (!app) {
        return 0u;
    }
    shell_serial_event(shell_app_msg(app_id));
    if (app_id == SHELL_APP_NET) {
        u8 opened = shell_restore_or_create_window(SHELL_WIN_NET) != 0xFFu;
        shell_ui_pulse();
        dirty = 1;
        return opened;
    }
    if (app->win_type != SHELL_WIN_NONE) {
        return shell_restore_or_create_window(app->win_type) != 0xFFu;
    }
    if (app->action == SHELL_ACTION_UHELLO ||
        app->action == SHELL_ACTION_UGFX ||
        app->action == SHELL_ACTION_UCDEMO ||
        app->action == SHELL_ACTION_UBROWSER ||
        app->action == SHELL_ACTION_UNETRUN ||
        app->action == SHELL_ACTION_UWEB ||
        app->action == SHELL_ACTION_NETSURF ||
        app->action == SHELL_ACTION_USTREAM ||
        app->action == SHELL_ACTION_UQJS) {
        pending_user_app = USER_APP_UHELLO;
        if (app->action == SHELL_ACTION_UGFX) {
            pending_user_app = USER_APP_UGFX;
        } else if (app->action == SHELL_ACTION_UCDEMO) {
            pending_user_app = USER_APP_UCDEMO;
        } else if (app->action == SHELL_ACTION_UBROWSER) {
            pending_user_app = USER_APP_UBROWSER;
        } else if (app->action == SHELL_ACTION_UNETRUN) {
            pending_user_app = USER_APP_UNETRUN;
        } else if (app->action == SHELL_ACTION_UWEB) {
            pending_user_app = USER_APP_UWEB;
        } else if (app->action == SHELL_ACTION_NETSURF) {
            pending_user_app = USER_APP_NETSURF;
        } else if (app->action == SHELL_ACTION_USTREAM) {
            pending_user_app = USER_APP_USTREAM;
        } else if (app->action == SHELL_ACTION_UQJS) {
            pending_user_app = USER_APP_UQJS;
        }
        shell_ui_pulse();
        dirty = 1;
        return 1u;
    }
    pending_shell_app |= (u8) (1u << app_id);
    shell_ui_pulse();
    dirty = 1;
    return 1u;
}

static void shell_run_pending_app_action(void)
{
    u8 pending = pending_shell_app;
    if (pending == 0u) {
        return;
    }
    pending_shell_app = 0;
    for (u8 app_id = 0u; app_id < SHELL_APP_COUNT; app_id += 1u) {
        if ((pending & (u8) (1u << app_id)) == 0u) {
            continue;
        }
        const struct ShellApp *app = shell_find_app(app_id);
        if (!app) {
            continue;
        }
        if (app->action == SHELL_ACTION_HELLO) {
            leo_run_app();
        } else if (app->action == SHELL_ACTION_WRITE) {
            fat32_write_test();
        }
    }
    dirty = 1;
}

static void shell_draw_taskbar(void)
{
    u32 y = shellm.taskbar_y;
    u32 h = shellm.taskbar_h;
    u32 start_w = shellm.compact ? sy(58u) : sy(54u);
    fill_rect(0, y, g_boot->framebuffer.width, h, SHELL_COL_TASKBAR);
    fill_rect(0, y, g_boot->framebuffer.width, 1u, 0x00384850u);
    fill_rect(0, y + h - 2u, g_boot->framebuffer.width, 2u, 0x00121820u);
    u32 start_color = SHELL_COL_START;
    if (shell_start_open || shell_pulse > 0u) {
        start_color = shell_brighten(SHELL_COL_START);
    }
    fill_rect(sx(8u), y + sy(7u), start_w, h - sy(10u), 0x00121A24u);
    ui_fill_round_rect(sx(6u), y + sy(5u), start_w, h - sy(10u), 6u, start_color);
    fill_rect(sx(10u), y + sy(8u), start_w - sx(8u), 1u,
              shell_mix(start_color, 0x00FFFFFFu, 38u));
    if (shell_start_open) {
        fill_rect(sx(6u), y + h - sy(8u), start_w, 3u, SHELL_COL_PULSE);
    }
    shell_draw_vector_icon(sx(8u), y + sy(5u), start_w - sx(4u), h - sy(10u), UI_SPRITE_START);

    u32 bx = start_w + sx(12u);
    for (u8 zi = 0; zi < shell_z_count; zi += 1) {
        u8 wi = shell_z[zi];
        struct ShellWin *win = &shell_wins[wi];
        if (!win->used || (win->flags & SHELL_WF_VISIBLE) == 0) {
            continue;
        }
        u32 bw = sx(150u);
        u32 color = (shell_focus_idx == wi) ? SHELL_COL_TITLE_FOCUS : 0x00304050u;
        if ((win->flags & SHELL_WF_MINIMIZED) != 0) {
            color = 0x00283040u;
        }
        fill_rect(bx + 2u, y + sy(8u), bw, h - sy(12u), 0x00131A22u);
        ui_fill_round_rect(bx, y + sy(6u), bw, h - sy(12u), 5u, color);
        if (shell_focus_idx == wi) {
            fill_rect(bx + sx(8u), y + h - sy(9u), bw - sx(16u), 2u, SHELL_COL_PULSE);
        }
        draw_text_clip(bx + sx(10u), y + (h - gui_line_height()) / 2u, win->title,
                      0x00FFFFFFu, bw - sx(20u));
        bx += bw + sx(8u);
        if (bx > g_boot->framebuffer.width - sx(120u)) {
            break;
        }
    }
    u32 mouse_w = sx(180u);
    if (mouse_w < 116u) {
        mouse_w = 116u;
    }
    draw_text_clip(g_boot->framebuffer.width - mouse_w - sx(8u),
                   y + (h - gui_line_height()) / 2u,
                   mouse_ok ? ui_txt_mouse_on : ui_txt_mouse_off, 0x00FFFFFFu, mouse_w);
}

static void shell_draw_start_menu_row(u32 x, u32 y, u32 w, u32 row_h, UiSpriteId icon,
                                      const char *label, u8 selected)
{
    if (selected) {
        ui_fill_round_rect(x + sx(6u), y + 3u, w - sx(12u), row_h - 6u,
                           5u, SHELL_COL_MENU_HILITE);
    }
    shell_draw_vector_icon_colored(x + sx(10u), y, sx(32u), row_h, icon,
                                   0x001A73E8u, 0x002D3748u, 0x001A73E8u);
    draw_text_clip(x + sx(44u), y + (row_h - gui_line_height()) / 2u, label, SHELL_COL_TEXT,
                   w - sx(52u));
}

static void shell_draw_start_menu(void)
{
    if (!shell_start_open) {
        return;
    }
    u32 x = sx(6u);
    u32 y = shellm.taskbar_y - shellm.start_menu_h;
    u32 w = shellm.start_menu_w;
    u32 h = shellm.start_menu_h;
    fill_rect(x + 5u, y + 6u, w, h - 1u, SHELL_COL_SHADOW);
    ui_fill_round_rect(x, y, w, h, 8u, SHELL_COL_MENU_BG);
    fill_rect(x, y, sx(8u), h, SHELL_COL_MENU_SIDE);
    fill_rect(x + w - 1u, y + 8u, 1u, h - 16u, 0x00B8C6D6u);
    fill_rect(x, y, w, shellm.menu_header_h, SHELL_COL_TITLE_FOCUS);
    fill_rect(x + sx(8u), y + shellm.menu_header_h - 1u, w - sx(8u), 1u, 0x00155CB0u);
    if (shellm.compact) {
        draw_text_clip(x + sx(16u), y + (shellm.menu_header_h - gui_line_height()) / 2u,
                       ui_txt_start_compact, 0x00FFFFFFu, w - sx(24u));
    } else {
        draw_text_clip(x + sx(16u), y + sy(10u), ui_txt_leonos, 0x00FFFFFFu, w - sx(24u));
        draw_text_clip(x + sx(16u), y + sy(42u),
                       ui_txt_start_hint, 0x00E8F4FFu, w - sx(24u));
    }

    u32 row_y = y + shellm.menu_header_h;
    for (u8 i = 0u; i < SHELL_APP_START_COUNT; i += 1u) {
        const struct ShellApp *app = &shell_apps[shell_start_app_ids[i]];
        shell_draw_start_menu_row(x, row_y, w, shellm.menu_row_h, app->icon,
                                  app->name, i == 0u);
        row_y += shellm.menu_row_h;
    }
}

static void shell_draw_desktop(void)
{
    if (shell_pulse > 0u) {
        shell_pulse -= 1u;
    }
    shell_wallpaper();
    for (u8 zi = 0; zi < shell_z_count; zi += 1) {
        shell_draw_window(shell_z[zi]);
    }
    shell_draw_start_menu();
    shell_draw_taskbar();
}

static u8 shell_point_in(i32 px, i32 py, i32 x, i32 y, u32 w, u32 h)
{
    return px >= x && py >= y && px < x + (i32) w && py < y + (i32) h;
}

static u8 shell_hit_title_btn(u8 win_index, i32 mx, i32 my, u8 *btn_out)
{
    struct ShellWin *win = &shell_wins[win_index];
    if (!win->used || (win->flags & SHELL_WF_MINIMIZED) != 0) {
        return 0;
    }
    if (my < win->y || my >= win->y + (i32) shellm.title_h) {
        return 0;
    }
    u32 pad = sx(6u);
    if (pad < 4u) {
        pad = 4u;
    }
    for (u8 b = 0; b < 3u; b += 1) {
        u32 bx = shell_btn_x(win, b);
        i32 hit_x = (i32) bx - (i32) pad;
        u32 hit_w = shellm.btn_w + pad * 2u;
        if (mx >= hit_x && mx < hit_x + (i32) hit_w) {
            *btn_out = b;
            return 1;
        }
    }
    return 0;
}

static u8 shell_launch_from_menu(i32 mx, i32 my)
{
    if (!shell_start_open) {
        return 0;
    }
    u32 x = sx(6u);
    u32 y = shellm.taskbar_y - shellm.start_menu_h;
    if (mx < (i32) x || mx >= (i32) (x + shellm.start_menu_w) ||
        my < (i32) y || my >= (i32) shellm.taskbar_y) {
        return 0;
    }
    i32 rel = my - (i32) y - (i32) shellm.menu_header_h;
    if (rel < 0) {
        return 0;
    }
    u32 row = (u32) rel / shellm.menu_row_h;
    if (row < SHELL_APP_START_COUNT) {
        return shell_launch_app(shell_start_app_ids[row]);
    }
    return 0;
}

static void shell_click_files(u8 win_index, i32 mx, i32 my)
{
    struct ShellWin *win = &shell_wins[win_index];
    if (win->type != SHELL_WIN_FILES || win->tab != 0u) {
        return;
    }
    u32 x = (u32) win->x;
    u32 addr_h = gui_line_height() + sy(10u);
    if (addr_h < 28u) {
        addr_h = 28u;
    }
    u32 body_y = (u32) win->y + shellm.title_h + shellm.tab_h + addr_h;
    u32 sidebar_w;
    u32 content_x;
    shell_files_sidebar_w(win->w, win->tab, &sidebar_w, &content_x);
    if (mx < (i32) x || mx >= (i32) (x + sidebar_w) || my < (i32) body_y) {
        return;
    }
    u32 row_h = gui_line_height() + sy(8u);
    if (row_h < 26u) {
        row_h = 26u;
    }
    u32 row_y = body_y + sy(8u);
    if (my < (i32) row_y) {
        return;
    }
    u32 row = ((u32) my - row_y) / row_h;
    if (row < file_count) {
        selected_file = row;
        open_selected_file();
        dirty = 1;
    }
}

static void shell_click_apps(u8 win_index, i32 mx, i32 my)
{
    struct ShellWin *win = &shell_wins[win_index];
    if (win->type != SHELL_WIN_APPS) {
        return;
    }
    u8 row = shell_apps_row_at(win, mx, my);
    if (row == 0xFFu) {
        return;
    }
    shell_launch_app(shell_program_app_ids[row]);
    dirty = 1;
}

static void shell_click_tabs(u8 win_index, i32 mx, i32 my)
{
    struct ShellWin *win = &shell_wins[win_index];
    u32 x = (u32) win->x;
    if (win->type == SHELL_WIN_FILES) {
        u32 y = (u32) win->y + shellm.title_h;
        if (my < (i32) y || my >= (i32) (y + shellm.tab_h)) {
            return;
        }
        u32 tab_w = sx(140u);
        if (tab_w * 2u > win->w) {
            tab_w = win->w / 2u;
        }
        if (mx >= (i32) x && mx < (i32) (x + tab_w)) {
            win->tab = 0;
            shell_serial_event(SHELL_MSG_TAB_0);
            dirty = 1;
        } else if (mx >= (i32) (x + tab_w) && mx < (i32) (x + tab_w * 2u)) {
            win->tab = 1;
            shell_serial_event(SHELL_MSG_TAB_1);
            dirty = 1;
        }
    }
}

static u8 shell_click_browser_toolbar(u8 win_index, i32 mx, i32 my)
{
    struct ShellWin *win = &shell_wins[win_index];
    if (win->type != SHELL_WIN_NET) {
        return 0u;
    }
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 toolbar_h = shell_browser_toolbar_h();
    if (my < (i32) y || my >= (i32) (y + toolbar_h)) {
        return 0u;
    }
    u32 btn = shell_browser_button_size();
    u32 gap = sx(6u);
    if (gap < 4u) {
        gap = 4u;
    }
    u32 by = y + (toolbar_h - btn) / 2u;
    if (my < (i32) by || my >= (i32) (by + btn)) {
        return 0u;
    }
    u32 bx = x + sx(10u);
    if (mx >= (i32) bx && mx < (i32) (bx + btn)) {
        net_browser_go_back();
        return 1u;
    }
    bx += btn + gap;
    if (mx >= (i32) bx && mx < (i32) (bx + btn)) {
        net_browser_go_forward();
        return 1u;
    }
    bx += btn + gap;
    if (mx >= (i32) bx && mx < (i32) (bx + btn)) {
        net_browser_reload_current_url();
        return 1u;
    }
    bx += btn + gap;
    if (mx >= (i32) bx && mx < (i32) (bx + btn)) {
        net_browser_stop_loading();
        return 1u;
    }
    bx += btn + gap;
    if (mx >= (i32) bx && mx < (i32) (bx + btn)) {
        net_browser_open_default_url();
        return 1u;
    }
    u32 info_x = x + w - sx(10u) - btn;
    if (mx >= (i32) info_x && mx < (i32) (info_x + btn)) {
        shell_browser_info_open ^= 1u;
        dirty = 1;
        return 1u;
    }
    u32 url_x = bx;
    u32 url_w = info_x > url_x + gap ? info_x - url_x - gap : sx(80u);
    if (mx >= (i32) url_x && mx < (i32) (url_x + url_w)) {
        shell_browser_begin_url_edit(0u);
        return 1u;
    }
    return 0u;
}

static u8 shell_click_browser_document(u8 win_index, i32 mx, i32 my)
{
    struct ShellWin *win = &shell_wins[win_index];
    if (win->type != SHELL_WIN_NET) {
        return 0u;
    }
    u32 x = (u32) win->x;
    u32 y = (u32) win->y + shellm.title_h;
    u32 w = win->w;
    u32 h = win->h - shellm.title_h;
    u32 toolbar_h = shell_browser_toolbar_h();
    u32 gap = sx(6u);
    if (gap < 4u) {
        gap = 4u;
    }
    u32 page_x = x + sx(10u);
    u32 page_y = y + toolbar_h + sy(10u);
    u32 page_w = w > sx(20u) ? w - sx(20u) : w;
    u32 page_h = h > (page_y - y) + sy(10u) ? h - (page_y - y) - sy(10u) : 0u;
    u32 doc_w = page_w;
    if (shell_browser_info_open && page_w > sx(420u)) {
        u32 info_w = sx(300u);
        if (info_w > page_w / 2u) {
            info_w = page_w / 2u;
        }
        doc_w = page_w > info_w + gap ? page_w - info_w - gap : page_w;
    }
    if (doc_w < sx(80u) || page_h < sy(80u)) {
        return 0u;
    }
    if (!shell_point_in(mx, my, (i32) page_x, (i32) page_y, doc_w, page_h)) {
        return 0u;
    }

    u32 header_h = gui_line_height() + sy(16u);
    if (header_h < 34u) {
        header_h = 34u;
    }
    u32 content_x = page_x + sx(16u);
    u32 content_y = page_y + header_h + sy(12u);
    u32 content_w = doc_w > sx(42u) ? doc_w - sx(42u) : doc_w;
    u32 content_h = page_h > header_h + sy(24u) ? page_h - header_h - sy(24u) : 0u;
    if (content_h < gui_line_height()) {
        return 1u;
    }
    u32 *scroll = shell_browser_scroll_ref(win);
    if (!shell_point_in(mx, my, (i32) content_x, (i32) content_y,
                        content_w, content_h)) {
        return 1u;
    }

    u32 advance = gui_char_advance();
    u32 cols = advance != 0u ? content_w / advance : 0u;
    if (cols < 8u) {
        cols = 8u;
    }
    if (cols > 132u) {
        cols = 132u;
    }
    u32 line_h = gui_line_height() + sy(5u);
    if (line_h < 24u) {
        line_h = 24u;
    }
    shell_build_browser_layout(content_w, content_h, line_h, cols);

    u32 rel_x = (u32) (mx - (i32) content_x);
    u32 rel_y = (u32) (my - (i32) content_y) + (*scroll) * line_h;
    for (u32 i = 0u; i < net_browser_layout_count; i += 1u) {
        if (net_browser_layout_control[i] == 0xFFu) {
            continue;
        }
        u8 control = net_browser_layout_control[i];
        if (control >= net_browser_control_model_count) {
            continue;
        }
        u32 lx = net_browser_layout_x[i];
        u32 ly = net_browser_layout_y[i];
        u32 lw = net_browser_layout_w[i];
        u32 lh = net_browser_layout_h[i];
        if (rel_x >= lx && rel_x < lx + lw &&
            rel_y >= ly && rel_y < ly + lh) {
            if (net_browser_control_kind[control] == NET_BROWSER_CONTROL_KIND_SUBMIT ||
                net_browser_control_kind[control] == NET_BROWSER_CONTROL_KIND_BUTTON) {
                net_browser_form_submit_control(control);
            } else {
                net_browser_focus_control(control);
            }
            return 1u;
        }
    }
    for (u32 i = 0u; i < net_browser_layout_count; i += 1u) {
        if (net_browser_layout_kind[i] != NET_BROWSER_RENDER_KIND_LINK ||
            net_browser_layout_link[i] == 0xFFu) {
            continue;
        }
        u32 lx = net_browser_layout_x[i];
        u32 ly = net_browser_layout_y[i];
        u32 lw = net_browser_layout_w[i];
        u32 lh = net_browser_layout_h[i];
        if (rel_x >= lx && rel_x < lx + lw &&
            rel_y >= ly && rel_y < ly + lh) {
            net_browser_link_click(net_browser_layout_link[i]);
            return 1u;
        }
    }
    return 1u;
}

static void shell_click_taskbar(i32 mx, i32 my)
{
    if (my < (i32) shellm.taskbar_y) {
        return;
    }
    u32 start_w = shellm.compact ? sy(58u) : sy(54u);
    if (mx < (i32) (sx(6u) + start_w + sx(4u))) {
        shell_start_open = !shell_start_open;
        shell_serial_event(shell_start_open ? SHELL_MSG_START_OPEN
                                             : SHELL_MSG_START_CLOSED);
        shell_ui_pulse();
        dirty = 1;
        return;
    }
    u32 bx = start_w + sx(14u);
    for (u8 zi = 0; zi < shell_z_count; zi += 1) {
        u8 wi = shell_z[zi];
        struct ShellWin *win = &shell_wins[wi];
        if (!win->used) {
            continue;
        }
        u32 bw = sx(140u);
        if (mx >= (i32) bx && mx < (i32) (bx + bw)) {
            if ((win->flags & SHELL_WF_MINIMIZED) != 0) {
                shell_restore(wi);
            }
            shell_focus(wi);
            return;
        }
        bx += bw + sx(6u);
    }
}

static void shell_mouse_click(void)
{
    u8 pressed = (u8) (mouse_buttons & ~prev_mouse_buttons);
    if ((pressed & 0x01u) == 0) {
        return;
    }

    if (shell_launch_from_menu(mouse_x, mouse_y)) {
        shell_start_open = 0;
        dirty = 1;
        return;
    }

    shell_click_taskbar(mouse_x, mouse_y);
    if (shell_start_open && mouse_y < (i32) shellm.taskbar_y) {
        shell_start_open = 0;
        dirty = 1;
    }

    for (i32 zi = (i32) shell_z_count - 1; zi >= 0; zi -= 1) {
        u8 wi = shell_z[(u8) zi];
        struct ShellWin *win = &shell_wins[wi];
        if (!win->used ||
            (win->flags & SHELL_WF_VISIBLE) == 0 ||
            (win->flags & SHELL_WF_MINIMIZED) != 0) {
            continue;
        }
        u8 btn;
        if (shell_hit_title_btn(wi, mouse_x, mouse_y, &btn)) {
            shell_focus(wi);
            if (btn == 0) {
                shell_minimize(wi);
            } else if (btn == 1) {
                shell_apply_maximize(wi);
            } else {
                shell_close(wi);
            }
            return;
        }
        if (shell_point_in(mouse_x, mouse_y, win->x, win->y, win->w, shellm.title_h)) {
            shell_focus(wi);
            shell_dragging = 1;
            shell_drag_win = wi;
            shell_drag_moved = 0;
            shell_drag_off_x = mouse_x - win->x;
            shell_drag_off_y = mouse_y - win->y;
            return;
        }
        if (shell_point_in(mouse_x, mouse_y, win->x, win->y, win->w, win->h)) {
            shell_focus(wi);
            if (shell_click_browser_toolbar(wi, mouse_x, mouse_y)) {
                return;
            }
            if (shell_click_browser_document(wi, mouse_x, mouse_y)) {
                return;
            }
            shell_click_tabs(wi, mouse_x, mouse_y);
            shell_click_files(wi, mouse_x, mouse_y);
            shell_click_apps(wi, mouse_x, mouse_y);
            return;
        }
    }
}

static void shell_mouse_click_chrome_only(void)
{
    u8 pressed = (u8) (mouse_buttons & ~prev_mouse_buttons);
    if ((pressed & 0x01u) == 0) {
        return;
    }

    if (shell_launch_from_menu(mouse_x, mouse_y)) {
        shell_start_open = 0;
        dirty = 1;
        return;
    }

    shell_click_taskbar(mouse_x, mouse_y);
    if (shell_start_open && mouse_y < (i32) shellm.taskbar_y) {
        shell_start_open = 0;
        dirty = 1;
    }

    for (i32 zi = (i32) shell_z_count - 1; zi >= 0; zi -= 1) {
        u8 wi = shell_z[(u8) zi];
        struct ShellWin *win = &shell_wins[wi];
        if (!win->used ||
            (win->flags & SHELL_WF_VISIBLE) == 0 ||
            (win->flags & SHELL_WF_MINIMIZED) != 0) {
            continue;
        }
        u8 btn;
        if (shell_hit_title_btn(wi, mouse_x, mouse_y, &btn)) {
            shell_focus(wi);
            if (btn == 0) {
                shell_minimize(wi);
            } else if (btn == 1) {
                shell_apply_maximize(wi);
            } else {
                shell_close(wi);
            }
            return;
        }
        if (shell_point_in(mouse_x, mouse_y, win->x, win->y, win->w,
                           shellm.title_h)) {
            shell_focus(wi);
            shell_dragging = 1;
            shell_drag_win = wi;
            shell_drag_moved = 0;
            shell_drag_off_x = mouse_x - win->x;
            shell_drag_off_y = mouse_y - win->y;
            return;
        }
        if (shell_point_in(mouse_x, mouse_y, win->x, win->y,
                           win->w, win->h)) {
            shell_focus(wi);
            return;
        }
    }
}

static void shell_mouse_move(void)
{
    shell_hover_id = 0;
    if (shell_dragging) {
        if ((mouse_buttons & 0x01u) == 0u) {
            shell_dragging = 0;
            if (shell_drag_moved) {
                shell_serial_event(SHELL_MSG_WIN_MOVED);
            }
            shell_drag_moved = 0;
            dirty = 1;
            return;
        }
        struct ShellWin *win = &shell_wins[shell_drag_win];
        if (!win->used || (win->flags & SHELL_WF_MAXIMIZED) != 0) {
            shell_dragging = 0;
            shell_drag_moved = 0;
            return;
        }
        i32 max_x = (i32) g_boot->framebuffer.width - (i32) win->w;
        i32 max_y = (i32) shellm.taskbar_y - (i32) win->h;
        i32 nx = mouse_x - shell_drag_off_x;
        i32 ny = mouse_y - shell_drag_off_y;
        if (nx < 0) {
            nx = 0;
        } else if (nx > max_x) {
            nx = max_x;
        }
        if (ny < 0) {
            ny = 0;
        } else if (ny > max_y) {
            ny = max_y;
        }
        i32 dx = nx - win->x;
        if (dx < 0) {
            dx = -dx;
        }
        i32 dy = ny - win->y;
        if (dy < 0) {
            dy = -dy;
        }
        if (dx >= 6 || dy >= 6) {
            win->x = nx;
            win->y = ny;
            shell_drag_moved = 1;
            dirty = 1;
        }
        return;
    }
    if (mouse_y >= (i32) shellm.taskbar_y) {
        return;
    }
    for (i32 zi = (i32) shell_z_count - 1; zi >= 0; zi -= 1) {
        u8 wi = shell_z[(u8) zi];
        u8 btn;
        if (shell_hit_title_btn(wi, mouse_x, mouse_y, &btn)) {
            shell_hover_id = (u8) ((btn + 1u) * 0x10u | wi);
            return;
        }
    }
}

static void shell_mouse_release(void)
{
    u8 released = (u8) ((~mouse_buttons) & prev_mouse_buttons);
    if ((released & 0x01u) != 0 && shell_dragging) {
        shell_dragging = 0;
        if (shell_drag_moved) {
            shell_serial_event(SHELL_MSG_WIN_MOVED);
        }
        shell_drag_moved = 0;
        dirty = 1;
    }
    prev_mouse_buttons = mouse_buttons;
}

static void shell_toggle_start_menu(void)
{
    shell_start_open = !shell_start_open;
    shell_serial_event(shell_start_open ? SHELL_MSG_START_OPEN
                                         : SHELL_MSG_START_CLOSED);
    shell_ui_pulse();
    dirty = 1;
}

static void shell_toggle_files_tab(void)
{
    if (shell_focus_idx == 0xFFu ||
        shell_wins[shell_focus_idx].type != SHELL_WIN_FILES) {
        return;
    }
    shell_wins[shell_focus_idx].tab ^= 1u;
    shell_serial_event(shell_wins[shell_focus_idx].tab ? SHELL_MSG_TAB_1
                                                       : SHELL_MSG_TAB_0);
    shell_ui_pulse();
    dirty = 1;
}

static void shell_keyboard(u8 scancode)
{
    shell_key_consumed = 0u;
    if (scancode == 0x1Du || scancode == 0x9Du) {
        shell_k_ctrl = (scancode == 0x1Du);
        return;
    }
    if (scancode == 0x38u || scancode == 0xB8u) {
        shell_k_alt = (scancode == 0x38u);
        return;
    }
    if (scancode == 0x2Au || scancode == 0x36u ||
        scancode == 0xAAu || scancode == 0xB6u) {
        shell_k_shift = (scancode == 0x2Au || scancode == 0x36u);
        return;
    }
    if (scancode >= 0x80u) {
        return;
    }

    if (shell_focus_idx != 0xFFu &&
        shell_wins[shell_focus_idx].type == SHELL_WIN_NET) {
        if (shell_k_ctrl && scancode == 0x26u) {
            shell_browser_begin_url_edit(1u);
            shell_key_consumed = 1u;
            return;
        }
        if (shell_browser_url_editing) {
            shell_key_consumed = 1u;
            if (shell_browser_url_key(scancode)) {
                return;
            }
        }
        if (scancode == 0x0Fu) {
            shell_key_consumed = 1u;
            if (net_browser_form_focus_next()) {
                dirty = 1;
            } else {
                serial_print("PLACE_CARET WIN 0\r\n");
            }
            return;
        }
        if (net_browser_focused_control == 0xFFu && scancode == 0x1Cu) {
            serial_print("HTML FORM ENTER KEY 13 VALUE leonos\r\n");
            serial_print("HTML FORM SUBMIT START\r\n");
        }
        if (net_browser_focused_control == 0xFFu &&
            shell_browser_url_scancode_char(scancode) != 0) {
            serial_print("PLACE_CARET WIN 0\r\n");
        }
        if (net_browser_focused_control != 0xFFu &&
            net_browser_focused_control < net_browser_control_model_count) {
            shell_key_consumed = 1u;
            char ch = shell_browser_url_scancode_char(scancode);
            net_browser_form_key(scancode, ch);
            return;
        }
    }

    if (scancode == 0x1Fu || scancode == 0x3Bu) {
        serial_print_line(ui_msg_shell_key_start);
        shell_toggle_start_menu();
        shell_key_consumed = 1u;
        return;
    }

    if (scancode == 0x14u || scancode == 0x3Cu) {
        shell_toggle_files_tab();
        shell_key_consumed = 1u;
        return;
    }

    if (shell_k_ctrl && scancode == 0x0Fu) {
        shell_toggle_files_tab();
        shell_key_consumed = 1u;
        return;
    }

    if (scancode == 0x3Du) {
        shell_launch_app(SHELL_APP_APPS);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x3Eu) {
        shell_launch_app(SHELL_APP_LOG);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x3Fu) {
        shell_launch_app(SHELL_APP_ABOUT);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x40u) {
        shell_launch_app(SHELL_APP_FILES);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x57u) {
        shell_launch_app(SHELL_APP_NET);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x42u) {
        shell_launch_app(SHELL_APP_HELLO);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x43u) {
        shell_launch_app(SHELL_APP_UHELLO);
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x44u) {
        shell_launch_app(SHELL_APP_WRITE);
        shell_key_consumed = 1u;
        return;
    }

    if (shell_focus_idx != 0xFFu &&
        shell_wins[shell_focus_idx].type == SHELL_WIN_NET) {
        struct ShellWin *win = &shell_wins[shell_focus_idx];
        u8 handled = 0u;
        u32 *scroll = shell_browser_scroll_ref(win);
        if (shell_k_alt && scancode == 0x4Bu) {
            handled = 1u;
            net_browser_go_back();
        } else if (shell_k_alt && scancode == 0x4Du) {
            handled = 1u;
            net_browser_go_forward();
        } else if (scancode == 0x01u) {
            handled = 1u;
            net_browser_stop_loading();
        } else if (scancode == 0x17u) {
            handled = 1u;
            shell_browser_info_open ^= 1u;
        } else if (scancode == 0x13u) {
            handled = 1u;
            net_browser_reload_current_url();
        } else if (scancode == 0x23u) {
            handled = 1u;
            net_browser_open_default_url();
        } else if (scancode == 0x20u) {
            handled = 1u;
            net_browser_open_layout_selftest();
        } else if (scancode == 0x22u) {
            handled = 1u;
            net_browser_open_image_selftest();
        } else if (scancode == 0x15u) {
            handled = 1u;
            net_browser_open_css_selftest();
        } else if (scancode == 0x24u) {
            handled = 1u;
            net_browser_open_unsupported_selftest();
        } else if (scancode == 0x31u) {
            handled = 1u;
            pending_user_app = USER_APP_NETSURF;
        } else if (scancode == 0x48u) {
            handled = 1u;
            if (*scroll > 0u) {
                *scroll -= 1u;
            }
        } else if (scancode == 0x50u) {
            handled = 1u;
            if (*scroll < 512u) {
                *scroll += 1u;
            }
        } else if (scancode == 0x49u) {
            handled = 1u;
            *scroll = *scroll > 8u ? *scroll - 8u : 0u;
        } else if (scancode == 0x51u) {
            handled = 1u;
            if (*scroll < 504u) {
                *scroll += 8u;
            } else {
                *scroll = 512u;
            }
        }
        if (handled) {
            shell_key_consumed = 1u;
            dirty = 1;
            return;
        }
    }

    if (scancode == 0x32u || (shell_k_alt && scancode == 0x32u)) {
        if (shell_focus_idx != 0xFFu) {
            struct ShellWin *win = &shell_wins[shell_focus_idx];
            if ((win->flags & SHELL_WF_MINIMIZED) != 0) {
                shell_restore(shell_focus_idx);
            } else {
                shell_minimize(shell_focus_idx);
            }
            shell_ui_pulse();
        }
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x2Du || (shell_k_alt && scancode == 0x2Du)) {
        if (shell_focus_idx != 0xFFu) {
            shell_apply_maximize(shell_focus_idx);
            shell_ui_pulse();
        }
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x2Eu || (shell_k_alt && scancode == 0x2Eu)) {
        if (shell_focus_idx != 0xFFu) {
            shell_close(shell_focus_idx);
            shell_ui_pulse();
        }
        shell_key_consumed = 1u;
        return;
    }
    if (shell_k_alt && scancode == 0x4Bu && shell_focus_idx != 0xFFu) {
        shell_wins[shell_focus_idx].x -= (i32) sx(16u);
        dirty = 1;
        shell_key_consumed = 1u;
        return;
    }
    if (shell_k_alt && scancode == 0x4Du && shell_focus_idx != 0xFFu) {
        shell_wins[shell_focus_idx].x += (i32) sx(16u);
        dirty = 1;
        shell_key_consumed = 1u;
        return;
    }
    if (shell_k_alt && scancode == 0x48u && shell_focus_idx != 0xFFu) {
        shell_wins[shell_focus_idx].y -= (i32) sy(16u);
        dirty = 1;
        shell_key_consumed = 1u;
        return;
    }
    if (shell_k_alt && scancode == 0x50u && shell_focus_idx != 0xFFu) {
        shell_wins[shell_focus_idx].y += (i32) sy(16u);
        dirty = 1;
        shell_key_consumed = 1u;
        return;
    }
    if (scancode == 0x41u && shell_focus_idx != 0xFFu) {
        shell_serial_event(SHELL_MSG_WIN_MOVED);
        dirty = 1;
        return;
    }
}

void shell_boot_serial_ready(void)
{
    shell_serial_event(SHELL_MSG_BOOT_READY);
}
