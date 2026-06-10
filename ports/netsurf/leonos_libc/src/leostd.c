#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <iconv.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#ifdef LEONOS_USER_APP
#include "leonos_user.h"
#endif

#define LEONOS_PROBE_HEAP_SIZE (256u * 1024u)
#define LEONOS_ALLOC_HEADER_SIZE 8u
#ifndef LEONOS_USER_APP
static unsigned char probe_heap[LEONOS_PROBE_HEAP_SIZE];
static size_t probe_heap_used;
#endif
int errno;

struct leonos_file {
    int dummy;
};

struct leonos_dir {
    int dummy;
};

static FILE stdin_file;
static FILE stdout_file;
static FILE stderr_file;
FILE *stdin = &stdin_file;
FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;
static unsigned int rand_state = 1u;

#ifdef LEONOS_USER_APP
static int leonos_netsurf_parser_in_body_for_script;

static unsigned int leonos_stdout_trimmed_len(const char *text)
{
    unsigned int len = 0u;

    while (text[len] != 0) {
        len += 1u;
    }
    while (len != 0u &&
           (text[len - 1u] == '\n' || text[len - 1u] == '\r')) {
        len -= 1u;
    }
    return len;
}

static int leonos_stdout_trimmed_equals(const char *text, const char *value)
{
    unsigned int len = leonos_stdout_trimmed_len(text);
    unsigned int i = 0u;

    while (value[i] != 0 && i < len && text[i] == value[i]) {
        i += 1u;
    }
    return i == len && value[i] == 0;
}

static int leonos_stdout_trimmed_starts_with(const char *text,
                                             const char *prefix)
{
    unsigned int len = leonos_stdout_trimmed_len(text);
    unsigned int i = 0u;

    while (prefix[i] != 0) {
        if (i >= len || text[i] != prefix[i]) {
            return 0;
        }
        i += 1u;
    }
    return 1;
}

static int leonos_stdout_hubbub_state(const char *text)
{
    unsigned int len = leonos_stdout_trimmed_len(text);
    int has_underscore = 0;

    if (len == 0u || len > 48u) {
        return 0;
    }
    for (unsigned int i = 0u; i < len; i += 1u) {
        if (text[i] == '_') {
            has_underscore = 1;
        } else if (text[i] < 'A' || text[i] > 'Z') {
            return 0;
        }
    }
    if (leonos_stdout_trimmed_equals(text, "INITIAL") ||
        leonos_stdout_trimmed_equals(text, "TEXT")) {
        return 1;
    }
    return has_underscore &&
           (leonos_stdout_trimmed_starts_with(text, "BEFORE_") ||
            leonos_stdout_trimmed_starts_with(text, "IN_") ||
            leonos_stdout_trimmed_starts_with(text, "AFTER_") ||
            leonos_stdout_trimmed_starts_with(text, "GENERIC_") ||
            leonos_stdout_trimmed_starts_with(text, "SCRIPT_"));
}

static int leonos_stdout_suppress(const char *text)
{
    static unsigned int saw_before_html;
    static unsigned int saw_in_body;

    if (strstr(text, "IN_BODY") != NULL) {
        leonos_netsurf_parser_in_body_for_script = 1;
    }
    if (strncmp(text, "GENERIC POLL TIMED", 18) == 0 ||
        strncmp(text, "WINDOW SET_STATUS", 17) == 0 ||
        strncmp(text, "WINDOW START_THROBBER", 21) == 0 ||
        strncmp(text, "WINDOW STOP_THROBBER", 20) == 0 ||
        strncmp(text, "WINDOW NEW_ICON", 15) == 0 ||
        strncmp(text, "WINDOW REMOVE_CARET", 19) == 0) {
        return 1;
    }
    if (strncmp(text, "PLOT ", 5) == 0) {
        static unsigned int plot_seen_mask;
        static unsigned int plot_text_seen;
        unsigned int bit = 0u;
        if (strncmp(text, "PLOT CLIP ", 10) == 0) {
            bit = 1u;
        } else if (strncmp(text, "PLOT RECT ", 10) == 0) {
            bit = 2u;
        } else if (strncmp(text, "PLOT TEXT ", 10) == 0) {
            if (strstr(text, " LEN 0 STR ") != NULL) {
                return 1;
            }
            if (plot_text_seen < 32u) {
                plot_text_seen += 1u;
                return 0;
            }
            bit = 4u;
        } else if (strncmp(text, "PLOT BITMAP ", 12) == 0) {
            bit = 8u;
        } else {
            return 1;
        }
        if ((plot_seen_mask & bit) != 0u) {
            return 1;
        }
        plot_seen_mask |= bit;
        return 0;
    }

    if (leonos_stdout_trimmed_equals(text, "BEFORE_HTML")) {
        if (saw_before_html) {
            return 1;
        }
        saw_before_html = 1u;
        return 0;
    }
    if (leonos_stdout_trimmed_equals(text, "IN_BODY")) {
        if (saw_in_body) {
            return 1;
        }
        saw_in_body = 1u;
        leonos_netsurf_parser_in_body_for_script = 1;
        return 0;
    }
    return leonos_stdout_hubbub_state(text);
}

static void leonos_write_stdout(const char *text)
{
    static char line[512];
    static unsigned int line_len;

    for (unsigned int i = 0u; text[i] != 0; i += 1u) {
        if (line_len < sizeof(line) - 1u) {
            line[line_len++] = text[i];
        }
        if (text[i] == '\n' || line_len >= sizeof(line) - 1u) {
            line[line_len] = 0;
            if (!leonos_stdout_suppress(line)) {
                leonos_write(line);
            }
            line_len = 0u;
        }
    }
}

#ifndef LEONOS_NETSURF_START_URL
#if defined(__has_include)
#if __has_include("leonos_netsurf_config.h")
#include "leonos_netsurf_config.h"
#endif
#endif
#endif
#ifndef LEONOS_NETSURF_START_URL
#define LEONOS_NETSURF_START_URL "https://www.google.com/?igu=1&hl=en&gbv=1"
#endif
#ifndef LEONOS_NETSURF_SETTLE_POLLS
#define LEONOS_NETSURF_SETTLE_POLLS 64u
#endif
#ifndef LEONOS_NETSURF_INTERACTIVE
#define LEONOS_NETSURF_INTERACTIVE 0
#endif

#define LEONOS_NETSURF_WINDOW_NEW "WINDOW NEW " LEONOS_NETSURF_START_URL "\n"

static const char *leonos_stdin_script[] = {
    LEONOS_NETSURF_WINDOW_NEW,
#if LEONOS_NETSURF_INTERACTIVE
#else
    "WINDOW EXEC WIN 0 true\n",
    "WINDOW REDRAW 0\n",
    "WINDOW REDRAW 0\n",
    "WINDOW REDRAW 0\n",
    "QUIT\n"
#endif
};
static const unsigned int leonos_stdin_delay_after[] = {
    0u,
#if LEONOS_NETSURF_INTERACTIVE
#else
    1024u, 1024u, 1024u, 1024u, 0u
#endif
};
static unsigned int leonos_stdin_script_index;
static unsigned int leonos_stdin_ready_delay;
static unsigned int leonos_stdin_settle_polls;
static unsigned int leonos_stdin_fetch_generation;
extern int leonos_netsurf_fetch_finished_for_script;
extern int leonos_netsurf_active_fetches_for_script;
extern unsigned int leonos_netsurf_fetch_generation_for_script;
extern int leonos_netsurf_content_ready_for_script;

#if LEONOS_NETSURF_INTERACTIVE
#define LEONOS_EVENT_KEYBOARD 2u
#define LEONOS_EVENT_MOUSE 3u
#define LEONOS_EVENT_MOUSE_BUTTON 4u
#define LEONOS_NETSURF_VIEW_X 48u
#define LEONOS_NETSURF_VIEW_Y 112u

static char leonos_pending_stdin_line[768];
static int leonos_pending_stdin_ready;
static char leonos_url_edit[512] = LEONOS_NETSURF_START_URL;
static unsigned int leonos_url_edit_len;
static int leonos_url_editing;
static unsigned int leonos_last_redraw_generation;
static unsigned int leonos_interactive_settle_polls;

static void leonos_queue_stdin_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void) vsnprintf(leonos_pending_stdin_line,
                     sizeof(leonos_pending_stdin_line), fmt, ap);
    va_end(ap);
    leonos_pending_stdin_line[sizeof(leonos_pending_stdin_line) - 1u] = 0;
    leonos_pending_stdin_ready = 1;
}

static char leonos_scancode_ascii(unsigned int scancode)
{
    static const char row_q[] = "qwertyuiop";
    static const char row_a[] = "asdfghjkl";
    static const char row_z[] = "zxcvbnm";
    static const char digits[] = "1234567890";

    if (scancode >= 0x10u && scancode <= 0x19u) {
        return row_q[scancode - 0x10u];
    }
    if (scancode >= 0x1Eu && scancode <= 0x26u) {
        return row_a[scancode - 0x1Eu];
    }
    if (scancode >= 0x2Cu && scancode <= 0x32u) {
        return row_z[scancode - 0x2Cu];
    }
    if (scancode >= 0x02u && scancode <= 0x0Bu) {
        return digits[scancode - 0x02u];
    }
    if (scancode == 0x0Cu) { return '-'; }
    if (scancode == 0x0Du) { return '='; }
    if (scancode == 0x1Au) { return '['; }
    if (scancode == 0x1Bu) { return ']'; }
    if (scancode == 0x27u) { return ';'; }
    if (scancode == 0x28u) { return '\''; }
    if (scancode == 0x29u) { return '`'; }
    if (scancode == 0x2Bu) { return '\\'; }
    if (scancode == 0x33u) { return ','; }
    if (scancode == 0x34u) { return '.'; }
    if (scancode == 0x35u) { return '/'; }
    if (scancode == 0x39u) { return ' '; }
    return 0;
}

static void leonos_url_edit_sync_len(void)
{
    leonos_url_edit_len = 0u;
    while (leonos_url_edit[leonos_url_edit_len] != 0 &&
           leonos_url_edit_len < sizeof(leonos_url_edit) - 1u) {
        leonos_url_edit_len += 1u;
    }
}

static void leonos_draw_url_edit(void)
{
    struct leonos_fb_info fb;
    if (!leonos_fb_info(&fb)) {
        return;
    }
    leonos_fb_fill(108u, 20u, fb.width > 216u ? fb.width - 216u : 0u,
                   36u, 0x00FFF7D6u);
    leonos_fb_text(120u, 31u, leonos_url_edit, 0x001A2433u);
    leonos_fb_present();
}

static void leonos_focus_url_edit(void)
{
    leonos_url_editing = 1;
    leonos_url_edit[0] = 0;
    leonos_url_edit_len = 0u;
    leonos_draw_url_edit();
}

static void leonos_blur_url_edit(void)
{
    leonos_url_editing = 0;
}

static void leonos_submit_url_edit(void)
{
    char target[640];
    if (leonos_url_edit_len == 0u) {
        return;
    }
    if (strncmp(leonos_url_edit, "https://", 8u) == 0) {
        snprintf(target, sizeof(target), "%s", leonos_url_edit);
    } else {
        snprintf(target, sizeof(target), "https://%s", leonos_url_edit);
    }
    leonos_blur_url_edit();
    leonos_queue_stdin_line("WINDOW GO 0 %s\n", target);
}

static int leonos_try_keyboard_event(unsigned int scancode)
{
    char ch;

    if ((scancode & 0x80u) != 0u) {
        return 0;
    }

    if (leonos_url_editing) {
        if (scancode == 0x01u) {
            leonos_blur_url_edit();
            return 0;
        }
        if (scancode == 0x1Cu) {
            leonos_submit_url_edit();
            return 1;
        }
        if (scancode == 0x0Eu) {
            if (leonos_url_edit_len != 0u) {
                leonos_url_edit_len -= 1u;
                leonos_url_edit[leonos_url_edit_len] = 0;
                leonos_draw_url_edit();
            }
            return 0;
        }
        ch = leonos_scancode_ascii(scancode);
        if (ch != 0 && ch != ' ' &&
            leonos_url_edit_len + 1u < sizeof(leonos_url_edit)) {
            leonos_url_edit[leonos_url_edit_len++] = ch;
            leonos_url_edit[leonos_url_edit_len] = 0;
            leonos_draw_url_edit();
        }
        return 0;
    }

    if (scancode == 0x40u) {
        leonos_focus_url_edit();
        return 0;
    }
    if (scancode == 0x3Fu || scancode == 0x13u) {
        leonos_queue_stdin_line("WINDOW RELOAD 0\n");
        return 1;
    }
    if (scancode == 0x30u) {
        leonos_queue_stdin_line("WINDOW BACK 0\n");
        return 1;
    }
    if (scancode == 0x31u) {
        leonos_queue_stdin_line("WINDOW FORWARD 0\n");
        return 1;
    }
    if (scancode == 0x48u) {
        leonos_queue_stdin_line("WINDOW SCROLL 0 DX 0 DY -96\n");
        return 1;
    }
    if (scancode == 0x50u) {
        leonos_queue_stdin_line("WINDOW SCROLL 0 DX 0 DY 96\n");
        return 1;
    }
    if (scancode == 0x49u) {
        leonos_queue_stdin_line("WINDOW SCROLL 0 DX 0 DY -520\n");
        return 1;
    }
    if (scancode == 0x51u) {
        leonos_queue_stdin_line("WINDOW SCROLL 0 DX 0 DY 520\n");
        return 1;
    }
    if (scancode == 0x47u) {
        leonos_queue_stdin_line("WINDOW SCROLL 0 DX 0 DY -4096\n");
        return 1;
    }
    if (scancode == 0x4Fu) {
        leonos_queue_stdin_line("WINDOW SCROLL 0 DX 0 DY 4096\n");
        return 1;
    }

    ch = leonos_scancode_ascii(scancode);
    if (ch != 0) {
        leonos_queue_stdin_line("WINDOW KEY 0 VALUE %u\n", (unsigned int) ch);
        return 1;
    }
    if (scancode == 0x0Eu) {
        leonos_queue_stdin_line("WINDOW KEY 0 VALUE 8\n");
        return 1;
    }
    if (scancode == 0x1Cu) {
        leonos_queue_stdin_line("WINDOW KEY 0 VALUE 13\n");
        return 1;
    }
    return 0;
}

static int leonos_try_mouse_event(const struct leonos_event *event)
{
    unsigned int x = event->data0;
    unsigned int y = event->data1;

    if (event->type == LEONOS_EVENT_MOUSE_BUTTON) {
        unsigned int packed = event->data1;
        unsigned int buttons = (packed >> 16) & 0xffu;
        unsigned int previous = (packed >> 24) & 0xffu;
        y = packed & 0xffffu;

        if ((buttons & 1u) != 0u && (previous & 1u) == 0u) {
            if (y >= 20u && y < 56u && x >= 108u) {
                leonos_focus_url_edit();
                return 0;
            }
            if (x >= LEONOS_NETSURF_VIEW_X && y >= LEONOS_NETSURF_VIEW_Y) {
                leonos_queue_stdin_line(
                    "WINDOW CLICK 0 X %u Y %u BUTTON LEFT KIND SINGLE\n",
                    x - LEONOS_NETSURF_VIEW_X,
                    y - LEONOS_NETSURF_VIEW_Y);
                return 1;
            }
        }
        if ((buttons & 2u) != 0u && (previous & 2u) == 0u &&
            x >= LEONOS_NETSURF_VIEW_X && y >= LEONOS_NETSURF_VIEW_Y) {
            leonos_queue_stdin_line(
                "WINDOW CLICK 0 X %u Y %u BUTTON RIGHT KIND SINGLE\n",
                x - LEONOS_NETSURF_VIEW_X,
                y - LEONOS_NETSURF_VIEW_Y);
            return 1;
        }
        return 0;
    }

    if (x >= LEONOS_NETSURF_VIEW_X && y >= LEONOS_NETSURF_VIEW_Y) {
        leonos_queue_stdin_line("WINDOW TRACK 0 X %u Y %u\n",
                                x - LEONOS_NETSURF_VIEW_X,
                                y - LEONOS_NETSURF_VIEW_Y);
        return 1;
    }
    return 0;
}

static void leonos_prepare_interactive_stdin(void)
{
    struct leonos_event event;

    if (leonos_pending_stdin_ready) {
        return;
    }

    if (leonos_netsurf_fetch_generation_for_script !=
            leonos_last_redraw_generation &&
        leonos_netsurf_fetch_finished_for_script &&
        leonos_netsurf_parser_in_body_for_script &&
        leonos_netsurf_content_ready_for_script) {
        if (leonos_interactive_settle_polls < LEONOS_NETSURF_SETTLE_POLLS) {
            leonos_interactive_settle_polls += 1u;
            return;
        }
        leonos_last_redraw_generation =
            leonos_netsurf_fetch_generation_for_script;
        leonos_interactive_settle_polls = 0u;
        leonos_queue_stdin_line("WINDOW REDRAW 0\n");
        return;
    }
    if (leonos_netsurf_fetch_generation_for_script !=
            leonos_last_redraw_generation &&
        !leonos_netsurf_fetch_finished_for_script) {
        return;
    }

    for (unsigned int i = 0u; i < 8u; i += 1u) {
        if (!leonos_event_poll(&event)) {
            break;
        }
        if (event.type == LEONOS_EVENT_KEYBOARD &&
            leonos_try_keyboard_event(event.data0)) {
            return;
        }
        if ((event.type == LEONOS_EVENT_MOUSE ||
             event.type == LEONOS_EVENT_MOUSE_BUTTON) &&
            leonos_try_mouse_event(&event)) {
            return;
        }
    }
}
#endif

static int leonos_stdin_script_ready(void)
{
#if LEONOS_NETSURF_INTERACTIVE
    if (leonos_url_edit_len == 0u) {
        leonos_url_edit_sync_len();
    }
    if (leonos_pending_stdin_ready) {
        return 1;
    }
#endif
    if (leonos_stdin_script_index >=
        sizeof(leonos_stdin_script) / sizeof(leonos_stdin_script[0])) {
#if LEONOS_NETSURF_INTERACTIVE
        leonos_prepare_interactive_stdin();
        return leonos_pending_stdin_ready;
#else
        return 0;
#endif
    }
    if (leonos_stdin_script_index > 0u &&
        (!leonos_netsurf_fetch_finished_for_script ||
         !leonos_netsurf_parser_in_body_for_script ||
         !leonos_netsurf_content_ready_for_script)) {
        return 0;
    }
    if (leonos_stdin_script_index > 0u) {
        if (leonos_stdin_settle_polls < LEONOS_NETSURF_SETTLE_POLLS) {
            leonos_stdin_settle_polls += 1u;
            return 0;
        }
    }
    if (leonos_stdin_ready_delay != 0u) {
        leonos_stdin_ready_delay -= 1u;
        return 0;
    }
    return 1;
}
#endif

static void *leonos_alloc_raw(size_t size)
{
#ifdef LEONOS_USER_APP
    if (size > 0xffffffffu) {
        errno = ENOMEM;
        return NULL;
    }
    return leonos_malloc((leonos_u32) size);
#else
    if (size == 0u || probe_heap_used + size < probe_heap_used ||
        probe_heap_used + size > LEONOS_PROBE_HEAP_SIZE) {
        errno = ENOMEM;
        return NULL;
    }
    void *ptr = (void *) (probe_heap + probe_heap_used);
    probe_heap_used += size;
    return ptr;
#endif
}

void *malloc(size_t size)
{
    size_t aligned = (size + 7u) & ~((size_t) 7u);
    if (aligned == 0u || aligned + LEONOS_ALLOC_HEADER_SIZE < aligned) {
        errno = ENOMEM;
        return NULL;
    }
    unsigned char *raw = leonos_alloc_raw(aligned + LEONOS_ALLOC_HEADER_SIZE);
    if (raw == NULL) {
        return NULL;
    }
    *((size_t *) raw) = aligned;
    return raw + LEONOS_ALLOC_HEADER_SIZE;
}

void free(void *ptr)
{
#ifdef LEONOS_USER_APP
    if (ptr != NULL) {
        leonos_free((unsigned char *) ptr - LEONOS_ALLOC_HEADER_SIZE);
    }
#else
    (void) ptr;
#endif
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0u) {
        free(ptr);
        return NULL;
    }
    unsigned char *old_raw = (unsigned char *) ptr - LEONOS_ALLOC_HEADER_SIZE;
    size_t old_size = *((size_t *) old_raw);
    void *new_ptr = malloc(size);
    if (new_ptr != NULL) {
        size_t copy = old_size < size ? old_size : size;
        memcpy(new_ptr, ptr, copy);
        free(ptr);
    }
    return new_ptr;
}

char *getenv(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "HOME") == 0) {
        return "/";
    }
    if (strcmp(name, "LANG") == 0 || strcmp(name, "LC_ALL") == 0 ||
        strcmp(name, "LC_MESSAGES") == 0) {
        return "C";
    }
    return NULL;
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

int rand(void)
{
    rand_state = rand_state * 1103515245u + 12345u;
    return (int) ((rand_state >> 1) & 0x7fffffffu);
}

void srand(unsigned int seed)
{
    rand_state = seed == 0u ? 1u : seed;
}

static int leonos_digit_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return -1;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    unsigned long value = 0;
    int any = 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v') {
        p += 1;
    }

    int negative = 0;
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        p += 1;
    }

    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
        p += 1;
    } else if (base == 0) {
        base = 10;
    }

    while (*p != 0) {
        int digit = leonos_digit_value(*p);
        if (digit < 0 || digit >= base) {
            break;
        }
        value = value * (unsigned long) base + (unsigned long) digit;
        any = 1;
        p += 1;
    }

    if (endptr != NULL) {
        *endptr = (char *) (any ? p : nptr);
    }
    return negative ? (unsigned long) (0ul - value) : value;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    unsigned long long value = 0;
    int any = 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v') {
        p += 1;
    }

    int negative = 0;
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        p += 1;
    }

    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
        p += 1;
    } else if (base == 0) {
        base = 10;
    }

    while (*p != 0) {
        int digit = leonos_digit_value(*p);
        if (digit < 0 || digit >= base) {
            break;
        }
        value = value * (unsigned long long) base + (unsigned long long) digit;
        any = 1;
        p += 1;
    }

    if (endptr != NULL) {
        *endptr = (char *) (any ? p : nptr);
    }
    return negative ? (unsigned long long) (0ull - value) : value;
}

long strtol(const char *nptr, char **endptr, int base)
{
    return (long) strtoul(nptr, endptr, base);
}

long long strtoll(const char *nptr, char **endptr, int base)
{
    return (long long) strtoull(nptr, endptr, base);
}

int atoi(const char *nptr)
{
    return (int) strtol(nptr, NULL, 10);
}

double strtod(const char *nptr, char **endptr)
{
    const char *p = nptr;
    double value = 0.0;
    double scale = 0.1;
    int any = 0;
    int negative = 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v') {
        p += 1;
    }
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        p += 1;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (double) (*p - '0');
        any = 1;
        p += 1;
    }
    if (*p == '.') {
        p += 1;
        while (*p >= '0' && *p <= '9') {
            value += (double) (*p - '0') * scale;
            scale *= 0.1;
            any = 1;
            p += 1;
        }
    }
    if (endptr != NULL) {
        *endptr = (char *) (any ? p : nptr);
    }
    return negative ? -value : value;
}

float strtof(const char *nptr, char **endptr)
{
    return (float) strtod(nptr, endptr);
}

void abort(void)
{
#ifdef LEONOS_USER_APP
    leonos_write("NetSurf abort\r\n");
    leonos_exit();
#endif
    for (;;) {
    }
}

void exit(int status)
{
    (void) status;
#ifdef LEONOS_USER_APP
    leonos_exit();
#endif
    abort();
}

int atexit(void (*func)(void))
{
    (void) func;
    return 0;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    const unsigned char *bytes = (const unsigned char *) base;
    size_t low = 0u;
    size_t high = nmemb;
    while (low < high) {
        size_t mid = low + ((high - low) / 2u);
        const void *item = bytes + mid * size;
        int cmp = compar(key, item);
        if (cmp < 0) {
            high = mid;
        } else if (cmp > 0) {
            low = mid + 1u;
        } else {
            return (void *) item;
        }
    }
    return NULL;
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    unsigned char *bytes = (unsigned char *) base;
    if (base == NULL || size == 0u || nmemb < 2u) {
        return;
    }
    for (size_t i = 1u; i < nmemb; i += 1u) {
        size_t j = i;
        while (j > 0u &&
               compar(bytes + (j - 1u) * size, bytes + j * size) > 0) {
            for (size_t b = 0u; b < size; b += 1u) {
                unsigned char tmp = bytes[(j - 1u) * size + b];
                bytes[(j - 1u) * size + b] = bytes[j * size + b];
                bytes[j * size + b] = tmp;
            }
            j -= 1u;
        }
    }
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    unsigned char *d = (unsigned char *) dst;
    const unsigned char *s = (const unsigned char *) src;
    for (size_t i = 0u; i < n; i += 1u) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *) dst;
    const unsigned char *s = (const unsigned char *) src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s) {
        for (size_t i = 0u; i < n; i += 1u) {
            d[i] = s[i];
        }
    } else {
        while (n-- != 0u) {
            d[n] = s[n];
        }
    }
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *) s;
    unsigned char byte = (unsigned char) c;
    for (size_t i = 0u; i < n; i += 1u) {
        p[i] = byte;
    }
    return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *) a;
    const unsigned char *pb = (const unsigned char *) b;
    for (size_t i = 0u; i < n; i += 1u) {
        if (pa[i] != pb[i]) {
            return (int) pa[i] - (int) pb[i];
        }
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *) s;
    unsigned char needle = (unsigned char) c;
    for (size_t i = 0u; i < n; i += 1u) {
        if (p[i] == needle) {
            return (void *) (p + i);
        }
    }
    return NULL;
}

size_t strlen(const char *s)
{
    size_t len = 0u;
    while (s[len] != 0) {
        len += 1u;
    }
    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a == *b && *a != 0) {
        a += 1;
        b += 1;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n != 0u && *a == *b && *a != 0) {
        a += 1;
        b += 1;
        n -= 1u;
    }
    if (n == 0u) {
        return 0;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0u;
    for (; i < n && src[i] != 0; i += 1u) {
        dst[i] = src[i];
    }
    for (; i < n; i += 1u) {
        dst[i] = 0;
    }
    return dst;
}

char *strchr(const char *s, int c)
{
    char needle = (char) c;
    while (*s != 0) {
        if (*s == needle) {
            return (char *) s;
        }
        s += 1;
    }
    return needle == 0 ? (char *) s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    char needle = (char) c;
    do {
        if (*s == needle) {
            last = s;
        }
    } while (*s++ != 0);
    return (char *) last;
}

size_t strspn(const char *s, const char *accept)
{
    size_t count = 0u;
    while (*s != 0) {
        int found = 0;
        for (const char *a = accept; *a != 0; a += 1) {
            if (*s == *a) {
                found = 1;
                break;
            }
        }
        if (!found) {
            break;
        }
        count += 1u;
        s += 1;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t count = 0u;
    while (*s != 0) {
        for (const char *r = reject; *r != 0; r += 1) {
            if (*s == *r) {
                return count;
            }
        }
        count += 1u;
        s += 1;
    }
    return count;
}

char *strpbrk(const char *s, const char *accept)
{
    while (*s != 0) {
        for (const char *a = accept; *a != 0; a += 1) {
            if (*s == *a) {
                return (char *) s;
            }
        }
        s += 1;
    }
    return NULL;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0u) {
        return (char *) haystack;
    }
    while (*haystack != 0) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *) haystack;
        }
        haystack += 1;
    }
    return NULL;
}

char *strtok(char *str, const char *delim)
{
    static char *next;
    char *start;

    if (str != NULL) {
        next = str;
    }
    if (next == NULL) {
        return NULL;
    }

    while (*next != 0 && strchr(delim, *next) != NULL) {
        next += 1;
    }
    if (*next == 0) {
        next = NULL;
        return NULL;
    }

    start = next;
    while (*next != 0 && strchr(delim, *next) == NULL) {
        next += 1;
    }
    if (*next != 0) {
        *next = 0;
        next += 1;
    } else {
        next = NULL;
    }
    return start;
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1u;
    char *copy = malloc(len);
    if (copy == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

char *strndup(const char *s, size_t n)
{
    size_t len = 0u;
    char *copy;

    if (s == NULL) {
        errno = EINVAL;
        return NULL;
    }

    while (len < n && s[len] != 0) {
        len += 1u;
    }

    copy = malloc(len + 1u);
    if (copy == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(copy, s, len);
    copy[len] = 0;
    return copy;
}

char *strerror(int errnum)
{
    switch (errnum) {
    case ENOENT:
        return "not found";
    case ENOMEM:
        return "out of memory";
    case EINVAL:
        return "invalid argument";
    case ENOSYS:
        return "not implemented";
    default:
        return "error";
    }
}

int strcasecmp(const char *a, const char *b)
{
    while (tolower((unsigned char) *a) == tolower((unsigned char) *b) &&
           *a != 0) {
        a += 1;
        b += 1;
    }
    return tolower((unsigned char) *a) - tolower((unsigned char) *b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n != 0u &&
           tolower((unsigned char) *a) == tolower((unsigned char) *b) &&
           *a != 0) {
        a += 1;
        b += 1;
        n -= 1u;
    }
    if (n == 0u) {
        return 0;
    }
    return tolower((unsigned char) *a) - tolower((unsigned char) *b);
}

char *strcasestr(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (haystack == NULL || needle == NULL) {
        errno = EINVAL;
        return NULL;
    }

    needle_len = strlen(needle);
    if (needle_len == 0u) {
        return (char *) haystack;
    }

    while (*haystack != 0) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *) haystack;
        }
        haystack += 1;
    }
    return NULL;
}

int tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

int isascii(int c)
{
    return (c & ~0x7f) == 0;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    (void) stream;
#ifdef LEONOS_USER_APP
    char buf[384];
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (ret >= 0) {
        leonos_write_stdout(buf);
    }
    return ret;
#else
    (void) fmt;
    (void) ap;
    return 0;
#endif
}

void setbuf(FILE *stream, char *buf)
{
    (void) stream;
    (void) buf;
}

static int leonos_putc(char *str, size_t size, size_t *used, char c)
{
    if (size != 0u && *used + 1u < size) {
        str[*used] = c;
    }
    *used += 1u;
    return 1;
}

static int leonos_puts(char *str, size_t size, size_t *used, const char *text)
{
    int count = 0;
    if (text == NULL) {
        text = "(null)";
    }
    while (*text != 0) {
        count += leonos_putc(str, size, used, *text++);
    }
    return count;
}

static int leonos_put_nstr(char *str, size_t size, size_t *used,
                           const char *text, int precision)
{
    int count = 0;
    if (text == NULL) {
        text = "(null)";
    }
    while (*text != 0 && (precision < 0 || count < precision)) {
        count += leonos_putc(str, size, used, *text++);
    }
    return count;
}

static int leonos_put_uint(char *str, size_t size, size_t *used,
                           unsigned int value, unsigned int base, int upper)
{
    char tmp[32];
    size_t len = 0u;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (value == 0u) {
        leonos_putc(str, size, used, '0');
        return 1;
    }
    while (value != 0u && len < sizeof(tmp)) {
        tmp[len++] = digits[value % base];
        value /= base;
    }
    for (size_t i = 0u; i < len; i += 1u) {
        leonos_putc(str, size, used, tmp[len - i - 1u]);
    }
    return (int) len;
}

static int leonos_put_int(char *str, size_t size, size_t *used, int value)
{
    if (value < 0) {
        int count = leonos_putc(str, size, used, '-');
        return count + leonos_put_uint(str, size, used,
                                       (unsigned int) (0u - (unsigned int) value),
                                       10u, 0);
    }
    return leonos_put_uint(str, size, used, (unsigned int) value, 10u, 0);
}

static int leonos_put_double(char *str, size_t size, size_t *used, double value)
{
    int count = 0;
    if (value < 0.0) {
        count += leonos_putc(str, size, used, '-');
        value = -value;
    }
    unsigned int whole = (unsigned int) value;
    double frac = value - (double) whole;
    count += leonos_put_uint(str, size, used, whole, 10u, 0);
    count += leonos_putc(str, size, used, '.');
    for (unsigned int i = 0u; i < 3u; i += 1u) {
        frac *= 10.0;
        unsigned int digit = (unsigned int) frac;
        count += leonos_putc(str, size, used, (char) ('0' + digit));
        frac -= (double) digit;
    }
    return count;
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, (size_t) -1, fmt, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    size_t used = 0u;
    if (str == NULL || fmt == NULL) {
        errno = EINVAL;
        return -1;
    }

    while (*fmt != 0) {
        if (*fmt != '%') {
            leonos_putc(str, size, &used, *fmt++);
            continue;
        }

        fmt += 1;
        if (*fmt == 0) {
            break;
        }
        int precision = -1;
        while (*fmt >= '0' && *fmt <= '9') {
            fmt += 1;
        }
        if (*fmt == '.') {
            fmt += 1;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                fmt += 1;
            } else {
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt += 1;
                }
            }
        }
        while (*fmt == 'l' || *fmt == 'z') {
            fmt += 1;
        }
        if (*fmt == '%') {
            leonos_putc(str, size, &used, '%');
        } else if (*fmt == 's') {
            leonos_put_nstr(str, size, &used, va_arg(ap, const char *),
                            precision);
        } else if (*fmt == 'c') {
            leonos_putc(str, size, &used, (char) va_arg(ap, int));
        } else if (*fmt == 'd' || *fmt == 'i') {
            leonos_put_int(str, size, &used, va_arg(ap, int));
        } else if (*fmt == 'u') {
            leonos_put_uint(str, size, &used, va_arg(ap, unsigned int), 10u, 0);
        } else if (*fmt == 'x') {
            leonos_put_uint(str, size, &used, va_arg(ap, unsigned int), 16u, 0);
        } else if (*fmt == 'X') {
            leonos_put_uint(str, size, &used, va_arg(ap, unsigned int), 16u, 1);
        } else if (*fmt == 'p') {
            leonos_puts(str, size, &used, "0x");
            leonos_put_uint(str, size, &used,
                            (unsigned int) va_arg(ap, void *), 16u, 0);
        } else if (*fmt == 'f') {
            leonos_put_double(str, size, &used, va_arg(ap, double));
        } else {
            leonos_putc(str, size, &used, '%');
            leonos_putc(str, size, &used, *fmt);
        }
        fmt += 1;
    }

    if (size != 0u) {
        size_t term = used < size ? used : size - 1u;
        str[term] = 0;
    }
    return (int) used;
}

int fputc(int c, FILE *stream)
{
    (void) stream;
#ifdef LEONOS_USER_APP
    char buf[2];
    buf[0] = (char) c;
    buf[1] = 0;
    leonos_write_stdout(buf);
#endif
    return c;
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *stream)
{
    (void) stream;
    if (s == NULL) {
        return EOF;
    }
#ifdef LEONOS_USER_APP
    leonos_write_stdout(s);
#endif
    return 0;
}

int fflush(FILE *stream)
{
    (void) stream;
    return 0;
}

char *fgets(char *str, int size, FILE *stream)
{
    (void) stream;
#ifdef LEONOS_USER_APP
    const char *line;
    if (str == NULL || size <= 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!leonos_stdin_script_ready()) {
        errno = EAGAIN;
        return NULL;
    }
#if LEONOS_NETSURF_INTERACTIVE
    if (leonos_pending_stdin_ready) {
        line = leonos_pending_stdin_line;
    } else {
        line = leonos_stdin_script[leonos_stdin_script_index];
    }
#else
    line = leonos_stdin_script[leonos_stdin_script_index];
#endif
    int i = 0;
    while (i + 1 < size && line[i] != 0) {
        str[i] = line[i];
        i += 1;
        if (str[i - 1] == '\n') {
            break;
        }
    }
    str[i] = 0;
#if LEONOS_NETSURF_INTERACTIVE
    if (leonos_pending_stdin_ready) {
        leonos_pending_stdin_ready = 0;
        leonos_pending_stdin_line[0] = 0;
        return str;
    }
#endif
    leonos_stdin_ready_delay =
        leonos_stdin_delay_after[leonos_stdin_script_index];
    leonos_stdin_settle_polls = 0u;
    leonos_stdin_script_index += 1u;
    return str;
#else
    (void) str;
    (void) size;
    errno = ENOSYS;
    return NULL;
#endif
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    (void) ptr;
    (void) size;
    (void) nmemb;
    (void) stream;
    return 0u;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    (void) stream;
#ifdef LEONOS_USER_APP
    const char *bytes = (const char *) ptr;
    size_t total = size * nmemb;
    char chunk[129];
    size_t off = 0u;
    while (off < total) {
        size_t n = total - off;
        if (n > sizeof(chunk) - 1u) {
            n = sizeof(chunk) - 1u;
        }
        memcpy(chunk, bytes + off, n);
        chunk[n] = 0;
        leonos_write_stdout(chunk);
        off += n;
    }
#else
    (void) ptr;
    (void) size;
#endif
    return nmemb;
}

int feof(FILE *stream)
{
    (void) stream;
    return 1;
}

int fseek(FILE *stream, long offset, int whence)
{
    (void) stream;
    (void) offset;
    (void) whence;
    errno = ENOSYS;
    return -1;
}

long ftell(FILE *stream)
{
    (void) stream;
    errno = ENOSYS;
    return -1L;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    int assigned = 0;

    if (str == NULL || fmt == NULL) {
        errno = EINVAL;
        return -1;
    }

    va_start(ap, fmt);
    while (*fmt != 0) {
        if (*fmt != '%') {
            if (*str != *fmt) {
                break;
            }
            str += 1;
            fmt += 1;
            continue;
        }

        fmt += 1;
        if (*fmt == '%') {
            if (*str != '%') {
                break;
            }
            str += 1;
            fmt += 1;
            continue;
        }

        while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
            str += 1;
        }

        if (*fmt == 'u') {
            char *endptr;
            unsigned int *out = va_arg(ap, unsigned int *);
            unsigned long value = strtoul(str, &endptr, 10);
            if (endptr == str) {
                break;
            }
            *out = (unsigned int) value;
            str = endptr;
            assigned += 1;
            fmt += 1;
        } else if (*fmt == 'd' || *fmt == 'i') {
            char *endptr;
            int *out = va_arg(ap, int *);
            long value = strtol(str, &endptr, 10);
            if (endptr == str) {
                break;
            }
            *out = (int) value;
            str = endptr;
            assigned += 1;
            fmt += 1;
        } else if (*fmt == 'z') {
            char *endptr;
            size_t *out = va_arg(ap, size_t *);
            fmt += 1;
            if (*fmt != 'u') {
                break;
            }
            unsigned long value = strtoul(str, &endptr, 10);
            if (endptr == str) {
                break;
            }
            *out = (size_t) value;
            str = endptr;
            assigned += 1;
            fmt += 1;
        } else {
            break;
        }
    }
    va_end(ap);

    return assigned;
}

FILE *fopen(const char *path, const char *mode)
{
    (void) path;
    (void) mode;
    return NULL;
}

int fclose(FILE *stream)
{
    (void) stream;
    return 0;
}

int remove(const char *path)
{
    (void) path;
    errno = ENOSYS;
    return -1;
}

int rename(const char *oldpath, const char *newpath)
{
    (void) oldpath;
    (void) newpath;
    errno = ENOSYS;
    return -1;
}

DIR *opendir(const char *name)
{
    (void) name;
    errno = ENOSYS;
    return NULL;
}

struct dirent *readdir(DIR *dirp)
{
    (void) dirp;
    errno = ENOSYS;
    return NULL;
}

int closedir(DIR *dirp)
{
    (void) dirp;
    return 0;
}

int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **))
{
    (void) dirp;
    (void) namelist;
    (void) filter;
    (void) compar;
    errno = ENOSYS;
    return -1;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

double fabs(double x)
{
    return x < 0.0 ? -x : x;
}

float fabsf(float x)
{
    return x < 0.0f ? -x : x;
}

double fmin(double x, double y)
{
    if (isnan(x)) {
        return y;
    }
    if (isnan(y)) {
        return x;
    }
    return x < y ? x : y;
}

double fmax(double x, double y)
{
    if (isnan(x)) {
        return y;
    }
    if (isnan(y)) {
        return x;
    }
    return x > y ? x : y;
}

int isnan(double x)
{
    return x != x;
}

int isfinite(double x)
{
    return !isnan(x) && x != INFINITY && x != -INFINITY;
}

int isinf(double x)
{
    return x == INFINITY || x == -INFINITY;
}

int signbit(double x)
{
    union {
        double d;
        unsigned long long u;
    } bits;
    bits.d = x;
    return (int) (bits.u >> 63);
}

double floor(double x)
{
    long i = (long) x;
    if ((double) i > x) {
        i -= 1;
    }
    return (double) i;
}

double ceil(double x)
{
    long i = (long) x;
    if ((double) i < x) {
        i += 1;
    }
    return (double) i;
}

double round(double x)
{
    return x < 0.0 ? ceil(x - 0.5) : floor(x + 0.5);
}

double trunc(double x)
{
    return x < 0.0 ? ceil(x) : floor(x);
}

long lrint(double x)
{
    return (long) round(x);
}

float ceilf(float x)
{
    return (float) ceil((double) x);
}

double fmod(double x, double y)
{
    if (y == 0.0) {
        return NAN;
    }
    long q = (long) (x / y);
    return x - (double) q * y;
}

static double leonos_reduce_angle(double x)
{
    const double tau = 6.28318530717958647692;
    x = fmod(x, tau);
    if (x > M_PI) {
        x -= tau;
    } else if (x < -M_PI) {
        x += tau;
    }
    return x;
}

double sqrt(double x)
{
    if (x < 0.0) {
        return NAN;
    }
    if (x == 0.0) {
        return 0.0;
    }
    double guess = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 16; i += 1) {
        guess = 0.5 * (guess + x / guess);
    }
    return guess;
}

double hypot(double x, double y)
{
    x = fabs(x);
    y = fabs(y);
    return sqrt(x * x + y * y);
}

double cbrt(double x)
{
    if (x == 0.0) {
        return 0.0;
    }
    int negative = x < 0.0;
    double value = negative ? -x : x;
    double guess = value > 1.0 ? value : 1.0;
    for (int i = 0; i < 24; i += 1) {
        guess = (2.0 * guess + value / (guess * guess)) / 3.0;
    }
    return negative ? -guess : guess;
}

double exp(double x)
{
    const double ln2 = 0.69314718055994530942;
    if (x != x) {
        return NAN;
    }
    if (x > 709.0) {
        return INFINITY;
    }
    if (x < -745.0) {
        return 0.0;
    }

    int power = (int) (x / ln2);
    double r = x - (double) power * ln2;
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i <= 24; i += 1) {
        term *= r / (double) i;
        sum += term;
    }

    if (power >= 0) {
        while (power-- > 0) {
            sum *= 2.0;
        }
    } else {
        while (power++ < 0) {
            sum *= 0.5;
        }
    }
    return sum;
}

double expm1(double x)
{
    return exp(x) - 1.0;
}

double log(double x)
{
    const double ln2 = 0.69314718055994530942;
    if (x < 0.0) {
        return NAN;
    }
    if (x == 0.0) {
        return -INFINITY;
    }
    if (x == INFINITY) {
        return INFINITY;
    }

    int power = 0;
    while (x > 1.41421356237309504880) {
        x *= 0.5;
        power += 1;
    }
    while (x < 0.70710678118654752440) {
        x *= 2.0;
        power -= 1;
    }

    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double term = y;
    double sum = 0.0;
    for (int n = 1; n < 40; n += 2) {
        sum += term / (double) n;
        term *= y2;
    }
    return 2.0 * sum + (double) power * ln2;
}

double log1p(double x)
{
    return log(1.0 + x);
}

double log2(double x)
{
    return log(x) / 0.69314718055994530942;
}

double log10(double x)
{
    return log(x) / 2.30258509299404568402;
}

double pow(double x, double y)
{
    long exponent = (long) y;
    if ((double) exponent == y) {
        double result = 1.0;
        long count = exponent < 0 ? -exponent : exponent;
        for (long i = 0; i < count; i += 1) {
            result *= x;
        }
        return exponent < 0 ? 1.0 / result : result;
    }
    if (x <= 0.0) {
        return NAN;
    }
    return exp(y * log(x));
}

float powf(float x, float y)
{
    return (float) pow((double) x, (double) y);
}

double sin(double x)
{
    x = leonos_reduce_angle(x);
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 -
                (x2 * x2 * x2) / 5040.0);
}

double cos(double x)
{
    x = leonos_reduce_angle(x);
    double x2 = x * x;
    return 1.0 - x2 / 2.0 + (x2 * x2) / 24.0 -
           (x2 * x2 * x2) / 720.0;
}

double acos(double x)
{
    if (x <= -1.0) {
        return M_PI;
    }
    if (x >= 1.0) {
        return 0.0;
    }
    int negate = x < 0.0;
    x = fabs(x);
    double ret = -0.0187293;
    ret = ret * x + 0.0742610;
    ret = ret * x - 0.2121144;
    ret = ret * x + 1.5707288;
    ret = ret * sqrt(1.0 - x);
    return negate ? M_PI - ret : ret;
}

double asin(double x)
{
    return M_PI_2 - acos(x);
}

double atan(double x)
{
    if (x != x) {
        return NAN;
    }
    if (x > 1.0) {
        return M_PI_2 - atan(1.0 / x);
    }
    if (x < -1.0) {
        return -M_PI_2 - atan(1.0 / x);
    }

    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int n = 3, sign = -1; n <= 27; n += 2, sign = -sign) {
        term *= x2;
        sum += (double) sign * term / (double) n;
    }
    return sum;
}

double atan2(double y, double x)
{
    if (x > 0.0) {
        return atan(y / x);
    }
    if (x < 0.0 && y >= 0.0) {
        return atan(y / x) + M_PI;
    }
    if (x < 0.0 && y < 0.0) {
        return atan(y / x) - M_PI;
    }
    if (x == 0.0 && y > 0.0) {
        return M_PI_2;
    }
    if (x == 0.0 && y < 0.0) {
        return -M_PI_2;
    }
    return 0.0;
}

double tan(double x)
{
    double c = cos(x);
    if (c == 0.0) {
        return sin(x) < 0.0 ? -INFINITY : INFINITY;
    }
    return sin(x) / c;
}

double sinh(double x)
{
    return (exp(x) - exp(-x)) * 0.5;
}

double cosh(double x)
{
    return (exp(x) + exp(-x)) * 0.5;
}

double tanh(double x)
{
    double ex = exp(x);
    double enx = exp(-x);
    return (ex - enx) / (ex + enx);
}

double asinh(double x)
{
    return log(x + sqrt(x * x + 1.0));
}

double acosh(double x)
{
    if (x < 1.0) {
        return NAN;
    }
    return log(x + sqrt(x * x - 1.0));
}

double atanh(double x)
{
    if (x <= -1.0 || x >= 1.0) {
        return NAN;
    }
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

long lroundf(float x)
{
    return (long) (x >= 0.0f ? x + 0.5f : x - 0.5f);
}

time_t time(time_t *timer)
{
#ifdef LEONOS_USER_APP
    time_t current = (time_t) (1767225600u + (leonos_millis() / 1000u));
#else
    static time_t current;
    current += 1;
#endif
    if (timer != NULL) {
        *timer = current;
    }
    return current;
}

double difftime(time_t time1, time_t time0)
{
    return (double) (time1 - time0);
}

time_t mktime(struct tm *tm)
{
    if (tm == NULL) {
        errno = EINVAL;
        return (time_t) -1;
    }
    return (time_t) ((((tm->tm_year + 1900) - 1970) * 365 +
                      tm->tm_yday) * 24 * 60 * 60);
}

struct tm *localtime(const time_t *timer)
{
    (void) timer;
    static struct tm tm_value;
    tm_value.tm_mday = 1;
    tm_value.tm_mon = 0;
    tm_value.tm_year = 126;
    tm_value.tm_gmtoff = 0;
    return &tm_value;
}

struct tm *localtime_r(const time_t *timer, struct tm *result)
{
    struct tm *value = localtime(timer);
    if (result == NULL || value == NULL) {
        return NULL;
    }
    *result = *value;
    return result;
}

struct tm *gmtime(const time_t *timer)
{
    return localtime(timer);
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    (void) format;
    (void) tm;
    const char *text = "Thu 01 Jan 2026";
    size_t len = strlen(text);
    if (max == 0u || len >= max) {
        return 0u;
    }
    memcpy(s, text, len + 1u);
    return len;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void) tz;
    if (tv == NULL) {
        errno = EINVAL;
        return -1;
    }
#ifdef LEONOS_USER_APP
    leonos_u32 ms = leonos_millis();
    tv->tv_sec = (time_t) (1767225600u + (ms / 1000u));
    tv->tv_usec = (long) ((ms % 1000u) * 1000u);
#else
    tv->tv_sec = time(NULL);
    tv->tv_usec = 0;
#endif
    return 0;
}

ssize_t read(int fd, void *buf, size_t count)
{
    (void) fd;
    (void) buf;
    (void) count;
    errno = ENOSYS;
    return -1;
}

ssize_t write(int fd, const void *buf, size_t count)
{
#ifdef LEONOS_USER_APP
    if (fd == 1 || fd == 2) {
        const char *bytes = (const char *) buf;
        char chunk[129];
        size_t off = 0u;
        while (off < count) {
            size_t n = count - off;
            if (n > sizeof(chunk) - 1u) {
                n = sizeof(chunk) - 1u;
            }
            memcpy(chunk, bytes + off, n);
            chunk[n] = 0;
            leonos_write_stdout(chunk);
            off += n;
        }
        return (ssize_t) count;
    }
#else
    (void) fd;
    (void) buf;
#endif
    return (ssize_t) count;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    (void) offset;
    return read(fd, buf, count);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    (void) offset;
    return write(fd, buf, count);
}

off_t lseek(int fd, off_t offset, int whence)
{
    (void) fd;
    (void) offset;
    (void) whence;
    errno = ESPIPE;
    return (off_t) -1;
}

int close(int fd)
{
    (void) fd;
    return 0;
}

int access(const char *path, int mode)
{
    (void) path;
    (void) mode;
    errno = ENOENT;
    return -1;
}

int unlink(const char *path)
{
    (void) path;
    errno = ENOSYS;
    return -1;
}

int rmdir(const char *path)
{
    (void) path;
    errno = ENOSYS;
    return -1;
}

char *getcwd(char *buf, size_t size)
{
    const char *cwd = "/";
    if (buf == NULL || size < 2u) {
        errno = EINVAL;
        return NULL;
    }
    strncpy(buf, cwd, size);
    buf[size - 1u] = 0;
    return buf;
}

char *realpath(const char *path, char *resolved_path)
{
    if (path == NULL || resolved_path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    strncpy(resolved_path, path, PATH_MAX);
    resolved_path[PATH_MAX - 1] = 0;
    return resolved_path;
}

int chdir(const char *path)
{
    (void) path;
    return 0;
}

int stat(const char *path, struct stat *st)
{
    (void) path;
    (void) st;
    errno = ENOENT;
    return -1;
}

int fstat(int fd, struct stat *st)
{
    (void) fd;
    (void) st;
    errno = EBADF;
    return -1;
}

int mkdir(const char *path, mode_t mode)
{
    (void) path;
    (void) mode;
    errno = ENOSYS;
    return -1;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    (void) nfds;
    (void) timeout;
#ifdef LEONOS_USER_APP
    leonos_yield();
    if (writefds != NULL) {
        FD_ZERO(writefds);
    }
    if (exceptfds != NULL) {
        FD_ZERO(exceptfds);
    }
    if (readfds != NULL) {
        FD_ZERO(readfds);
        if (leonos_stdin_script_ready()) {
            FD_SET(0, readfds);
            return 1;
        }
    }
    return 0;
#else
    if (readfds != NULL) {
        FD_ZERO(readfds);
    }
    if (writefds != NULL) {
        FD_ZERO(writefds);
    }
    if (exceptfds != NULL) {
        FD_ZERO(exceptfds);
    }
    return 0;
#endif
}

int socket(int domain, int type, int protocol)
{
    (void) domain;
    (void) type;
    (void) protocol;
    errno = ENOSYS;
    return -1;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void) sockfd;
    (void) addr;
    (void) addrlen;
    errno = ENOSYS;
    return -1;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    (void) sockfd;
    (void) buf;
    (void) len;
    (void) flags;
    errno = ENOSYS;
    return -1;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    (void) sockfd;
    (void) buf;
    (void) len;
    (void) flags;
    errno = ENOSYS;
    return -1;
}

int shutdown(int sockfd, int how)
{
    (void) sockfd;
    (void) how;
    errno = ENOSYS;
    return -1;
}

static uint16_t leonos_bswap16(uint16_t value)
{
    return (uint16_t) ((value >> 8) | (value << 8));
}

static uint32_t leonos_bswap32(uint32_t value)
{
    return ((value & 0x000000ffu) << 24) |
           ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) |
           ((value & 0xff000000u) >> 24);
}

uint16_t htons(uint16_t hostshort)
{
    return leonos_bswap16(hostshort);
}

uint16_t ntohs(uint16_t netshort)
{
    return leonos_bswap16(netshort);
}

uint32_t htonl(uint32_t hostlong)
{
    return leonos_bswap32(hostlong);
}

uint32_t ntohl(uint32_t netlong)
{
    return leonos_bswap32(netlong);
}

int inet_aton(const char *cp, struct in_addr *inp)
{
    unsigned int parts[4] = {0u, 0u, 0u, 0u};
    const char *p = cp;

    if (cp == NULL || inp == NULL) {
        errno = EINVAL;
        return 0;
    }
    for (int i = 0; i < 4; i += 1) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        while (*p >= '0' && *p <= '9') {
            parts[i] = parts[i] * 10u + (unsigned int) (*p - '0');
            if (parts[i] > 255u) {
                return 0;
            }
            p += 1;
        }
        if (i < 3) {
            if (*p != '.') {
                return 0;
            }
            p += 1;
        }
    }
    if (*p != 0) {
        return 0;
    }
    inp->s_addr = htonl((parts[0] << 24) | (parts[1] << 16) |
                        (parts[2] << 8) | parts[3]);
    return 1;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af == AF_INET) {
        return inet_aton(src, (struct in_addr *) dst);
    }
    errno = EAFNOSUPPORT;
    return -1;
}

static uint64_t leonos_udivmod64(uint64_t numerator, uint64_t denominator,
                                 uint64_t *remainder_out)
{
    uint64_t quotient = 0u;
    uint64_t remainder = 0u;

    if (denominator == 0u) {
        if (remainder_out != NULL) {
            *remainder_out = numerator;
        }
        return 0u;
    }

    for (int bit = 63; bit >= 0; bit -= 1) {
        remainder = (remainder << 1) | ((numerator >> bit) & 1u);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= (1ull << bit);
        }
    }
    if (remainder_out != NULL) {
        *remainder_out = remainder;
    }
    return quotient;
}

uint64_t __udivdi3(uint64_t numerator, uint64_t denominator)
{
    return leonos_udivmod64(numerator, denominator, NULL);
}

uint64_t __umoddi3(uint64_t numerator, uint64_t denominator)
{
    uint64_t remainder;
    (void) leonos_udivmod64(numerator, denominator, &remainder);
    return remainder;
}

int64_t __divdi3(int64_t numerator, int64_t denominator)
{
    int negative = ((numerator < 0) != (denominator < 0));
    uint64_t un = numerator < 0 ? (uint64_t) (0ull - (uint64_t) numerator) :
        (uint64_t) numerator;
    uint64_t ud = denominator < 0 ? (uint64_t) (0ull - (uint64_t) denominator) :
        (uint64_t) denominator;
    uint64_t quotient = leonos_udivmod64(un, ud, NULL);
    return negative ? (int64_t) (0ull - quotient) : (int64_t) quotient;
}

int64_t __moddi3(int64_t numerator, int64_t denominator)
{
    uint64_t remainder;
    uint64_t un = numerator < 0 ? (uint64_t) (0ull - (uint64_t) numerator) :
        (uint64_t) numerator;
    uint64_t ud = denominator < 0 ? (uint64_t) (0ull - (uint64_t) denominator) :
        (uint64_t) denominator;
    (void) leonos_udivmod64(un, ud, &remainder);
    return numerator < 0 ? (int64_t) (0ull - remainder) : (int64_t) remainder;
}

int inflateInit2(z_stream *strm, int windowBits)
{
    (void) strm;
    (void) windowBits;
    return Z_DATA_ERROR;
}

int inflate(z_stream *strm, int flush)
{
    (void) strm;
    (void) flush;
    return Z_DATA_ERROR;
}

int inflateEnd(z_stream *strm)
{
    (void) strm;
    return Z_OK;
}

uLong crc32(uLong crc, const Bytef *buf, uInt len)
{
    uLong value = crc ^ 0xffffffffUL;
    for (uInt i = 0u; i < len; i += 1u) {
        value ^= buf[i];
        for (unsigned int bit = 0u; bit < 8u; bit += 1u) {
            value = (value >> 1) ^ (0xedb88320UL & (0UL - (value & 1UL)));
        }
    }
    return value ^ 0xffffffffUL;
}

gzFile gzopen(const char *path, const char *mode)
{
    (void) path;
    (void) mode;
    errno = ENOSYS;
    return NULL;
}

char *gzgets(gzFile file, char *buf, int len)
{
    (void) file;
    (void) buf;
    (void) len;
    errno = ENOSYS;
    return NULL;
}

int gzclose(gzFile file)
{
    (void) file;
    return 0;
}

int uname(struct utsname *buf)
{
    if (buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    strncpy(buf->sysname, "LeonOS", sizeof(buf->sysname));
    strncpy(buf->nodename, "leonos", sizeof(buf->nodename));
    strncpy(buf->release, "0.1", sizeof(buf->release));
    strncpy(buf->version, "netsurf-probe", sizeof(buf->version));
    strncpy(buf->machine, "i386", sizeof(buf->machine));
    return 0;
}

static int leonos_iconv_encoding_ok(const char *name)
{
    return name != NULL &&
           (strcasecmp(name, "UTF-8") == 0 ||
            strcasecmp(name, "UTF8") == 0 ||
            strcasecmp(name, "US-ASCII") == 0 ||
            strcasecmp(name, "ASCII") == 0);
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    if (!leonos_iconv_encoding_ok(tocode) ||
        !leonos_iconv_encoding_ok(fromcode)) {
        errno = EINVAL;
        return (iconv_t) -1;
    }
    return (iconv_t) 1;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft)
{
    if (cd == (iconv_t) -1 || inbuf == NULL || inbytesleft == NULL ||
        outbuf == NULL || outbytesleft == NULL) {
        errno = EINVAL;
        return (size_t) -1;
    }
    while (*inbytesleft != 0u) {
        if (*outbytesleft == 0u) {
            errno = E2BIG;
            return (size_t) -1;
        }
        **outbuf = **inbuf;
        *inbuf += 1;
        *outbuf += 1;
        *inbytesleft -= 1u;
        *outbytesleft -= 1u;
    }
    return 0u;
}

int iconv_close(iconv_t cd)
{
    (void) cd;
    return 0;
}

char *setlocale(int category, const char *locale)
{
    (void) category;
    (void) locale;
    return "C";
}

sighandler_t signal(int signum, sighandler_t handler)
{
    (void) signum;
    return handler;
}

int regcomp(regex_t *preg, const char *regex, int cflags)
{
    (void) preg;
    (void) regex;
    (void) cflags;
    return 0;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags)
{
    (void) preg;
    (void) string;
    (void) nmatch;
    (void) pmatch;
    (void) eflags;
    return REG_NOMATCH;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf,
                size_t errbuf_size)
{
    (void) errcode;
    (void) preg;
    const char *message = "regex unavailable";
    size_t len = strlen(message);
    if (errbuf != NULL && errbuf_size != 0u) {
        size_t copy = len < errbuf_size - 1u ? len : errbuf_size - 1u;
        memcpy(errbuf, message, copy);
        errbuf[copy] = 0;
    }
    return len + 1u;
}

void regfree(regex_t *preg)
{
    (void) preg;
}
