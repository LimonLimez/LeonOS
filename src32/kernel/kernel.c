#include "bootinfo.h"

typedef int i32;
typedef signed char i8;
typedef long long i64;
typedef unsigned long long u64;

#define IDT_GATE_COUNT 256u
#define ROOT_ENTRY_SIZE 32u
#define MAX_FILES 18u
#define FILE_BUFFER_SIZE 8192u
#define CURSOR_W 16u
#define CURSOR_H 24u
#define GUI_REF_W 1920u
#define GUI_REF_H 1080u
#define PAGE_SIZE 4096u
#define PMM_MAX_MEMORY (256u * 1024u * 1024u)
#define PMM_MAX_PAGES (PMM_MAX_MEMORY / PAGE_SIZE)
#define LOW_IDENTITY_TABLES 32u
#define FB_IDENTITY_TABLES 4u
#define EVENT_QUEUE_CAPACITY 64u
#define MAX_KERNEL_TASKS 8u
#define FAT_FIXED_DATE (((2026u - 1980u) << 9) | (5u << 5) | 29u)
#define FAT32_WRITE_MAX_BYTES 512u
#define NET_MTU 1500u
#define NET_FRAME_MAX 1518u
#define NET_RX_QUEUE_MAX 64u
#define NET_ENABLE_ARP_PROBE 1u
#define NET_DNS_A_MAX 8u
#define RTL8139_RX_BUF_SIZE 65536u
#define RTL8139_RX_ALLOC_SIZE 69632u
#define RTL8139_TX_BUF_SIZE 2048u
#define LATE_RODATA __attribute__((section(".rodata.late")))

enum KernelEventType {
    EVENT_NONE = 0,
    EVENT_TIMER = 1,
    EVENT_KEYBOARD = 2,
    EVENT_MOUSE = 3,
    EVENT_MOUSE_BUTTON = 4
};

static volatile u16 *const vga_text = (volatile u16 *) 0xB8000u;

struct IdtEntry {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 type_attr;
    u16 offset_high;
} __attribute__((packed));

struct IdtPointer {
    u16 limit;
    u32 base;
} __attribute__((packed));

struct InterruptFrame {
    u32 gs;
    u32 fs;
    u32 es;
    u32 ds;
    u32 edi;
    u32 esi;
    u32 ebp;
    u32 esp_saved;
    u32 ebx;
    u32 edx;
    u32 ecx;
    u32 eax;
    u32 vector;
    u32 error_code;
    u32 eip;
    u32 cs;
    u32 eflags;
} __attribute__((packed));

struct GdtEntry {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 granularity;
    u8 base_high;
} __attribute__((packed));

struct GdtPointer {
    u16 limit;
    u32 base;
} __attribute__((packed));

struct Tss {
    u32 prev_tss;
    u32 esp0;
    u32 ss0;
    u32 esp1;
    u32 ss1;
    u32 esp2;
    u32 ss2;
    u32 cr3;
    u32 eip;
    u32 eflags;
    u32 eax;
    u32 ecx;
    u32 edx;
    u32 ebx;
    u32 esp;
    u32 ebp;
    u32 esi;
    u32 edi;
    u32 es;
    u32 cs;
    u32 ss;
    u32 ds;
    u32 fs;
    u32 gs;
    u32 ldt;
    u16 trap;
    u16 iomap_base;
} __attribute__((packed));

struct FileEntry {
    const u8 *dirent;
    char name[13];
    u32 first_cluster;
    u32 size;
};

struct KernelEvent {
    u32 type;
    u32 data0;
    u32 data1;
};

struct KernelTask {
    const char *name;
    void (*entry)(struct KernelTask *task);
    u32 run_count;
    u32 data0;
    u32 data1;
    u8 active;
};

extern void idt_load(const struct IdtPointer *pointer);
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);
extern void isr128(void);
extern void gdt_flush(const struct GdtPointer *pointer);
extern void tss_flush(void);
extern void enter_user_mode(u32 entry, u32 user_esp, u32 user_ss, u32 user_cs);
extern void resume_to_kernel(void);

static void scheduler_run_once(void);
extern u8 __kernel_start;
extern u8 __kernel_end;

#define KERNEL_CODE_SEL 0x08u
#define KERNEL_DATA_SEL 0x10u
#define USER_CODE_SEL 0x1Bu
#define USER_DATA_SEL 0x23u
#define TSS_SEL 0x28u
#define SYS_EXIT 0u
#define SYS_WRITE 1u
#define SYS_FB_INFO 2u
#define SYS_FB_FILL 3u
#define SYS_FB_PRESENT 4u
#define SYS_EVENT_POLL 5u
#define SYS_BROWSER_OPEN 6u
#define SYS_YIELD 7u
#define SYS_MILLIS 8u
#define SYS_MALLOC 9u
#define SYS_FREE 10u
#define SYS_NET_FETCH 11u
#define SYS_NET_FETCH_META 12u
#define SYS_NET_STREAM_OPEN 13u
#define SYS_NET_STREAM_POLL 14u
#define SYS_NET_STREAM_READ 15u
#define SYS_NET_STREAM_META 16u
#define SYS_NET_STREAM_CLOSE 17u
#define SYS_FB_TEXT 18u
#define SYS_FB_BLIT 19u
#define SYS_NET_FETCH_EX 20u
#define USER_BROWSER_URL_MAX 4096u
#define USER_NET_FETCH_MAX (8u * 1024u * 1024u)
#define USER_NET_FETCH_META_SIZE 588u
#define USER_NET_FETCH_REQUEST_SIZE 32u
#define USER_NET_FETCH_CONTENT_TYPE_MAX 64u
#define USER_NET_FETCH_LOCATION_MAX 512u
#define USER_NET_FETCH_FLAG_HEADERS 0x00000001u
#define USER_NET_FETCH_FLAG_CHUNKED 0x00000002u
#define USER_NET_FETCH_FLAG_GZIP 0x00000004u
#define USER_NET_FETCH_FLAG_TRUNCATED 0x00000008u
#define USER_NET_REQUEST_METHOD_MAX 8u
#define USER_NET_REQUEST_HEADER_VALUE_MAX 128u
#define USER_NET_REQUEST_BODY_MAX (64u * 1024u)
#define USER_NET_STREAM_HANDLE 1u
#define USER_NET_STREAM_STATE_OPEN 0x00000001u
#define USER_NET_STREAM_STATE_ACTIVE 0x00000002u
#define USER_NET_STREAM_STATE_DONE 0x00000004u
#define USER_NET_STREAM_STATE_OK 0x00000008u
#define USER_NET_STREAM_STATE_ERROR 0x00000010u
#define USER_NET_STREAM_STATE_HAS_DATA 0x00000020u
#define USER_NET_FETCH_TICK_LIMIT 60000u
#define USER_NET_STREAM_IDLE_POLL_LIMIT 32u
#define USER_HEAP_PAGES_DEFAULT 2048u
#define USER_HEAP_PAGES_NETSURF 49152u
#define PIT_MS_PER_TICK 10u
#define USER_APP_NONE 0u
#define USER_APP_UHELLO 1u
#define USER_APP_UGFX 2u
#define USER_APP_UCDEMO 3u
#define USER_APP_UBROWSER 4u
#define USER_APP_UNETRUN 5u
#define USER_APP_UWEB 6u
#define USER_APP_NETSURF 7u
#define USER_APP_USTREAM 8u
#define USER_APP_UQJS 9u
#define USER_APP_MAX_IMAGE_BYTES (4u * 1024u * 1024u)
#define USER_STACK_PAGES_DEFAULT 4u
#define USER_STACK_PAGES_NETSURF 512u
#define USER_STACK_GUARD_PAGES_NETSURF 1u
#define USER_VIRT_BASE 0x40000000u
#define USER_VIRT_PDE (USER_VIRT_BASE >> 22)
#define USER_VIRT_TABLES 64u
#define USER_VIRT_PAGES (USER_VIRT_TABLES * 1024u)

static struct IdtEntry idt[IDT_GATE_COUNT];
static struct IdtPointer idt_pointer;
static struct GdtEntry gdt[6];
static struct GdtPointer gdt_pointer;
static struct Tss tss;
static u8 tss_kernel_stack[65536] __attribute__((aligned(16)));
static u32 user_app_base;
static u32 user_app_limit;
static u32 user_stack_base;
static u32 user_stack_limit;
static u32 user_heap_base;
static u32 user_heap_limit;
static u32 user_heap_next;
static u32 user_heap_first;
static volatile u8 pending_user_app;
static volatile u8 pending_shell_app;
static volatile u8 pending_user_browser_open;
static volatile u8 user_app_running;
static volatile u8 user_app_netsurf_running;
static volatile u8 user_fb_overlay_active;
static char pending_user_browser_url[USER_BROWSER_URL_MAX];
static char user_url_copy[USER_BROWSER_URL_MAX];
static u8 net_user_fetch_active;
static u8 net_user_fetch_done;
static u8 net_user_fetch_ok;
static u8 net_user_fetch_ignore_tail;
static u32 net_user_fetch_len;
static u32 net_user_fetch_content_length;
static u8 net_user_fetch_content_length_seen;
static u32 net_user_fetch_status_code;
static u32 net_user_fetch_flags;
static char net_user_fetch_content_type[USER_NET_FETCH_CONTENT_TYPE_MAX];
static char net_user_fetch_location[USER_NET_FETCH_LOCATION_MAX];
static u8 net_user_fetch_buf[USER_NET_FETCH_MAX];
static char net_user_request_method[USER_NET_REQUEST_METHOD_MAX];
static char net_user_request_content_type[USER_NET_REQUEST_HEADER_VALUE_MAX];
static char net_user_request_accept[USER_NET_REQUEST_HEADER_VALUE_MAX];
static u8 net_user_request_body[USER_NET_REQUEST_BODY_MAX];
static u32 net_user_request_body_len;
static u8 net_user_stream_open;
static u32 net_user_stream_read_pos;
static u32 net_user_stream_start_tick;
static u32 net_user_stream_last_len;
static u32 net_user_stream_idle_polls;
static u8 net_user_stream_read_log_count;
static u8 net_user_stream_busy_log_count;
static u8 net_browser_fetch_enabled;
static u8 kernel_heap[65536] __attribute__((aligned(16)));
static u32 kernel_heap_used;
static u32 page_directory[1024] __attribute__((aligned(PAGE_SIZE)));
static u32 low_page_tables[LOW_IDENTITY_TABLES][1024] __attribute__((aligned(PAGE_SIZE)));
static u32 fb_page_tables[FB_IDENTITY_TABLES][1024] __attribute__((aligned(PAGE_SIZE)));
static u32 user_page_tables[USER_VIRT_TABLES][1024] __attribute__((aligned(PAGE_SIZE)));
static u32 fb_page_table_pdes[FB_IDENTITY_TABLES];
static u32 pmm_bitmap[PMM_MAX_PAGES / 32u];
static u32 pmm_total_pages;
static u32 pmm_free_pages;
static u8 pmm_ready;
static u8 paging_ready;
static u32 heap_page_current;
static u32 heap_page_remaining;
static volatile u32 system_ticks;
static volatile u8 timer_reported;
static volatile u32 event_head;
static volatile u32 event_tail;
static volatile u32 event_dropped;
static struct KernelEvent event_queue[EVENT_QUEUE_CAPACITY];
static struct KernelTask kernel_tasks[MAX_KERNEL_TASKS];
static u32 kernel_task_count;
static volatile u8 scheduler_reported;

static const struct LeonBootInfo *g_boot;
static struct FileEntry files[MAX_FILES];
static u32 file_count;
static u32 selected_file;
static u8 file_loaded;
static u8 dirty = 1;
static char open_name[13];
static u8 file_buffer[FILE_BUFFER_SIZE];
static u32 file_buffer_size;
static u8 using_fat32;
static u8 ata_present;
static u8 fat32_present;
static u32 fat32_partition_lba;
static u32 fat32_sectors_per_cluster;
static u32 fat32_first_fat_sector;
static u32 fat32_first_data_sector;
static u32 fat32_root_cluster;
static u32 fat32_num_fats;
static u32 fat32_fat_size;
static u8 fat32_data_buf[512];
static u8 fat32_fat_cache[512];
static u32 fat32_fat_cache_sector;
static u8 fat32_io_buf[512];
static u8 fat32_rb_buf[512];
static char app_message[64];
static u8 app_message_set;
static u8 mouse_ok;
static i32 mouse_x;
static i32 mouse_y;

static const char msg_memory_entries[] LATE_RODATA = "LeonOS memory map entries: ";
static const char msg_pmm_ready[] = "LeonOS physical page allocator OK free=";
static const char msg_fat32_mounted[] LATE_RODATA = "LeonOS 32-bit FAT32 volume mounted";
static const char msg_gui_ready[] LATE_RODATA = "LeonOS 32-bit GUI ready";
static const char msg_framebuffer_prefix[] = "LeonOS 32-bit framebuffer ";
static const char msg_backbuffer_prefix[] = "LeonOS desktop backbuffer OK bytes=";
static const char msg_root_count_prefix[] LATE_RODATA = "LeonOS 32-bit FAT32 root listed count=";
static const char msg_scheduler_ok[] LATE_RODATA = "LeonOS cooperative scheduler OK";
static const char msg_scheduler_tasks[] LATE_RODATA = "LeonOS cooperative tasks=";
static const char msg_fat32_write_prefix[] = "LeonOS 32-bit FAT32 file written WRITE32.TXT bytes=";
static const char msg_fat32_readback_prefix[] = "LeonOS 32-bit FAT32 write readback OK bytes=";
static const char msg_leo_loaded_prefix[] = "LeonOS LEO1 app HELLOAPP.LEO loaded base=";
static const char msg_user_queued[] = "LeonOS user app queued";
static const char msg_user_launch_begin[] = "LeonOS user app launch begin";
static const char msg_user_image_found[] = "LeonOS user app image found";
static const char msg_user_sector_read[] = "LeonOS user app sector read OK";
static const char msg_user_magic_ok[] = "LeonOS user app magic OK";
static const char msg_user_header_prefix[] = "LeonOS user app header version=";
static const char msg_user_validated[] = "LeonOS user app header validated";
static const char msg_user_alloc_prefix[] = "LeonOS user app pages code=";
static const char msg_user_stack_prefix[] = " stack=";
static const char msg_user_copy_ok[] = "LeonOS user app image copied";
static const char msg_user_pages_ok[] = "LeonOS user app pages user-accessible";
static const char msg_user_pages_bad[] = "LeonOS user app page range bad";
static const char msg_user_enter_prefix[] = "LeonOS user mode enter ";
static const char msg_user_base_prefix[] = " base=";
static const char msg_user_entry_prefix[] = " entry=";
static const char msg_user_fb_info[] = "LeonOS user fb info ";
static const char msg_user_fb_fill[] = "LeonOS user fb fill";
static const char msg_user_fb_present[] = "LeonOS user fb present";
static const char msg_user_event_poll[] = "LeonOS user event poll";
static const char msg_user_fb_blit[] = "LeonOS user fb blit";
static const char msg_cpu_exception[] = "CPU exception ";
static const char msg_cpu_error[] = " error ";
static const char msg_cpu_eip[] = " eip ";
static const char msg_cpu_cr2[] = " cr2 ";
static const char msg_net_pci_prefix[] LATE_RODATA = "LeonOS PCI RTL8139 found io=";
static const char msg_net_irq_prefix[] LATE_RODATA = " irq=";
static const char msg_net_mac_prefix[] LATE_RODATA = "LeonOS RTL8139 MAC ";
static const char msg_net_rx_prefix[] LATE_RODATA = "LeonOS RTL8139 RX ring ";
static const char msg_net_tx_prefix[] LATE_RODATA = "LeonOS RTL8139 TX ring ";
static const char msg_net_link_ready[] LATE_RODATA = "LeonOS RTL8139 link ready";
static const char msg_net_arp_request[] LATE_RODATA = "LeonOS net ARP gateway request sent";
static const char msg_net_arp_resolved_prefix[] LATE_RODATA = "LeonOS net ARP gateway resolved ";
static const char msg_net_icmp_request[] LATE_RODATA = "LeonOS net ICMP echo request sent";
static const char msg_net_icmp_reply[] LATE_RODATA = "LeonOS net ICMP echo reply received";
static const char msg_net_dns_arp_request[] LATE_RODATA = "LeonOS net ARP DNS request sent";
static const char msg_net_dns_resolved_prefix[] LATE_RODATA = "LeonOS net ARP DNS resolved ";
static const char msg_net_dns_query[] LATE_RODATA = "LeonOS net DNS query sent";
static const char msg_net_dns_reply_prefix[] LATE_RODATA = "LeonOS net DNS A ";
static const char msg_net_tcp_syn[] LATE_RODATA = "LeonOS net TCP 80 SYN sent";
static const char msg_net_tcp_connected[] LATE_RODATA = "LeonOS net TCP 80 connected";
static const char msg_net_http_get[] LATE_RODATA = "LeonOS net HTTP GET sent";
static const char msg_net_http_status_prefix[] LATE_RODATA = "LeonOS net HTTP status ";
static const char msg_net_tls_syn[] LATE_RODATA = "LeonOS net TCP 443 SYN sent";
static const char msg_net_tls_connected[] LATE_RODATA = "LeonOS net TCP 443 connected";
static const char msg_net_tls_client_hello[] LATE_RODATA = "LeonOS net TLS ClientHello sent";
static const char msg_net_tls_server_hello[] LATE_RODATA = "LeonOS net TLS ServerHello received";
static const char msg_net_tls_keys_ready[] LATE_RODATA = "LeonOS net TLS handshake keys ready";
static const char msg_net_tls_finished[] LATE_RODATA = "LeonOS net TLS Finished sent";
static const char msg_net_https_get[] LATE_RODATA = "LeonOS net HTTPS GET sent";
static const char msg_net_https_status_prefix[] LATE_RODATA = "LeonOS net HTTPS status ";
static const char msg_net_browser_structure[] LATE_RODATA = "LeonOS net browser HTML structure tags ";
static const char msg_net_browser_resources[] LATE_RODATA = "LeonOS net browser HTML resources attrs ";
static const char msg_net_browser_queue[] LATE_RODATA = "LeonOS net browser resource queue total ";
static const char msg_net_browser_render[] LATE_RODATA = "LeonOS net browser render blocks ";
static const char msg_net_browser_css[] LATE_RODATA = "LeonOS net browser CSS rules ";
static const char msg_net_browser_js[] LATE_RODATA = "LeonOS net browser JS scripts ";
static const char msg_net_browser_dom[] LATE_RODATA = "LeonOS net browser DOM nodes ";
static const char msg_net_browser_layout[] LATE_RODATA = "LeonOS net browser layout boxes ";
static const char msg_net_browser_link_click[] LATE_RODATA = "LeonOS net browser link click ";
static const char msg_net_browser_navigate[] LATE_RODATA = "LeonOS net browser navigate ";
static const char msg_net_browser_state[] LATE_RODATA = "LeonOS net browser state ";
static const char msg_net_browser_history[] LATE_RODATA = "LeonOS net browser history ";
static const char msg_net_browser_unsupported[] LATE_RODATA = "LeonOS net browser unsupported summary ";
static const char msg_net_browser_resource_syn[] LATE_RODATA = "LeonOS net browser resource HTTPS SYN sent";
static const char msg_net_browser_resource_get[] LATE_RODATA = "LeonOS net browser resource HTTPS GET ";
static const char msg_net_browser_resource_status_prefix[] LATE_RODATA = "LeonOS net browser resource status ";
static const char msg_net_browser_resource_done_prefix[] LATE_RODATA = "LeonOS net browser resource done status ";
static const char msg_net_tls_decrypt_bad[] LATE_RODATA = "LeonOS net TLS decrypt failed";
static const char msg_net_tls_finished_bad[] LATE_RODATA = "LeonOS net TLS Finished verify failed";

static u8 mouse_buttons;
static u8 prev_mouse_buttons;
static u8 mouse_packet_index;
static u8 mouse_packet0;
static u8 mouse_packet1;
static u8 mouse_packet2;
static u8 keyboard_ctrl_down;
static u8 cursor_drawn;
static i32 cursor_saved_x;
static i32 cursor_saved_y;
static u32 cursor_back[CURSOR_W * CURSOR_H];
static volatile u32 *draw_pixels_override;
static u32 draw_stride_override;
static u32 *framebuffer_back;
static u32 framebuffer_back_stride;
static u8 framebuffer_back_ready;
static u8 net_present;
static u8 net_ready;
static u8 net_mac[6];
static const u8 net_ip[4] = { 10u, 0u, 2u, 15u };
static const u8 net_gateway_ip[4] = { 10u, 0u, 2u, 2u };
static const u8 net_dns_ip[4] = { 10u, 0u, 2u, 3u };
static u8 net_gateway_mac[6];
static u8 net_dns_mac[6];
static u8 net_gateway_mac_valid;
static u8 net_dns_mac_valid;
static u8 net_arp_requested;
static u8 net_dns_arp_requested;
static u8 net_icmp_probe_sent;
static u8 net_icmp_reply_seen;
static u8 net_dns_query_sent;
static u8 net_dns_reply_seen;
static u8 net_dns_a_record[4];
static u8 net_dns_a_records[NET_DNS_A_MAX][4];
static u8 net_dns_a_count;
static u8 net_dns_a_index;
static u8 net_tcp_syn_sent;
static u8 net_tcp_connected;
static u8 net_http_get_sent;
static u8 net_http_response_seen;
static char net_browser_status[4] = "RDY";
static char net_browser_line[72] = "NO REQUEST";
static char net_browser_location[72] = "";
static u32 net_tcp_next_seq;
static u32 net_tcp_ack;
static u8 net_tls_syn_sent;
static u8 net_tls_connected;
static u8 net_tls_client_hello_sent;
static u8 net_tls_server_hello_seen;
static u8 net_tls_syn_retry_count;
static u8 net_tls_fin_pending;
static u32 net_tls_syn_tick;
static u32 net_tls_fin_seq;
static u8 net_tls_fetch_kind;
static u16 net_tls_source_port;
static u32 net_tls_next_seq;
static u32 net_tls_ack;
static u32 net_rx_frames;
static u32 net_tx_frames;
static u32 net_arp_frames;
static u32 net_ipv4_frames;
static u32 net_icmp_rx;
static u32 net_icmp_tx;
static u32 net_udp_rx;
static u32 net_udp_tx;
static u32 net_tcp_rx;
static u32 net_tcp_tx;
static u16 rtl8139_io_base;
static u8 rtl8139_irq;
static u8 rtl8139_running;
static u8 rtl8139_rx_enabled;
static u8 *rtl8139_rx_buffer;
static u8 *rtl8139_tx_region;
static u16 rtl8139_rx_cur;
static u8 *rtl8139_tx_buffers[4];
static u8 rtl8139_tx_slot;
static u8 rtl8139_tx_used[4];
static u8 net_rx_frame[NET_FRAME_MAX] __attribute__((aligned(4)));
static u8 net_tx_packet[NET_FRAME_MAX] __attribute__((aligned(4)));
static char net_last_event[40] = "NO NIC";

static void outb(u16 port, u8 value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u16 inw(u16 port)
{
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outw(u16 port, u16 value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static u32 inl(u16 port)
{
    u32 value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outl(u16 port, u32 value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void write16(u8 *ptr, u16 value)
{
    ptr[0] = (u8) (value & 0xFFu);
    ptr[1] = (u8) ((value >> 8) & 0xFFu);
}

static void write32(u8 *ptr, u32 value)
{
    ptr[0] = (u8) (value & 0xFFu);
    ptr[1] = (u8) ((value >> 8) & 0xFFu);
    ptr[2] = (u8) ((value >> 16) & 0xFFu);
    ptr[3] = (u8) ((value >> 24) & 0xFFu);
}

static void mem_zero(void *ptr, u32 len)
{
    u8 *p = (u8 *) ptr;
    for (u32 i = 0; i < len; i += 1u) {
        p[i] = 0;
    }
}

static void mem_copy(void *dst, const void *src, u32 len)
{
    u8 *d = (u8 *) dst;
    const u8 *s = (const u8 *) src;
    for (u32 i = 0; i < len; i += 1u) {
        d[i] = s[i];
    }
}

static u8 mem_equal(const void *a, const void *b, u32 len)
{
    const u8 *pa = (const u8 *) a;
    const u8 *pb = (const u8 *) b;
    for (u32 i = 0; i < len; i += 1u) {
        if (pa[i] != pb[i]) {
            return 0;
        }
    }
    return 1;
}

static u16 read_be16(const u8 *ptr)
{
    return (u16) (((u16) ptr[0] << 8) | ptr[1]);
}

static u32 read_be32(const u8 *ptr)
{
    return ((u32) ptr[0] << 24) |
           ((u32) ptr[1] << 16) |
           ((u32) ptr[2] << 8) |
           ptr[3];
}

static void write_be16(u8 *ptr, u16 value)
{
    ptr[0] = (u8) (value >> 8);
    ptr[1] = (u8) (value & 0xFFu);
}

static void write_be32_value(u8 *ptr, u32 value)
{
    ptr[0] = (u8) (value >> 24);
    ptr[1] = (u8) (value >> 16);
    ptr[2] = (u8) (value >> 8);
    ptr[3] = (u8) value;
}

static void write_be32(u8 *ptr, const u8 ip[4])
{
    ptr[0] = ip[0];
    ptr[1] = ip[1];
    ptr[2] = ip[2];
    ptr[3] = ip[3];
}

static void serial_write(char value)
{
    while ((inb(0x3F8u + 5u) & 0x20u) == 0) {
    }
    outb(0x3F8u, (u8) value);
}

static void serial_print(const char *text)
{
    for (u32 index = 0; text[index] != 0; index += 1) {
        serial_write(text[index]);
    }
}

static void serial_print_line(const char *text)
{
    serial_print(text);
    serial_write('\r');
    serial_write('\n');
}

static char hex_digit(u8 value)
{
    value &= 0x0Fu;
    return (char) (value < 10u ? ('0' + value) : ('A' + value - 10u));
}

static void serial_print_hex(u32 value)
{
    serial_print("0x");
    for (u32 index = 0; index < 8u; index += 1u) {
        u32 shift = 28u - index * 4u;
        serial_write(hex_digit((u8) (value >> shift)));
    }
}

static void serial_print_dec(u32 value)
{
    char buffer[11];
    u32 index = 0;

    if (value == 0) {
        serial_write('0');
        return;
    }

    while (value > 0 && index < sizeof(buffer)) {
        buffer[index] = (char) ('0' + (value % 10u));
        value /= 10u;
        index += 1;
    }

    while (index > 0) {
        index -= 1;
        serial_write(buffer[index]);
    }
}

static void vga_write(const char *text, u8 color)
{
    u32 index = 0;
    while (text[index] != 0) {
        vga_text[index] = ((u16) color << 8) | (u8) text[index];
        index += 1;
    }
}

static void panic(const char *message)
{
    serial_print("PANIC: ");
    serial_print(message);
    serial_write('\r');
    serial_write('\n');
    vga_write("LeonOS kernel panic - see serial log", 0x4F);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void idt_set_gate_dpl(u32 vector, void (*handler)(void), u8 type_attr)
{
    u32 address = (u32) handler;
    idt[vector].offset_low = (u16) (address & 0xFFFFu);
    idt[vector].selector = 0x08u;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (u16) ((address >> 16) & 0xFFFFu);
}

static void idt_set_gate(u32 vector, void (*handler)(void))
{
    idt_set_gate_dpl(vector, handler, 0x8Eu);
}

static void idt_init(void)
{
    for (u32 index = 0; index < IDT_GATE_COUNT; index += 1) {
        idt_set_gate(index, isr31);
    }

    idt_set_gate(0, isr0);   idt_set_gate(1, isr1);
    idt_set_gate(2, isr2);   idt_set_gate(3, isr3);
    idt_set_gate(4, isr4);   idt_set_gate(5, isr5);
    idt_set_gate(6, isr6);   idt_set_gate(7, isr7);
    idt_set_gate(8, isr8);   idt_set_gate(9, isr9);
    idt_set_gate(10, isr10); idt_set_gate(11, isr11);
    idt_set_gate(12, isr12); idt_set_gate(13, isr13);
    idt_set_gate(14, isr14); idt_set_gate(15, isr15);
    idt_set_gate(16, isr16); idt_set_gate(17, isr17);
    idt_set_gate(18, isr18); idt_set_gate(19, isr19);
    idt_set_gate(20, isr20); idt_set_gate(21, isr21);
    idt_set_gate(22, isr22); idt_set_gate(23, isr23);
    idt_set_gate(24, isr24); idt_set_gate(25, isr25);
    idt_set_gate(26, isr26); idt_set_gate(27, isr27);
    idt_set_gate(28, isr28); idt_set_gate(29, isr29);
    idt_set_gate(30, isr30); idt_set_gate(31, isr31);

    idt_set_gate(32, irq0);  idt_set_gate(33, irq1);
    idt_set_gate(34, irq2);  idt_set_gate(35, irq3);
    idt_set_gate(36, irq4);  idt_set_gate(37, irq5);
    idt_set_gate(38, irq6);  idt_set_gate(39, irq7);
    idt_set_gate(40, irq8);  idt_set_gate(41, irq9);
    idt_set_gate(42, irq10); idt_set_gate(43, irq11);
    idt_set_gate(44, irq12); idt_set_gate(45, irq13);
    idt_set_gate(46, irq14); idt_set_gate(47, irq15);

    /* int 0x80 must be reachable from ring 3: present, DPL=3, 32-bit int gate. */
    idt_set_gate_dpl(0x80u, isr128, 0xEEu);

    idt_pointer.limit = (u16) (sizeof(idt) - 1u);
    idt_pointer.base = (u32) &idt[0];
    idt_load(&idt_pointer);
}

static void gdt_set_entry(int index, u32 base, u32 limit, u8 access, u8 granularity)
{
    gdt[index].limit_low = (u16) (limit & 0xFFFFu);
    gdt[index].base_low = (u16) (base & 0xFFFFu);
    gdt[index].base_mid = (u8) ((base >> 16) & 0xFFu);
    gdt[index].access = access;
    gdt[index].granularity = (u8) (((limit >> 16) & 0x0Fu) | (granularity & 0xF0u));
    gdt[index].base_high = (u8) ((base >> 24) & 0xFFu);
}

/*
 * Replace stage2's minimal GDT with one that also has ring-3 user code/data
 * descriptors and a TSS. The kernel code/data selectors (0x08/0x10) keep the
 * same meaning so existing segment registers stay valid. The TSS only carries
 * ss0/esp0 so the CPU has a ring-0 stack to switch to on an int 0x80 / IRQ
 * that arrives while running in ring 3.
 */
static void gdt_init(void)
{
    gdt_set_entry(0, 0, 0, 0x00u, 0x00u);
    gdt_set_entry(1, 0, 0xFFFFFu, 0x9Au, 0xC0u); /* kernel code, DPL0 */
    gdt_set_entry(2, 0, 0xFFFFFu, 0x92u, 0xC0u); /* kernel data, DPL0 */
    gdt_set_entry(3, 0, 0xFFFFFu, 0xFAu, 0xC0u); /* user code,   DPL3 */
    gdt_set_entry(4, 0, 0xFFFFFu, 0xF2u, 0xC0u); /* user data,   DPL3 */

    u8 *tss_bytes = (u8 *) &tss;
    for (u32 i = 0; i < sizeof(tss); i += 1) {
        tss_bytes[i] = 0;
    }
    tss.ss0 = KERNEL_DATA_SEL;
    tss.esp0 = (u32) (tss_kernel_stack + sizeof(tss_kernel_stack));
    tss.iomap_base = (u16) sizeof(tss);
    gdt_set_entry(5, (u32) &tss, sizeof(tss) - 1u, 0x89u, 0x00u); /* 32-bit TSS */

    gdt_pointer.limit = (u16) (sizeof(gdt) - 1u);
    gdt_pointer.base = (u32) &gdt[0];
    gdt_flush(&gdt_pointer);
    tss_flush();
    serial_print("LeonOS 32-bit GDT/TSS ring3 ready\r\n");
}

static void pic_remap_and_mask(void)
{
    outb(0x20u, 0x11u);
    outb(0xA0u, 0x11u);
    outb(0x21u, 0x20u);
    outb(0xA1u, 0x28u);
    outb(0x21u, 0x04u);
    outb(0xA1u, 0x02u);
    outb(0x21u, 0x01u);
    outb(0xA1u, 0x01u);
    outb(0x21u, 0xF8u);
    outb(0xA1u, 0xEFu);
}

static void pic_eoi(u32 vector)
{
    if (vector >= 40u) {
        outb(0xA0u, 0x20u);
    }
    outb(0x20u, 0x20u);
}

static void pit_init_masked(void)
{
    const u16 divisor = 11931u;
    outb(0x43u, 0x36u);
    outb(0x40u, (u8) (divisor & 0xFFu));
    outb(0x40u, (u8) ((divisor >> 8) & 0xFFu));
}

static u8 ps2_wait_input_clear(void)
{
    for (u32 tries = 0; tries < 100000u; tries += 1) {
        if ((inb(0x64u) & 0x02u) == 0) {
            return 1;
        }
    }
    return 0;
}

static u8 ps2_wait_output_full(void)
{
    for (u32 tries = 0; tries < 100000u; tries += 1) {
        if ((inb(0x64u) & 0x01u) != 0) {
            return 1;
        }
    }
    return 0;
}

static void ps2_flush_output(void)
{
    for (u32 tries = 0; tries < 16u && (inb(0x64u) & 0x01u) != 0; tries += 1) {
        (void) inb(0x60u);
    }
}

static u8 ps2_aux_write(u8 value)
{
    if (!ps2_wait_input_clear()) {
        return 0;
    }
    outb(0x64u, 0xD4u);
    if (!ps2_wait_input_clear()) {
        return 0;
    }
    outb(0x60u, value);
    if (!ps2_wait_output_full()) {
        return 0;
    }
    return inb(0x60u) == 0xFAu;
}

static u8 ps2_aux_write_param(u8 command, u8 value)
{
    if (!ps2_aux_write(command)) {
        return 0;
    }
    return ps2_aux_write(value);
}

static void mouse_init(void)
{
    ps2_flush_output();
    if (!ps2_wait_input_clear()) {
        return;
    }
    outb(0x64u, 0xA8u);

    if (!ps2_wait_input_clear()) {
        return;
    }
    outb(0x64u, 0x20u);
    if (!ps2_wait_output_full()) {
        return;
    }
    u8 command_byte = inb(0x60u);
    command_byte = (u8) ((command_byte | 0x02u) & ~0x20u);

    if (!ps2_wait_input_clear()) {
        return;
    }
    outb(0x64u, 0x60u);
    if (!ps2_wait_input_clear()) {
        return;
    }
    outb(0x60u, command_byte);

    if (!ps2_aux_write(0xF6u)) {
        return;
    }
    (void) ps2_aux_write(0xE6u);
    (void) ps2_aux_write_param(0xE8u, 0x03u);
    (void) ps2_aux_write_param(0xF3u, 100u);
    if (!ps2_aux_write(0xF4u)) {
        return;
    }

    mouse_ok = 1;
    serial_print("LeonOS 32-bit PS/2 mouse OK\r\n");
}

static u32 align_down(u32 value, u32 align)
{
    return value & ~(align - 1u);
}

static u32 align_up(u32 value, u32 align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static u8 event_queue_contains_type(u32 type);

static void event_push(u32 type, u32 data0, u32 data1)
{
    u32 next = (event_head + 1u) % EVENT_QUEUE_CAPACITY;
    if (type == EVENT_TIMER && event_queue_contains_type(EVENT_TIMER)) {
        return;
    }
    if (next == event_tail) {
        event_dropped += 1;
        if (type != EVENT_KEYBOARD && type != EVENT_MOUSE_BUTTON) {
            return;
        }
        event_tail = (event_tail + 1u) % EVENT_QUEUE_CAPACITY;
    }
    event_queue[event_head].type = type;
    event_queue[event_head].data0 = data0;
    event_queue[event_head].data1 = data1;
    event_head = next;
}

static u8 event_pop(struct KernelEvent *out)
{
    if (event_tail == event_head) {
        return 0;
    }

    out->type = event_queue[event_tail].type;
    out->data0 = event_queue[event_tail].data0;
    out->data1 = event_queue[event_tail].data1;
    event_tail = (event_tail + 1u) % EVENT_QUEUE_CAPACITY;
    return 1;
}

static u8 event_queue_contains_type(u32 type)
{
    u32 index = event_tail;
    while (index != event_head) {
        if (event_queue[index].type == type) {
            return 1u;
        }
        index = (index + 1u) % EVENT_QUEUE_CAPACITY;
    }
    return 0u;
}

static void event_clear(void)
{
    event_tail = event_head;
}

static void pmm_set_free(u32 page_index, u8 free)
{
    u32 word = page_index / 32u;
    u32 bit = page_index % 32u;
    u32 mask = 1u << bit;
    u8 was_free = (pmm_bitmap[word] & mask) != 0;

    if (free && !was_free) {
        pmm_bitmap[word] |= mask;
        pmm_free_pages += 1;
    } else if (!free && was_free) {
        pmm_bitmap[word] &= ~mask;
        pmm_free_pages -= 1;
    }
}

static u8 pmm_is_free(u32 page_index)
{
    return (pmm_bitmap[page_index / 32u] & (1u << (page_index % 32u))) != 0;
}

static void pmm_mark_range(u32 base, u32 length, u8 free)
{
    if (length == 0 || base >= PMM_MAX_MEMORY) {
        return;
    }

    u32 end = base + length;
    if (end < base || end > PMM_MAX_MEMORY) {
        end = PMM_MAX_MEMORY;
    }

    u32 first = (free ? align_up(base, PAGE_SIZE) : align_down(base, PAGE_SIZE)) / PAGE_SIZE;
    u32 last = (free ? align_down(end, PAGE_SIZE) : align_up(end, PAGE_SIZE)) / PAGE_SIZE;
    for (u32 page = first; page < last && page < PMM_MAX_PAGES; page += 1) {
        pmm_set_free(page, free);
    }
}

static void pmm_init(const struct LeonBootInfo *boot_info)
{
    pmm_total_pages = PMM_MAX_PAGES;
    pmm_free_pages = 0;
    for (u32 index = 0; index < (PMM_MAX_PAGES / 32u); index += 1) {
        pmm_bitmap[index] = 0;
    }

    for (u32 index = 0; index < boot_info->memory_map_count; index += 1) {
        const struct LeonMemoryMapEntry *entry = &boot_info->memory_map[index];
        if (entry->type != 1u || (entry->base >> 32) != 0) {
            continue;
        }

        u32 base = (u32) entry->base;
        u32 length = (entry->length > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (u32) entry->length;
        pmm_mark_range(base, length, 1);
    }

    pmm_mark_range(0, 0x00100000u, 0);
    pmm_mark_range((u32) &__kernel_start, (u32) (&__kernel_end - &__kernel_start), 0);
    pmm_ready = 1;
    serial_print(msg_pmm_ready);
    serial_print_dec(pmm_free_pages);
    serial_write('\r');
    serial_write('\n');
}

static u32 pmm_alloc_page(void)
{
    if (!pmm_ready) {
        panic("pmm alloc before init");
    }

    for (u32 page = 0x100000u / PAGE_SIZE; page < PMM_MAX_PAGES; page += 1) {
        if (pmm_is_free(page)) {
            pmm_set_free(page, 0);
            return page * PAGE_SIZE;
        }
    }

    panic("out of physical pages");
    return 0;
}

static u32 pmm_alloc_contiguous_pages(u32 count)
{
    if (count == 0u) {
        return 0;
    }
    if (!pmm_ready) {
        panic("pmm run alloc before init");
    }

    u32 first_page = 0x100000u / PAGE_SIZE;
    for (u32 page = first_page; page + count <= PMM_MAX_PAGES; page += 1) {
        u8 found = 1u;
        for (u32 offset = 0; offset < count; offset += 1) {
            if (!pmm_is_free(page + offset)) {
                found = 0u;
                page += offset;
                break;
            }
        }
        if (!found) {
            continue;
        }
        for (u32 offset = 0; offset < count; offset += 1) {
            pmm_set_free(page + offset, 0);
        }
        return page * PAGE_SIZE;
    }

    panic("out of contiguous physical pages");
    return 0;
}

static void pmm_free_page(u32 address)
{
    if ((address % PAGE_SIZE) != 0 || address >= PMM_MAX_MEMORY) {
        panic("invalid physical page free");
    }
    pmm_set_free(address / PAGE_SIZE, 1);
}

static void map_identity_page(u32 physical)
{
    u32 pde = physical >> 22;
    u32 pte = (physical >> 12) & 0x3FFu;
    u32 *table = 0;

    if (pde < LOW_IDENTITY_TABLES) {
        table = low_page_tables[pde];
    } else {
        for (u32 slot = 0; slot < FB_IDENTITY_TABLES; slot += 1) {
            if (fb_page_table_pdes[slot] == pde) {
                table = fb_page_tables[slot];
                break;
            }
            if (fb_page_table_pdes[slot] == 0xFFFFFFFFu) {
                fb_page_table_pdes[slot] = pde;
                page_directory[pde] = ((u32) fb_page_tables[slot]) | 0x003u;
                table = fb_page_tables[slot];
                break;
            }
        }
    }

    if (table == 0) {
        panic("out of framebuffer page tables");
    }

    table[pte] = (physical & 0xFFFFF000u) | 0x003u;
}

static void paging_init(const struct LeonBootInfo *boot_info)
{
    for (u32 index = 0; index < 1024u; index += 1) {
        page_directory[index] = 0;
    }
    for (u32 slot = 0; slot < FB_IDENTITY_TABLES; slot += 1) {
        fb_page_table_pdes[slot] = 0xFFFFFFFFu;
        for (u32 index = 0; index < 1024u; index += 1) {
            fb_page_tables[slot][index] = 0;
        }
    }

    for (u32 table = 0; table < LOW_IDENTITY_TABLES; table += 1) {
        page_directory[table] = ((u32) low_page_tables[table]) | 0x003u;
        for (u32 entry = 0; entry < 1024u; entry += 1) {
            u32 physical = (table << 22) + (entry << 12);
            low_page_tables[table][entry] = physical | 0x003u;
        }
    }

    u32 fb_start = align_down(boot_info->framebuffer.address, PAGE_SIZE);
    u32 fb_bytes = boot_info->framebuffer.pitch * boot_info->framebuffer.height;
    u32 fb_end = align_up(boot_info->framebuffer.address + fb_bytes, PAGE_SIZE);
    for (u32 physical = fb_start; physical < fb_end; physical += PAGE_SIZE) {
        map_identity_page(physical);
    }

    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory) : "memory");
    u32 cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    paging_ready = 1;
    serial_print("LeonOS paging enabled\r\n");
}

static void *kmalloc(u32 size, u32 align)
{
    if (pmm_ready) {
        if (align < 1u) {
            align = 1u;
        }

        u32 current = heap_page_current;
        if (align > 1u) {
            u32 mask = align - 1u;
            current = (current + mask) & ~mask;
        }

        if (heap_page_remaining == 0 || current < heap_page_current ||
            (current - heap_page_current) + size > heap_page_remaining) {
            if (size > PAGE_SIZE) {
                panic("large kmalloc unsupported");
            }
            heap_page_current = pmm_alloc_page();
            heap_page_remaining = PAGE_SIZE;
            current = heap_page_current;
            if (align > 1u) {
                u32 mask = align - 1u;
                current = (current + mask) & ~mask;
            }
        }

        u32 used = (current - heap_page_current) + size;
        heap_page_remaining -= used;
        heap_page_current = current + size;
        return (void *) current;
    }

    u32 current = kernel_heap_used;
    if (align > 1u) {
        u32 mask = align - 1u;
        current = (current + mask) & ~mask;
    }
    if (size > sizeof(kernel_heap) || current > (sizeof(kernel_heap) - size)) {
        panic("kernel heap exhausted");
    }
    kernel_heap_used = current + size;
    return &kernel_heap[current];
}

static void heap_smoke_test(void)
{
    volatile u32 *probe = (volatile u32 *) kmalloc(sizeof(u32), 4u);
    *probe = 0x1E0A05u;
    if (*probe != 0x1E0A05u) {
        panic("kernel heap smoke test failed");
    }
    serial_print("LeonOS page-backed heap OK\r\n");
}

static void pmm_smoke_test(void)
{
    u32 first = pmm_alloc_page();
    u32 second = pmm_alloc_page();
    volatile u32 *first_ptr = (volatile u32 *) first;
    volatile u32 *second_ptr = (volatile u32 *) second;

    *first_ptr = 0xC0FFEE01u;
    *second_ptr = 0xC0FFEE02u;
    if (*first_ptr != 0xC0FFEE01u || *second_ptr != 0xC0FFEE02u || first == second) {
        panic("physical page allocator smoke test failed");
    }

    pmm_free_page(second);
    pmm_free_page(first);
    serial_print("LeonOS physical page alloc/free OK\r\n");
}

static volatile u32 *fb_pixels(void)
{
    if (draw_pixels_override != 0) {
        return draw_pixels_override;
    }
    return (volatile u32 *) g_boot->framebuffer.address;
}

static u32 fb_stride(void)
{
    if (draw_pixels_override != 0) {
        return draw_stride_override;
    }
    return g_boot->framebuffer.pitch / 4u;
}

static void framebuffer_back_init(void)
{
    u32 bytes = g_boot->framebuffer.pitch * g_boot->framebuffer.height;
    u32 pages = align_up(bytes, PAGE_SIZE) / PAGE_SIZE;
    framebuffer_back = (u32 *) pmm_alloc_contiguous_pages(pages);
    framebuffer_back_stride = g_boot->framebuffer.pitch / 4u;
    for (u32 i = 0; i < (pages * PAGE_SIZE) / 4u; i += 1) {
        framebuffer_back[i] = 0;
    }
    framebuffer_back_ready = 1u;
    serial_print(msg_backbuffer_prefix);
    serial_print_dec(bytes);
    serial_write('\r');
    serial_write('\n');
}

static void framebuffer_present(void)
{
    if (!framebuffer_back_ready) {
        return;
    }
    volatile u32 *front = (volatile u32 *) g_boot->framebuffer.address;
    u32 front_stride = g_boot->framebuffer.pitch / 4u;
    if (front_stride == framebuffer_back_stride) {
        u32 count = front_stride * g_boot->framebuffer.height;
        for (u32 i = 0; i < count; i += 1) {
            front[i] = framebuffer_back[i];
        }
    } else {
        for (u32 y = 0; y < g_boot->framebuffer.height; y += 1) {
            for (u32 x = 0; x < g_boot->framebuffer.width; x += 1) {
                front[y * front_stride + x] = framebuffer_back[y * framebuffer_back_stride + x];
            }
        }
    }
}

static u32 read_pixel(i32 x, i32 y)
{
    if (g_boot->framebuffer.address == 0 ||
        x < 0 || y < 0 ||
        (u32) x >= g_boot->framebuffer.width ||
        (u32) y >= g_boot->framebuffer.height) {
        return 0;
    }

    volatile u32 *pixels = fb_pixels();
    return pixels[(u32) y * fb_stride() + (u32) x];
}

static void write_pixel(i32 x, i32 y, u32 color)
{
    if (g_boot->framebuffer.address == 0 ||
        x < 0 || y < 0 ||
        (u32) x >= g_boot->framebuffer.width ||
        (u32) y >= g_boot->framebuffer.height) {
        return;
    }

    volatile u32 *pixels = fb_pixels();
    pixels[(u32) y * fb_stride() + (u32) x] = color;
}

static void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    if (g_boot->framebuffer.address == 0) {
        return;
    }
    if (x >= g_boot->framebuffer.width || y >= g_boot->framebuffer.height) {
        return;
    }
    if (w > g_boot->framebuffer.width - x) {
        w = g_boot->framebuffer.width - x;
    }
    if (h > g_boot->framebuffer.height - y) {
        h = g_boot->framebuffer.height - y;
    }

    volatile u32 *pixels = fb_pixels();
    u32 stride = fb_stride();
    for (u32 row = 0; row < h; row += 1) {
        for (u32 col = 0; col < w; col += 1) {
            pixels[(y + row) * stride + x + col] = color;
        }
    }
}

static u32 sx(u32 ref_x)
{
    return (ref_x * g_boot->framebuffer.width + GUI_REF_W / 2u) / GUI_REF_W;
}

static u32 sy(u32 ref_y)
{
    return (ref_y * g_boot->framebuffer.height + GUI_REF_H / 2u) / GUI_REF_H;
}

static u8 framebuffer_resolution_supported(u32 width, u32 height)
{
    return (width == 1920u && height == 1080u) ||
           (width == 1366u && height == 768u) ||
           (width == 1280u && height == 720u) ||
           (width == 1024u && height == 768u);
}

static u8 gui_font_scale(void)
{
    return 1u;
}

static u32 gui_char_advance(void)
{
    return 12u * (u32) gui_font_scale();
}

static u32 gui_line_height(void)
{
    return 3u * (u32) gui_font_scale() * 7u;
}

static void draw_glyph_rows_scaled(u32 x, u32 y, u32 color, u8 scale, u8 r0, u8 r1, u8 r2, u8 r3, u8 r4, u8 r5, u8 r6)
{
    u8 rows[7] = { r0, r1, r2, r3, r4, r5, r6 };
    u32 cell_w = 2u * scale;
    u32 cell_h = 2u * scale;
    for (u32 row = 0; row < 7u; row += 1) {
        for (u32 col = 0; col < 5u; col += 1) {
            if ((rows[row] & (1u << (4u - col))) != 0) {
                fill_rect(x + col * cell_w, y + row * cell_h, cell_w, cell_h, color);
            }
        }
    }
}

static void draw_char_scaled(u32 x, u32 y, char ch, u32 color, u8 scale)
{
    switch (ch) {
    case 'A': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u); break;
    case 'B': draw_glyph_rows_scaled(x, y, color, scale, 0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu); break;
    case 'C': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu); break;
    case 'D': draw_glyph_rows_scaled(x, y, color, scale, 0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu); break;
    case 'E': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu); break;
    case 'F': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u); break;
    case 'G': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x10u, 0x13u, 0x11u, 0x11u, 0x0Fu); break;
    case 'H': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u); break;
    case 'I': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x1Fu); break;
    case 'J': draw_glyph_rows_scaled(x, y, color, scale, 0x07u, 0x02u, 0x02u, 0x02u, 0x12u, 0x12u, 0x0Cu); break;
    case 'K': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u); break;
    case 'L': draw_glyph_rows_scaled(x, y, color, scale, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu); break;
    case 'M': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u); break;
    case 'N': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u); break;
    case 'O': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu); break;
    case 'P': draw_glyph_rows_scaled(x, y, color, scale, 0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u); break;
    case 'Q': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du); break;
    case 'R': draw_glyph_rows_scaled(x, y, color, scale, 0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u); break;
    case 'S': draw_glyph_rows_scaled(x, y, color, scale, 0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu); break;
    case 'T': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u); break;
    case 'U': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu); break;
    case 'V': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u); break;
    case 'W': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0Au); break;
    case 'X': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u, 0x11u); break;
    case 'Y': draw_glyph_rows_scaled(x, y, color, scale, 0x11u, 0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u); break;
    case 'Z': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu); break;
    case '0': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0Eu); break;
    case '1': draw_glyph_rows_scaled(x, y, color, scale, 0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu); break;
    case '2': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu); break;
    case '3': draw_glyph_rows_scaled(x, y, color, scale, 0x1Eu, 0x01u, 0x01u, 0x0Eu, 0x01u, 0x01u, 0x1Eu); break;
    case '4': draw_glyph_rows_scaled(x, y, color, scale, 0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u); break;
    case '5': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x1Eu); break;
    case '6': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x10u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu); break;
    case '7': draw_glyph_rows_scaled(x, y, color, scale, 0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u); break;
    case '8': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu); break;
    case '9': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x01u, 0x0Eu); break;
    case '.': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu); break;
    case ':': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x0Cu, 0x0Cu, 0x00u); break;
    case '/': draw_glyph_rows_scaled(x, y, color, scale, 0x01u, 0x02u, 0x02u, 0x04u, 0x08u, 0x08u, 0x10u); break;
    case '-': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x00u, 0x1Eu, 0x00u, 0x00u, 0x00u); break;
    case 'a': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Eu, 0x01u, 0x0Fu, 0x11u, 0x0Fu); break;
    case 'b': draw_glyph_rows_scaled(x, y, color, scale, 0x10u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x11u, 0x1Eu); break;
    case 'c': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Eu, 0x10u, 0x10u, 0x11u, 0x0Eu); break;
    case 'd': draw_glyph_rows_scaled(x, y, color, scale, 0x01u, 0x01u, 0x0Fu, 0x11u, 0x11u, 0x11u, 0x0Fu); break;
    case 'e': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Eu, 0x11u, 0x1Fu, 0x10u, 0x0Eu); break;
    case 'f': draw_glyph_rows_scaled(x, y, color, scale, 0x06u, 0x08u, 0x1Eu, 0x08u, 0x08u, 0x08u, 0x08u); break;
    case 'g': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Fu, 0x11u, 0x0Fu, 0x01u, 0x0Eu); break;
    case 'h': draw_glyph_rows_scaled(x, y, color, scale, 0x10u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u); break;
    case 'i': draw_glyph_rows_scaled(x, y, color, scale, 0x04u, 0x00u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x0Eu); break;
    case 'j': draw_glyph_rows_scaled(x, y, color, scale, 0x02u, 0x00u, 0x06u, 0x02u, 0x02u, 0x12u, 0x0Cu); break;
    case 'k': draw_glyph_rows_scaled(x, y, color, scale, 0x10u, 0x10u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u); break;
    case 'l': draw_glyph_rows_scaled(x, y, color, scale, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu); break;
    case 'm': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x1Au, 0x15u, 0x15u, 0x15u, 0x15u); break;
    case 'n': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u); break;
    case 'o': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Eu, 0x11u, 0x11u, 0x11u, 0x0Eu); break;
    case 'p': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x1Eu, 0x11u, 0x1Eu, 0x10u, 0x10u); break;
    case 'q': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Fu, 0x11u, 0x0Fu, 0x01u, 0x01u); break;
    case 'r': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x16u, 0x19u, 0x10u, 0x10u, 0x10u); break;
    case 's': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x0Fu, 0x10u, 0x0Eu, 0x01u, 0x1Eu); break;
    case 't': draw_glyph_rows_scaled(x, y, color, scale, 0x08u, 0x08u, 0x1Eu, 0x08u, 0x08u, 0x09u, 0x06u); break;
    case 'u': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x11u, 0x11u, 0x11u, 0x13u, 0x0Du); break;
    case 'v': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u); break;
    case 'w': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x11u, 0x11u, 0x15u, 0x15u, 0x0Au); break;
    case 'x': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u); break;
    case 'y': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x0Eu); break;
    case 'z': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x1Fu, 0x02u, 0x04u, 0x08u, 0x1Fu); break;
    case ',': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x08u); break;
    case ';': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x00u, 0x0Cu, 0x08u); break;
    case '\'': draw_glyph_rows_scaled(x, y, color, scale, 0x0Cu, 0x04u, 0x08u, 0x00u, 0x00u, 0x00u, 0x00u); break;
    case '"': draw_glyph_rows_scaled(x, y, color, scale, 0x0Au, 0x0Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x00u); break;
    case '?': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x00u, 0x04u); break;
    case '!': draw_glyph_rows_scaled(x, y, color, scale, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x00u, 0x04u); break;
    case '_': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x1Fu); break;
    case '+': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x04u, 0x04u, 0x1Fu, 0x04u, 0x04u, 0x00u); break;
    case '=': draw_glyph_rows_scaled(x, y, color, scale, 0x00u, 0x00u, 0x1Fu, 0x00u, 0x1Fu, 0x00u, 0x00u); break;
    case '(': draw_glyph_rows_scaled(x, y, color, scale, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u, 0x04u, 0x02u); break;
    case ')': draw_glyph_rows_scaled(x, y, color, scale, 0x08u, 0x04u, 0x02u, 0x02u, 0x02u, 0x04u, 0x08u); break;
    case '&': draw_glyph_rows_scaled(x, y, color, scale, 0x0Cu, 0x12u, 0x14u, 0x08u, 0x15u, 0x12u, 0x0Du); break;
    case '%': draw_glyph_rows_scaled(x, y, color, scale, 0x18u, 0x19u, 0x02u, 0x04u, 0x08u, 0x13u, 0x03u); break;
    case '#': draw_glyph_rows_scaled(x, y, color, scale, 0x0Au, 0x0Au, 0x1Fu, 0x0Au, 0x1Fu, 0x0Au, 0x0Au); break;
    case '@': draw_glyph_rows_scaled(x, y, color, scale, 0x0Eu, 0x11u, 0x17u, 0x15u, 0x17u, 0x10u, 0x0Eu); break;
    default: break;
    }
}

static void draw_char(u32 x, u32 y, char ch, u32 color)
{
    draw_char_scaled(x, y, ch, color, gui_font_scale());
}

static void draw_text(u32 x, u32 y, const char *text, u32 color)
{
    u32 advance = gui_char_advance();
    for (u32 index = 0; text[index] != 0; index += 1) {
        draw_char(x + index * advance, y, text[index], color);
    }
}

static u16 read16(const u8 *ptr)
{
    return (u16) ptr[0] | ((u16) ptr[1] << 8);
}

static u32 read32(const u8 *ptr)
{
    return (u32) ptr[0] | ((u32) ptr[1] << 8) | ((u32) ptr[2] << 16) | ((u32) ptr[3] << 24);
}

static u8 visible_dirent(const u8 *entry)
{
    if (entry[0] == 0 || entry[0] == 0xE5u) {
        return 0;
    }
    if ((entry[11] & 0x1Eu) != 0) {
        return 0;
    }
    return 1;
}

static void format_name(const u8 *entry, char *out)
{
    u32 pos = 0;
    for (u32 i = 0; i < 8u; i += 1) {
        if (entry[i] != ' ') {
            out[pos++] = (char) entry[i];
        }
    }
    if (entry[8] != ' ') {
        out[pos++] = '.';
        for (u32 i = 0; i < 3u; i += 1) {
            if (entry[8u + i] != ' ') {
                out[pos++] = (char) entry[8u + i];
            }
        }
    }
    out[pos] = 0;
}

static void scan_root(void)
{
    const u8 *root = (const u8 *) g_boot->root_address;
    file_count = 0;
    for (u32 index = 0; index < g_boot->root_entries && file_count < MAX_FILES; index += 1) {
        const u8 *entry = root + index * ROOT_ENTRY_SIZE;
        if (entry[0] == 0) {
            break;
        }
        if (!visible_dirent(entry)) {
            continue;
        }
        files[file_count].dirent = entry;
        files[file_count].first_cluster = read16(entry + 26);
        files[file_count].size = read32(entry + 28);
        format_name(entry, files[file_count].name);
        file_count += 1;
    }
    if (selected_file >= file_count) {
        selected_file = 0;
    }
}

static u16 fat_next(u16 cluster)
{
    const u8 *fat = (const u8 *) g_boot->fat_address;
    u32 offset = cluster + (cluster / 2u);
    u16 value;
    if ((cluster & 1u) == 0) {
        value = (u16) fat[offset] | (((u16) fat[offset + 1u] & 0x0Fu) << 8);
    } else {
        value = (((u16) fat[offset] >> 4) | ((u16) fat[offset + 1u] << 4)) & 0x0FFFu;
    }
    return value;
}

static void copy_name(char *dst, const char *src)
{
    u32 index = 0;
    while (src[index] != 0 && index < 12u) {
        dst[index] = src[index];
        index += 1;
    }
    dst[index] = 0;
}

static u8 text_eq(const char *a, const char *b)
{
    for (u32 i = 0; i < 13u; i += 1u) {
        if (a[i] != b[i]) {
            return 0u;
        }
        if (a[i] == 0) {
            return 1u;
        }
    }
    return 1u;
}

static u8 find_loaded_file(const char *display_name, u32 *out_cluster, u32 *out_size)
{
    for (u32 i = 0; i < file_count; i += 1u) {
        if (text_eq(files[i].name, display_name)) {
            *out_cluster = files[i].first_cluster;
            *out_size = files[i].size;
            return 1u;
        }
    }
    return 0u;
}

static void restore_cursor(void);
static void draw_cursor_overlay(void);
static u32 user_fb_clip_limit_y(void);
static void user_fb_present_overlay(void);

/* --- ATA PIO (primary master) read-only driver -------------------------- */

#define ATA_DATA      0x1F0u
#define ATA_SECCOUNT  0x1F2u
#define ATA_LBA_LOW   0x1F3u
#define ATA_LBA_MID   0x1F4u
#define ATA_LBA_HIGH  0x1F5u
#define ATA_DRIVE     0x1F6u
#define ATA_STATUS    0x1F7u
#define ATA_COMMAND   0x1F7u
#define ATA_CONTROL   0x3F6u
#define ATA_SR_BSY    0x80u
#define ATA_SR_DRQ    0x08u
#define ATA_SR_ERR    0x01u

static u8 ata_wait_clear_busy(void)
{
    for (u32 tries = 0; tries < 2000000u; tries += 1) {
        u8 status = inb(ATA_STATUS);
        if (status == 0xFFu) {
            return 0;
        }
        if ((status & ATA_SR_BSY) == 0) {
            return 1;
        }
    }
    return 0;
}

static u8 ata_wait_drq(void)
{
    for (u32 tries = 0; tries < 2000000u; tries += 1) {
        u8 status = inb(ATA_STATUS);
        if (status == 0xFFu || (status & ATA_SR_ERR) != 0) {
            return 0;
        }
        if ((status & ATA_SR_DRQ) != 0) {
            return 1;
        }
    }
    return 0;
}

static void ata_detect(void)
{
    ata_present = 0;
    outb(ATA_DRIVE, 0xA0u);
    for (u32 i = 0; i < 4u; i += 1) {
        (void) inb(ATA_CONTROL);
    }

    u8 status = inb(ATA_STATUS);
    if (status == 0xFFu || status == 0x00u) {
        serial_print("LeonOS 32-bit ATA no disk\r\n");
        return;
    }

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, 0xECu);

    status = inb(ATA_STATUS);
    if (status == 0x00u) {
        serial_print("LeonOS 32-bit ATA no disk\r\n");
        return;
    }
    if (!ata_wait_clear_busy()) {
        serial_print("LeonOS 32-bit ATA no disk\r\n");
        return;
    }
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) {
        serial_print("LeonOS 32-bit ATA non-ATA device\r\n");
        return;
    }
    if (!ata_wait_drq()) {
        serial_print("LeonOS 32-bit ATA no disk\r\n");
        return;
    }
    for (u32 i = 0; i < 256u; i += 1) {
        (void) inw(ATA_DATA);
    }

    ata_present = 1;
    serial_print("LeonOS 32-bit ATA primary master detected\r\n");
}

static u8 ata_read_sector(u32 lba, u8 *dest)
{
    if (!ata_present) {
        return 0;
    }
    if (!ata_wait_clear_busy()) {
        return 0;
    }

    outb(ATA_DRIVE, (u8) (0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(ATA_SECCOUNT, 1u);
    outb(ATA_LBA_LOW, (u8) (lba & 0xFFu));
    outb(ATA_LBA_MID, (u8) ((lba >> 8) & 0xFFu));
    outb(ATA_LBA_HIGH, (u8) ((lba >> 16) & 0xFFu));
    outb(ATA_COMMAND, 0x20u);

    if (!ata_wait_clear_busy() || !ata_wait_drq()) {
        return 0;
    }
    for (u32 i = 0; i < 256u; i += 1) {
        u16 word = inw(ATA_DATA);
        dest[i * 2u] = (u8) (word & 0xFFu);
        dest[i * 2u + 1u] = (u8) ((word >> 8) & 0xFFu);
    }
    return 1;
}

static u8 ata_read_sectors(u32 lba, u8 *dest, u32 count)
{
    if (count == 0u) {
        return 1;
    }
    if (count == 1u) {
        return ata_read_sector(lba, dest);
    }
    if (!ata_present || count > 255u) {
        return 0;
    }
    if (!ata_wait_clear_busy()) {
        return 0;
    }

    outb(ATA_DRIVE, (u8) (0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(ATA_SECCOUNT, (u8) count);
    outb(ATA_LBA_LOW, (u8) (lba & 0xFFu));
    outb(ATA_LBA_MID, (u8) ((lba >> 8) & 0xFFu));
    outb(ATA_LBA_HIGH, (u8) ((lba >> 16) & 0xFFu));
    outb(ATA_COMMAND, 0x20u);

    for (u32 sector = 0; sector < count; sector += 1u) {
        if (!ata_wait_clear_busy() || !ata_wait_drq()) {
            return 0;
        }
        u8 *sector_dest = dest + sector * 512u;
        for (u32 i = 0; i < 256u; i += 1) {
            u16 word = inw(ATA_DATA);
            sector_dest[i * 2u] = (u8) (word & 0xFFu);
            sector_dest[i * 2u + 1u] = (u8) ((word >> 8) & 0xFFu);
        }
    }
    return 1;
}

static u8 ata_write_sector(u32 lba, const u8 *src)
{
    if (!ata_present) {
        return 0;
    }
    if (!ata_wait_clear_busy()) {
        return 0;
    }

    outb(ATA_DRIVE, (u8) (0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(ATA_SECCOUNT, 1u);
    outb(ATA_LBA_LOW, (u8) (lba & 0xFFu));
    outb(ATA_LBA_MID, (u8) ((lba >> 8) & 0xFFu));
    outb(ATA_LBA_HIGH, (u8) ((lba >> 16) & 0xFFu));
    outb(ATA_COMMAND, 0x30u);

    if (!ata_wait_clear_busy() || !ata_wait_drq()) {
        return 0;
    }
    for (u32 i = 0; i < 256u; i += 1) {
        u16 word = (u16) ((u16) src[i * 2u] | ((u16) src[i * 2u + 1u] << 8));
        outw(ATA_DATA, word);
    }

    if (!ata_wait_clear_busy()) {
        return 0;
    }
    outb(ATA_COMMAND, 0xE7u);
    if (!ata_wait_clear_busy()) {
        return 0;
    }
    return 1;
}

/* --- read-only FAT32 ----------------------------------------------------- */

static u32 fat32_cluster_lba(u32 cluster)
{
    return fat32_first_data_sector + (cluster - 2u) * fat32_sectors_per_cluster;
}

static u32 fat32_next_cluster(u32 cluster)
{
    u32 fat_offset = cluster * 4u;
    u32 fat_sector = fat32_first_fat_sector + (fat_offset / 512u);
    u32 entry_offset = fat_offset % 512u;

    if (fat32_fat_cache_sector != fat_sector) {
        if (!ata_read_sector(fat_sector, fat32_fat_cache)) {
            return 0x0FFFFFFFu;
        }
        fat32_fat_cache_sector = fat_sector;
    }
    return read32(fat32_fat_cache + entry_offset) & 0x0FFFFFFFu;
}

static u8 fat32_mount(void)
{
    if (!ata_present) {
        return 0;
    }

    u32 part_lba = 0;
    if (!ata_read_sector(0, fat32_data_buf)) {
        return 0;
    }
    if (fat32_data_buf[510] == 0x55u && fat32_data_buf[511] == 0xAAu) {
        const u8 *pe = fat32_data_buf + 446u;
        u8 type = pe[4];
        u32 start = read32(pe + 8);
        if ((type == 0x0Bu || type == 0x0Cu) && start != 0) {
            part_lba = start;
        }
    }

    if (!ata_read_sector(part_lba, fat32_data_buf)) {
        return 0;
    }
    if (fat32_data_buf[510] != 0x55u || fat32_data_buf[511] != 0xAAu) {
        return 0;
    }

    u16 bytes_per_sector = read16(fat32_data_buf + 0x0Bu);
    u8 sectors_per_cluster = fat32_data_buf[0x0Du];
    u16 reserved = read16(fat32_data_buf + 0x0Eu);
    u8 num_fats = fat32_data_buf[0x10u];
    u16 root_entries16 = read16(fat32_data_buf + 0x11u);
    u16 fat_size16 = read16(fat32_data_buf + 0x16u);
    u32 fat_size32 = read32(fat32_data_buf + 0x24u);
    u32 root_cluster = read32(fat32_data_buf + 0x2Cu);

    if (bytes_per_sector != 512u || sectors_per_cluster == 0 || num_fats == 0) {
        return 0;
    }
    if (root_entries16 != 0 || fat_size16 != 0 || fat_size32 == 0) {
        return 0;
    }

    fat32_partition_lba = part_lba;
    fat32_sectors_per_cluster = sectors_per_cluster;
    fat32_num_fats = num_fats;
    fat32_fat_size = fat_size32;
    fat32_first_fat_sector = part_lba + reserved;
    fat32_first_data_sector = part_lba + reserved + (u32) num_fats * fat_size32;
    fat32_root_cluster = root_cluster;
    fat32_fat_cache_sector = 0xFFFFFFFFu;
    fat32_present = 1;
    return 1;
}

static void scan_root_fat32(void)
{
    file_count = 0;
    u32 cluster = fat32_root_cluster;
    u32 guard = 0;
    u8 done = 0;

    while (!done && cluster >= 2u && cluster < 0x0FFFFFF8u &&
           file_count < MAX_FILES && guard < 65536u) {
        guard += 1;
        u32 base_lba = fat32_cluster_lba(cluster);
        for (u32 s = 0; s < fat32_sectors_per_cluster && !done && file_count < MAX_FILES; s += 1) {
            if (!ata_read_sector(base_lba + s, fat32_data_buf)) {
                done = 1;
                break;
            }
            for (u32 e = 0; e < 16u && file_count < MAX_FILES; e += 1) {
                const u8 *entry = fat32_data_buf + e * ROOT_ENTRY_SIZE;
                if (entry[0] == 0) {
                    done = 1;
                    break;
                }
                if (entry[0] == 0xE5u) {
                    continue;
                }
                if ((entry[11] & 0x0Fu) == 0x0Fu) {
                    continue;
                }
                if ((entry[11] & 0x08u) != 0 || (entry[11] & 0x10u) != 0) {
                    continue;
                }
                files[file_count].dirent = 0;
                files[file_count].first_cluster =
                    ((u32) read16(entry + 20) << 16) | (u32) read16(entry + 26);
                files[file_count].size = read32(entry + 28);
                format_name(entry, files[file_count].name);
                file_count += 1;
            }
        }
        if (!done) {
            cluster = fat32_next_cluster(cluster);
        }
    }

    if (selected_file >= file_count) {
        selected_file = 0;
    }
}

static void open_selected_file_fat32(void)
{
    struct FileEntry *entry = &files[selected_file];
    u32 cluster = entry->first_cluster;
    u32 copied = 0;
    u32 guard = 0;

    while (cluster >= 2u && cluster < 0x0FFFFFF8u &&
           copied < FILE_BUFFER_SIZE && copied < entry->size && guard < 65536u) {
        guard += 1;
        u32 base_lba = fat32_cluster_lba(cluster);
        for (u32 s = 0; s < fat32_sectors_per_cluster &&
             copied < FILE_BUFFER_SIZE && copied < entry->size; s += 1) {
            if (!ata_read_sector(base_lba + s, fat32_data_buf)) {
                cluster = 0x0FFFFFFFu;
                break;
            }
            for (u32 i = 0; i < 512u && copied < FILE_BUFFER_SIZE && copied < entry->size; i += 1) {
                file_buffer[copied++] = fat32_data_buf[i];
            }
        }
        if (copied >= entry->size || cluster >= 0x0FFFFFF8u) {
            break;
        }
        cluster = fat32_next_cluster(cluster);
    }

    file_buffer_size = copied;
    file_loaded = 1;
    copy_name(open_name, entry->name);
    serial_print("LeonOS 32-bit FAT32 file opened ");
    serial_print(open_name);
    serial_print(" bytes=");
    serial_print_dec(copied);
    serial_write('\r');
    serial_write('\n');
    dirty = 1;
}

/* --- minimal read/write FAT32 (single small 8.3 root file) -------------- */

static u8 fat32_set_fat_entry(u32 cluster, u32 value)
{
    u32 fat_offset = cluster * 4u;
    u32 rel_sector = fat_offset / 512u;
    u32 entry_offset = fat_offset % 512u;

    for (u32 copy = 0; copy < fat32_num_fats; copy += 1) {
        u32 sector = fat32_first_fat_sector + copy * fat32_fat_size + rel_sector;
        if (!ata_read_sector(sector, fat32_io_buf)) {
            return 0;
        }
        u32 existing = read32(fat32_io_buf + entry_offset);
        u32 updated = (existing & 0xF0000000u) | (value & 0x0FFFFFFFu);
        write32(fat32_io_buf + entry_offset, updated);
        if (!ata_write_sector(sector, fat32_io_buf)) {
            return 0;
        }
    }
    fat32_fat_cache_sector = 0xFFFFFFFFu;
    return 1;
}

static u32 fat32_alloc_cluster(void)
{
    u32 max_clusters = fat32_fat_size * 128u;
    for (u32 cluster = 3u; cluster < max_clusters; cluster += 1) {
        if (fat32_next_cluster(cluster) == 0) {
            return cluster;
        }
    }
    return 0;
}

static u8 fat32_name_match(const u8 *entry, const char *name11)
{
    for (u32 k = 0; k < 11u; k += 1) {
        if (entry[k] != (u8) name11[k]) {
            return 0;
        }
    }
    return 1;
}

static u8 fat32_write_root_file(const char *name11, const u8 *data, u32 len)
{
    if (!fat32_present || len > FAT32_WRITE_MAX_BYTES) {
        return 0;
    }

    u32 entry_lba = 0;
    u32 entry_off = 0;
    u8 have_slot = 0;
    u8 overwrite = 0;
    u32 existing_cluster = 0;
    u32 cluster = fat32_root_cluster;
    u32 guard = 0;
    u8 done = 0;

    while (!done && cluster >= 2u && cluster < 0x0FFFFFF8u && guard < 65536u) {
        guard += 1;
        u32 base_lba = fat32_cluster_lba(cluster);
        for (u32 s = 0; s < fat32_sectors_per_cluster && !done; s += 1) {
            u32 lba = base_lba + s;
            if (!ata_read_sector(lba, fat32_io_buf)) {
                return 0;
            }
            for (u32 e = 0; e < 16u; e += 1) {
                u8 *entry = fat32_io_buf + e * ROOT_ENTRY_SIZE;
                if (entry[0] == 0) {
                    if (!have_slot) {
                        entry_lba = lba;
                        entry_off = e * ROOT_ENTRY_SIZE;
                        have_slot = 1;
                    }
                    done = 1;
                    break;
                }
                if (entry[0] == 0xE5u) {
                    if (!have_slot) {
                        entry_lba = lba;
                        entry_off = e * ROOT_ENTRY_SIZE;
                        have_slot = 1;
                    }
                    continue;
                }
                if ((entry[11] & 0x0Fu) == 0x0Fu) {
                    continue;
                }
                if (fat32_name_match(entry, name11)) {
                    entry_lba = lba;
                    entry_off = e * ROOT_ENTRY_SIZE;
                    have_slot = 1;
                    overwrite = 1;
                    existing_cluster = ((u32) read16(entry + 20) << 16) | (u32) read16(entry + 26);
                    done = 1;
                    break;
                }
            }
        }
        if (!done) {
            cluster = fat32_next_cluster(cluster);
        }
    }

    if (!have_slot) {
        return 0;
    }

    u32 file_cluster;
    if (overwrite && existing_cluster >= 2u && existing_cluster < 0x0FFFFFF8u) {
        file_cluster = existing_cluster;
    } else {
        file_cluster = fat32_alloc_cluster();
        if (file_cluster == 0) {
            return 0;
        }
    }

    if (!fat32_set_fat_entry(file_cluster, 0x0FFFFFFFu)) {
        return 0;
    }

    for (u32 i = 0; i < 512u; i += 1) {
        fat32_io_buf[i] = 0;
    }
    for (u32 i = 0; i < len; i += 1) {
        fat32_io_buf[i] = data[i];
    }
    if (!ata_write_sector(fat32_cluster_lba(file_cluster), fat32_io_buf)) {
        return 0;
    }

    if (!ata_read_sector(entry_lba, fat32_io_buf)) {
        return 0;
    }
    u8 *entry = fat32_io_buf + entry_off;
    for (u32 k = 0; k < 11u; k += 1) {
        entry[k] = (u8) name11[k];
    }
    entry[11] = 0x20u;
    entry[12] = 0;
    entry[13] = 0;
    write16(entry + 14, 0);
    write16(entry + 16, (u16) FAT_FIXED_DATE);
    write16(entry + 18, (u16) FAT_FIXED_DATE);
    write16(entry + 20, (u16) ((file_cluster >> 16) & 0xFFFFu));
    write16(entry + 22, 0);
    write16(entry + 24, (u16) FAT_FIXED_DATE);
    write16(entry + 26, (u16) (file_cluster & 0xFFFFu));
    write32(entry + 28, len);
    if (!ata_write_sector(entry_lba, fat32_io_buf)) {
        return 0;
    }

    return 1;
}

static u8 fat32_find_file_raw(const char *name11, u32 *out_cluster, u32 *out_size)
{
    u32 cluster = fat32_root_cluster;
    u32 guard = 0;
    u8 done = 0;

    while (!done && cluster >= 2u && cluster < 0x0FFFFFF8u && guard < 65536u) {
        guard += 1;
        u32 base_lba = fat32_cluster_lba(cluster);
        for (u32 s = 0; s < fat32_sectors_per_cluster && !done; s += 1) {
            if (!ata_read_sector(base_lba + s, fat32_rb_buf)) {
                return 0;
            }
            for (u32 e = 0; e < 16u; e += 1) {
                const u8 *entry = fat32_rb_buf + e * ROOT_ENTRY_SIZE;
                if (entry[0] == 0) {
                    done = 1;
                    break;
                }
                if (entry[0] == 0xE5u || (entry[11] & 0x0Fu) == 0x0Fu) {
                    continue;
                }
                if (fat32_name_match(entry, name11)) {
                    *out_cluster = ((u32) read16(entry + 20) << 16) | (u32) read16(entry + 26);
                    *out_size = read32(entry + 28);
                    return 1;
                }
            }
        }
        if (!done) {
            cluster = fat32_next_cluster(cluster);
        }
    }
    return 0;
}

static u32 fat32_read_file_bytes(u32 start_cluster, u32 file_size, u8 *dest, u32 max_bytes)
{
    u32 cluster = start_cluster;
    u32 copied = 0;
    u32 guard = 0;
    u32 target = file_size < max_bytes ? file_size : max_bytes;

    if (fat32_sectors_per_cluster == 1u) {
        while (cluster >= 2u && cluster < 0x0FFFFFF8u &&
               copied < target && guard < 65536u) {
            guard += 1u;
            u32 remaining = target - copied;
            u32 full_sectors = remaining / 512u;
            if (full_sectors != 0u) {
                u32 run = 1u;
                u32 last_cluster = cluster;
                while (run < full_sectors && run < 255u) {
                    u32 next = fat32_next_cluster(last_cluster);
                    if (next != last_cluster + 1u ||
                        next >= 0x0FFFFFF8u) {
                        break;
                    }
                    last_cluster = next;
                    run += 1u;
                }

                if (!ata_read_sectors(fat32_cluster_lba(cluster),
                                      dest + copied, run)) {
                    return copied;
                }
                copied += run * 512u;
                cluster = fat32_next_cluster(last_cluster);
                continue;
            }

            if (!ata_read_sector(fat32_cluster_lba(cluster), fat32_data_buf)) {
                return copied;
            }
            for (u32 i = 0; i < 512u && copied < target; i += 1u) {
                dest[copied++] = fat32_data_buf[i];
            }
            break;
        }
        return copied;
    }

    while (cluster >= 2u && cluster < 0x0FFFFFF8u &&
           copied < target && guard < 65536u) {
        guard += 1;
        u32 base_lba = fat32_cluster_lba(cluster);
        u32 s = 0;
        while (s < fat32_sectors_per_cluster && copied < target) {
            u32 remaining = target - copied;
            u32 full_sectors = remaining / 512u;
            u32 run = fat32_sectors_per_cluster - s;
            if (run > 255u) {
                run = 255u;
            }
            if (run > full_sectors) {
                run = full_sectors;
            }

            if (run != 0u) {
                if (!ata_read_sectors(base_lba + s, dest + copied, run)) {
                    return copied;
                }
                copied += run * 512u;
                s += run;
                continue;
            }

            if (!ata_read_sector(base_lba + s, fat32_data_buf)) {
                return copied;
            }
            for (u32 i = 0; i < 512u && copied < target; i += 1u) {
                dest[copied++] = fat32_data_buf[i];
            }
            s += 1u;
        }
        if (copied >= target || cluster >= 0x0FFFFFF8u) {
            break;
        }
        cluster = fat32_next_cluster(cluster);
    }

    return copied;
}

static const char fat32_write_name[11] = {
    'W', 'R', 'I', 'T', 'E', '3', '2', ' ', 'T', 'X', 'T'
};
static const char fat32_write_payload[] = "LeonOS FAT32 write OK 2026\n";

static void fat32_write_test(void)
{
    if (!using_fat32) {
        return;
    }

    u32 len = (u32) (sizeof(fat32_write_payload) - 1u);

    /* First create, then overwrite in place, to exercise both paths. */
    if (!fat32_write_root_file(fat32_write_name, (const u8 *) "FIRST", 5u)) {
        serial_print("LeonOS 32-bit FAT32 write FAILED\r\n");
        return;
    }
    if (!fat32_write_root_file(fat32_write_name, (const u8 *) fat32_write_payload, len)) {
        serial_print("LeonOS 32-bit FAT32 write FAILED\r\n");
        return;
    }
    serial_print(msg_fat32_write_prefix);
    serial_print_dec(len);
    serial_write('\r');
    serial_write('\n');

    u32 found_cluster = 0;
    u32 found_size = 0;
    if (!fat32_find_file_raw(fat32_write_name, &found_cluster, &found_size)) {
        serial_print("LeonOS 32-bit FAT32 readback FAILED not found\r\n");
        return;
    }
    if (found_size != len) {
        serial_print("LeonOS 32-bit FAT32 readback MISMATCH size\r\n");
        return;
    }
    if (!ata_read_sector(fat32_cluster_lba(found_cluster), fat32_rb_buf)) {
        serial_print("LeonOS 32-bit FAT32 readback FAILED read\r\n");
        return;
    }
    for (u32 i = 0; i < len; i += 1) {
        if (fat32_rb_buf[i] != (u8) fat32_write_payload[i]) {
            serial_print("LeonOS 32-bit FAT32 readback MISMATCH data\r\n");
            return;
        }
    }

    serial_print(msg_fat32_readback_prefix);
    serial_print_dec(found_size);
    serial_write('\r');
    serial_write('\n');

    scan_root_fat32();
    dirty = 1;
}

/* --- LEO1 ring-0 cooperative flat app loader ---------------------------- */
/*
 * This is NOT user mode. The loader copies a tiny flat binary into a kernel
 * page and calls its entry point directly in ring 0 with no isolation. The
 * kernel passes a fixed in-kernel API table (see struct LeoAppApi) by register
 * contract; the app calls those function pointers directly. There is no
 * syscall boundary, no separate address space, and no process model.
 */

struct LeoAppApi {
    u32 abi_version;
    void (*serial_print)(const char *text);
    void (*app_print)(const char *text);
};

static struct LeoAppApi leo_api;
static const char leo_app_name[11] = {
    'H', 'E', 'L', 'L', 'O', 'A', 'P', 'P', 'L', 'E', 'O'
};
static const char leo_app_display[] = "HELLOAPP.LEO";

static void leo_app_print(const char *text)
{
    u32 i = 0;
    for (; text[i] != 0 && i < sizeof(app_message) - 1u; i += 1) {
        app_message[i] = text[i];
    }
    app_message[i] = 0;
    app_message_set = 1;
    dirty = 1;
}

static void leo_run_app(void)
{
    if (!using_fat32) {
        return;
    }

    u32 cluster = 0;
    u32 size = 0;
    if (!find_loaded_file(leo_app_display, &cluster, &size) &&
        !fat32_find_file_raw(leo_app_name, &cluster, &size)) {
        serial_print("LeonOS LEO1 app not found\r\n");
        return;
    }
    if (size < 32u || size > 512u) {
        serial_print("LeonOS LEO1 app bad size\r\n");
        return;
    }
    if (!ata_read_sector(fat32_cluster_lba(cluster), fat32_rb_buf)) {
        serial_print("LeonOS LEO1 app read failed\r\n");
        return;
    }

    if (fat32_rb_buf[0] != 'L' || fat32_rb_buf[1] != 'E' ||
        fat32_rb_buf[2] != 'O' || fat32_rb_buf[3] != '1') {
        serial_print("LeonOS LEO1 app bad magic\r\n");
        return;
    }
    u32 version = read32(fat32_rb_buf + 4);
    u32 entry_offset = read32(fat32_rb_buf + 8);
    u32 image_size = read32(fat32_rb_buf + 12);
    u32 bss_size = read32(fat32_rb_buf + 16);

    if (version != 1u) {
        serial_print("LeonOS LEO1 app bad version\r\n");
        return;
    }
    if (image_size < 32u || image_size > size || image_size > 512u) {
        serial_print("LeonOS LEO1 app bad image size\r\n");
        return;
    }
    if (entry_offset < 32u || entry_offset >= image_size) {
        serial_print("LeonOS LEO1 app bad entry\r\n");
        return;
    }
    if (image_size + bss_size > PAGE_SIZE) {
        serial_print("LeonOS LEO1 app too large\r\n");
        return;
    }
    if (!pmm_ready) {
        serial_print("LeonOS LEO1 app no allocator\r\n");
        return;
    }

    u32 base = pmm_alloc_page();
    u8 *dest = (u8 *) base;
    for (u32 i = 0; i < image_size; i += 1) {
        dest[i] = fat32_rb_buf[i];
    }
    for (u32 i = image_size; i < PAGE_SIZE; i += 1) {
        dest[i] = 0;
    }

    serial_print(msg_leo_loaded_prefix);
    serial_print_hex(base);
    serial_print(msg_user_entry_prefix);
    serial_print_hex(entry_offset);
    serial_write('\r');
    serial_write('\n');

    leo_api.abi_version = 1u;
    leo_api.serial_print = serial_print;
    leo_api.app_print = leo_app_print;

    u32 entry_addr = base + entry_offset;
    u32 entry_eax;
    __asm__ volatile (
        "call *%[entry]\n\t"
        : "=a"(entry_eax)
        : "0"(&leo_api), "b"(base), [entry] "r"(entry_addr)
        : "ecx", "edx", "memory", "cc");
    (void) entry_eax;

    serial_print("LeonOS LEO1 app returned\r\n");
    pmm_free_page(base);
    dirty = 1;
}

/* --- LEO1 v2 ring-3 user app + int 0x80 syscall path -------------------- */
/*
 * Unlike leo_run_app (ring 0), this loads a LEO1 *version 2* image, marks its
 * code/stack pages user-accessible, and enters it at ring 3 via iret. The app
 * gets NO kernel function pointers; its only way to talk to the kernel is the
 * int 0x80 syscall gate. This is a real privilege boundary, but it is still a
 * single, cooperative, blocking call - there is no scheduler/process model.
 */

static const char leo_user_name[11] = {
    'U', 'H', 'E', 'L', 'L', 'O', ' ', ' ', 'L', 'E', 'O'
};

static const char leo_ugfx_name[11] = {
    'U', 'G', 'F', 'X', ' ', ' ', ' ', ' ', 'L', 'E', 'O'
};

static const char leo_ucdemo_name[11] = {
    'U', 'C', 'D', 'E', 'M', 'O', ' ', ' ', 'L', 'E', 'O'
};

static const char leo_ubrowser_name[11] = {
    'U', 'B', 'R', 'O', 'W', 'S', 'E', 'R', ' ', 'L', 'E'
};

static const char leo_unetrun_name[11] = {
    'U', 'N', 'E', 'T', 'R', 'U', 'N', ' ', ' ', 'L', 'E'
};

static const char leo_uweb_name[11] = {
    'U', 'W', 'E', 'B', ' ', ' ', ' ', ' ', ' ', 'L', 'E'
};

static const char leo_ustream_name[11] = {
    'U', 'S', 'T', 'R', 'E', 'A', 'M', ' ', 'L', 'E', 'O'
};

static const char leo_netsurf_name[11] = {
    'N', 'E', 'T', 'S', 'U', 'R', 'F', ' ', 'L', 'E', 'O'
};

static const char leo_uqjs_name[11] = {
    'U', 'Q', 'J', 'S', ' ', ' ', ' ', ' ', 'L', 'E', 'O'
};

static u8 user_fb_info_reported;
static u8 user_fb_fill_reported;
static u8 user_fb_present_reported;
static u8 user_event_poll_reported;
static u8 user_fb_blit_reported;

static u32 user_range_max(u32 user_ptr)
{
    if (user_app_base != 0u && user_ptr >= user_app_base &&
        user_ptr < user_app_limit) {
        return user_app_limit - user_ptr;
    }
    if (user_stack_base != 0u && user_ptr >= user_stack_base &&
        user_ptr < user_stack_limit) {
        return user_stack_limit - user_ptr;
    }
    if (user_heap_base != 0u && user_ptr >= user_heap_base &&
        user_ptr < user_heap_limit) {
        return user_heap_limit - user_ptr;
    }
    return 0u;
}

static u8 user_range_valid(u32 user_ptr, u32 bytes)
{
    u32 max = user_range_max(user_ptr);
    if (max == 0u || bytes == 0u) {
        return 0u;
    }
    if (bytes <= max) {
        return 1u;
    }
    return 0u;
}

/* Print a NUL-terminated string supplied by ring 3. The pointer is validated
 * to lie inside the loaded user image/stack range and the scan is
 * length-bounded so a bad/unterminated string cannot walk off into kernel
 * memory. */
static void user_syscall_write(u32 user_ptr)
{
    if (!user_range_valid(user_ptr, 1u)) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return;
    }
    const char *p = (const char *) user_ptr;
    u32 max = user_range_max(user_ptr);
    u32 ai = 0;
    for (u32 i = 0; i < max && p[i] != 0; i += 1) {
        char c = p[i];
        serial_write(c);
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 32);
        }
        if (c != '\r' && c != '\n' && ai < sizeof(app_message) - 1u) {
            app_message[ai] = c;
            ai += 1;
        }
    }
    app_message[ai] = 0;
    app_message_set = 1;
    dirty = 1;
}

static u8 user_syscall_fb_info(u32 user_ptr)
{
    if (!user_range_valid(user_ptr, 16u)) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }

    u32 *out = (u32 *) user_ptr;
    out[0] = g_boot->framebuffer.width;
    out[1] = g_boot->framebuffer.height;
    out[2] = g_boot->framebuffer.pitch;
    out[3] = 32u;

    if (!user_fb_info_reported) {
        user_fb_info_reported = 1u;
        serial_print(msg_user_fb_info);
        serial_print_dec(g_boot->framebuffer.width);
        serial_write('x');
        serial_print_dec(g_boot->framebuffer.height);
        serial_write('\r');
        serial_write('\n');
    }
    return 1u;
}

static u8 user_syscall_fb_fill(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    if (g_boot->framebuffer.address == 0 || w == 0u || h == 0u) {
        return 0u;
    }
    u32 limit_y = user_fb_clip_limit_y();
    if (y >= limit_y) {
        return 1u;
    }
    if (h > limit_y - y) {
        h = limit_y - y;
    }
    if (h == 0u) {
        return 1u;
    }

    volatile u32 *old_pixels = draw_pixels_override;
    u32 old_stride = draw_stride_override;
    if (framebuffer_back_ready) {
        draw_pixels_override = framebuffer_back;
        draw_stride_override = framebuffer_back_stride;
    }
    fill_rect(x, y, w, h, color);
    draw_pixels_override = old_pixels;
    draw_stride_override = old_stride;

    if (!user_fb_fill_reported) {
        user_fb_fill_reported = 1u;
        serial_print_line(msg_user_fb_fill);
    }
    return 1u;
}

static u8 user_syscall_fb_present(void)
{
    if (!framebuffer_back_ready) {
        return 0u;
    }
    user_fb_overlay_active = 1u;
    dirty = 0u;
    user_fb_present_overlay();
    if (!user_fb_present_reported) {
        user_fb_present_reported = 1u;
        serial_print_line(msg_user_fb_present);
    }
    return 1u;
}

static u8 user_syscall_fb_text(u32 x, u32 y, u32 text_ptr, u32 color)
{
    char text[256];
    u32 limit;
    u32 len = 0u;

    if (!user_range_valid(text_ptr, 1u)) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }

    limit = user_range_max(text_ptr);
    while (len + 1u < sizeof(text) && len < limit) {
        char ch = ((const char *) text_ptr)[len];
        text[len] = ch;
        len += 1u;
        if (ch == 0) {
            break;
        }
    }
    if (len == 0u || text[len - 1u] != 0) {
        if (len < sizeof(text)) {
            text[len] = 0;
        } else {
            text[sizeof(text) - 1u] = 0;
        }
    }
    u32 limit_y = user_fb_clip_limit_y();
    if (y >= limit_y || gui_line_height() > limit_y - y) {
        return 1u;
    }

    volatile u32 *old_pixels = draw_pixels_override;
    u32 old_stride = draw_stride_override;
    if (framebuffer_back_ready) {
        draw_pixels_override = framebuffer_back;
        draw_stride_override = framebuffer_back_stride;
    }
    draw_text(x, y, text, color);
    draw_pixels_override = old_pixels;
    draw_stride_override = old_stride;
    return 1u;
}

static u8 user_syscall_fb_blit(u32 desc_ptr)
{
    if (!user_range_valid(desc_ptr, 36u) ||
        g_boot->framebuffer.address == 0 ||
        !framebuffer_back_ready) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }

    const u32 *desc = (const u32 *) desc_ptr;
    u32 dst_x = desc[0];
    u32 dst_y = desc[1];
    u32 width = desc[2];
    u32 height = desc[3];
    u32 src_width = desc[4];
    u32 src_height = desc[5];
    u32 src_stride = desc[6];
    u32 pixels_ptr = desc[7];
    u32 flags = desc[8];
    u32 src_x = 0u;
    u32 src_y = 0u;
    u32 src_clip_width = src_width;
    u32 src_clip_height = src_height;

    if (user_range_valid(desc_ptr, 52u)) {
        src_x = desc[9];
        src_y = desc[10];
        src_clip_width = desc[11];
        src_clip_height = desc[12];
    }

    if (width == 0u || height == 0u || src_width == 0u ||
        src_height == 0u || src_stride < src_width * 4u ||
        pixels_ptr == 0u) {
        return 0u;
    }
    if (!user_range_valid(pixels_ptr, 1u)) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }
    u32 max_pixels = user_range_max(pixels_ptr);
    u32 needed = src_stride * (src_height - 1u) + src_width * 4u;
    if (needed < src_width * 4u || needed > max_pixels) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }
    if (src_x >= src_width || src_y >= src_height) {
        return 1u;
    }
    if (src_clip_width == 0u || src_clip_width > src_width - src_x) {
        src_clip_width = src_width - src_x;
    }
    if (src_clip_height == 0u || src_clip_height > src_height - src_y) {
        src_clip_height = src_height - src_y;
    }

    if (dst_x >= g_boot->framebuffer.width ||
        dst_y >= g_boot->framebuffer.height) {
        return 1u;
    }
    if (width > g_boot->framebuffer.width - dst_x) {
        width = g_boot->framebuffer.width - dst_x;
    }
    if (height > g_boot->framebuffer.height - dst_y) {
        height = g_boot->framebuffer.height - dst_y;
    }
    u32 limit_y = user_fb_clip_limit_y();
    if (dst_y >= limit_y) {
        return 1u;
    }
    if (height > limit_y - dst_y) {
        height = limit_y - dst_y;
    }
    if (height == 0u) {
        return 1u;
    }

    volatile u32 *old_pixels = draw_pixels_override;
    u32 old_stride = draw_stride_override;
    draw_pixels_override = framebuffer_back;
    draw_stride_override = framebuffer_back_stride;

    const u8 *src = (const u8 *) pixels_ptr;
    for (u32 y = 0u; y < height; y += 1u) {
        u32 syi = (height == src_clip_height) ? y :
                  (y * src_clip_height) / height;
        if (syi >= src_clip_height) {
            syi = src_clip_height - 1u;
        }
        syi += src_y;
        for (u32 x = 0u; x < width; x += 1u) {
            u32 sxi = (width == src_clip_width) ? x :
                      (x * src_clip_width) / width;
            if (sxi >= src_clip_width) {
                sxi = src_clip_width - 1u;
            }
            sxi += src_x;
            const u8 *p = src + syi * src_stride + sxi * 4u;
            u32 r = p[0];
            u32 g = p[1];
            u32 b = p[2];
            u32 a = p[3];
            u32 color;
            if ((flags & 1u) != 0u || a >= 255u) {
                color = (r << 16) | (g << 8) | b;
            } else if (a == 0u) {
                continue;
            } else {
                u32 dst = read_pixel((i32) (dst_x + x), (i32) (dst_y + y));
                u32 dr = (dst >> 16) & 0xFFu;
                u32 dg = (dst >> 8) & 0xFFu;
                u32 db = dst & 0xFFu;
                u32 inv = 255u - a;
                r = (r * a + dr * inv + 127u) / 255u;
                g = (g * a + dg * inv + 127u) / 255u;
                b = (b * a + db * inv + 127u) / 255u;
                color = (r << 16) | (g << 8) | b;
            }
            write_pixel((i32) (dst_x + x), (i32) (dst_y + y), color);
        }
    }

    draw_pixels_override = old_pixels;
    draw_stride_override = old_stride;
    if (!user_fb_blit_reported) {
        user_fb_blit_reported = 1u;
        serial_print_line(msg_user_fb_blit);
    }
    return 1u;
}

static u8 user_syscall_event_poll(u32 user_ptr)
{
    if (!user_range_valid(user_ptr, 12u)) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }

    struct KernelEvent event;
    if (!event_pop(&event)) {
        return 0u;
    }

    u32 *out = (u32 *) user_ptr;
    out[0] = event.type;
    out[1] = event.data0;
    out[2] = event.data1;

    if (!user_event_poll_reported) {
        user_event_poll_reported = 1u;
        serial_print_line(msg_user_event_poll);
    }
    return event.type;
}

static u8 user_url_starts_https(const char *url)
{
    return url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
           url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/';
}

static u8 user_syscall_browser_open(u32 user_ptr)
{
    if (!user_range_valid(user_ptr, 1u)) {
        serial_print("LeonOS user syscall bad ptr\r\n");
        return 0u;
    }
    const char *url = (const char *) user_ptr;
    u32 max = user_range_max(user_ptr);
    u32 i = 0u;
    while (i < USER_BROWSER_URL_MAX - 1u && i < max && url[i] != 0) {
        pending_user_browser_url[i] = url[i];
        i += 1u;
    }
    pending_user_browser_url[i] = 0;
    if (i < 12u || !user_url_starts_https(url)) {
        serial_print("LeonOS user browser open rejected\r\n");
        return 0u;
    }
    pending_user_browser_open = 1u;
    serial_print("LeonOS user browser open queued ");
    serial_print(pending_user_browser_url);
    serial_print("\r\n");
    return 1u;
}

static u8 user_syscall_yield(void)
{
    scheduler_run_once();
    if (framebuffer_back_ready) {
        if (user_fb_overlay_active) {
            user_fb_present_overlay();
        } else {
            framebuffer_present();
        }
    }
    if (!user_fb_overlay_active) {
        dirty = 1;
    }
    return 1u;
}

static u32 user_syscall_millis(void)
{
    return system_ticks * PIT_MS_PER_TICK;
}

#define USER_HEAP_BLOCK_MAGIC 0x4C48424Bu
#define USER_HEAP_BLOCK_HEADER_SIZE 16u

struct UserHeapBlock {
    u32 size;
    u32 next;
    u32 free;
    u32 magic;
};

static u8 user_heap_block_valid(u32 address)
{
    if (address < user_heap_base ||
        address + USER_HEAP_BLOCK_HEADER_SIZE < address ||
        address + USER_HEAP_BLOCK_HEADER_SIZE > user_heap_next) {
        return 0u;
    }
    const struct UserHeapBlock *block =
        (const struct UserHeapBlock *) address;
    return block->magic == USER_HEAP_BLOCK_MAGIC &&
           block->size <= user_heap_limit - address -
                          USER_HEAP_BLOCK_HEADER_SIZE;
}

static u32 user_syscall_malloc(u32 size)
{
    if (size == 0u || user_heap_base == 0u) {
        return 0u;
    }
    if (size > user_heap_limit - user_heap_base -
               USER_HEAP_BLOCK_HEADER_SIZE - 7u) {
        return 0u;
    }
    size = align_up(size, 8u);
    u32 block_address = user_heap_first;
    u32 last_address = 0u;
    u32 guard = 0u;
    while (block_address != 0u && guard < 1048576u) {
        if (!user_heap_block_valid(block_address)) {
            serial_print("LeonOS user heap corrupt\r\n");
            return 0u;
        }
        struct UserHeapBlock *block = (struct UserHeapBlock *) block_address;
        if (block->free && block->size >= size) {
            u32 remaining = block->size - size;
            if (remaining >= USER_HEAP_BLOCK_HEADER_SIZE + 8u) {
                u32 split_address = block_address +
                        USER_HEAP_BLOCK_HEADER_SIZE + size;
                struct UserHeapBlock *split =
                        (struct UserHeapBlock *) split_address;
                split->size = remaining - USER_HEAP_BLOCK_HEADER_SIZE;
                split->next = block->next;
                split->free = 1u;
                split->magic = USER_HEAP_BLOCK_MAGIC;
                block->size = size;
                block->next = split_address;
            }
            block->free = 0u;
            return block_address + USER_HEAP_BLOCK_HEADER_SIZE;
        }
        last_address = block_address;
        block_address = block->next;
        guard += 1u;
    }

    u32 total = USER_HEAP_BLOCK_HEADER_SIZE + size;
    if (user_heap_next + total < user_heap_next ||
        user_heap_next + total > user_heap_limit) {
        serial_print("LeonOS user heap exhausted\r\n");
        return 0u;
    }
    block_address = user_heap_next;
    struct UserHeapBlock *block = (struct UserHeapBlock *) block_address;
    block->size = size;
    block->next = 0u;
    block->free = 0u;
    block->magic = USER_HEAP_BLOCK_MAGIC;
    if (last_address != 0u) {
        ((struct UserHeapBlock *) last_address)->next = block_address;
    } else {
        user_heap_first = block_address;
    }
    user_heap_next += total;
    return block_address + USER_HEAP_BLOCK_HEADER_SIZE;
}

static u8 user_syscall_free(u32 user_ptr)
{
    if (user_ptr == 0u) {
        return 1u;
    }
    if (user_ptr < user_heap_base + USER_HEAP_BLOCK_HEADER_SIZE ||
        user_ptr >= user_heap_next) {
        serial_print("LeonOS user heap free bad ptr\r\n");
        return 0u;
    }

    u32 target = user_ptr - USER_HEAP_BLOCK_HEADER_SIZE;
    u32 block_address = user_heap_first;
    u32 previous_address = 0u;
    u32 guard = 0u;
    while (block_address != 0u && guard < 1048576u) {
        if (!user_heap_block_valid(block_address)) {
            serial_print("LeonOS user heap corrupt\r\n");
            return 0u;
        }
        struct UserHeapBlock *block = (struct UserHeapBlock *) block_address;
        if (block_address == target) {
            if (block->free) {
                serial_print("LeonOS user heap double free\r\n");
                return 0u;
            }
            block->free = 1u;
            while (block->next != 0u && user_heap_block_valid(block->next)) {
                struct UserHeapBlock *next =
                        (struct UserHeapBlock *) block->next;
                if (!next->free) {
                    break;
                }
                block->size += USER_HEAP_BLOCK_HEADER_SIZE + next->size;
                block->next = next->next;
            }
            if (previous_address != 0u) {
                struct UserHeapBlock *previous =
                        (struct UserHeapBlock *) previous_address;
                if (previous->free) {
                    previous->size += USER_HEAP_BLOCK_HEADER_SIZE + block->size;
                    previous->next = block->next;
                    block = previous;
                    block_address = previous_address;
                }
            }
            return 1u;
        }
        previous_address = block_address;
        block_address = block->next;
        guard += 1u;
    }
    serial_print("LeonOS user heap free unknown ptr\r\n");
    return 0u;
}

static void net_browser_open_url_internal(const char *url, u8 add_history,
                                          u8 restore_scroll);
static void net_browser_retry_current_url_after_https_stall(const char *reason);
static void net_user_fetch_finish(u8 ok);
static u32 net_append_capped(char *dst, u32 pos, u32 dst_len, const char *src);

static void net_user_request_reset(void)
{
    net_user_request_method[0] = 'G';
    net_user_request_method[1] = 'E';
    net_user_request_method[2] = 'T';
    net_user_request_method[3] = 0;
    net_user_request_content_type[0] = 0;
    net_user_request_accept[0] = 0;
    net_user_request_body_len = 0u;
}

static void net_user_fetch_reset(void)
{
    net_user_fetch_active = 0u;
    net_user_fetch_done = 0u;
    net_user_fetch_ok = 0u;
    net_user_fetch_ignore_tail = 0u;
    net_user_fetch_len = 0u;
    net_user_fetch_content_length = 0u;
    net_user_fetch_content_length_seen = 0u;
    net_user_fetch_status_code = 0u;
    net_user_fetch_flags = 0u;
    net_user_fetch_content_type[0] = 0;
    net_user_fetch_location[0] = 0;
    net_user_stream_busy_log_count = 0u;
    net_user_request_reset();
}

static void net_user_fetch_note_body(const u8 *data, u32 len)
{
    if (!net_user_fetch_active || len == 0u) {
        return;
    }
    if (net_user_fetch_len >= USER_NET_FETCH_MAX) {
        return;
    }
    u32 room = USER_NET_FETCH_MAX - net_user_fetch_len;
    if (len > room) {
        len = room;
        net_user_fetch_flags |= USER_NET_FETCH_FLAG_TRUNCATED;
    }
    mem_copy(net_user_fetch_buf + net_user_fetch_len, data, len);
    net_user_fetch_len += len;
    if (net_user_fetch_content_length_seen &&
        net_user_fetch_len >= net_user_fetch_content_length) {
        net_user_fetch_finish(1u);
    }
}

static void net_user_fetch_finish(u8 ok)
{
    net_user_fetch_done = 1u;
    net_user_fetch_ok = ok;
    net_user_fetch_active = 0u;
    net_user_fetch_ignore_tail = 1u;
    net_browser_fetch_enabled = 0u;
    serial_print("LeonOS user net fetch done bytes ");
    serial_print_dec(net_user_fetch_len);
    serial_print(ok ? " ok\r\n" : " err\r\n");
}

static void net_user_fetch_begin(const char *url)
{
    net_user_fetch_reset();
    net_user_fetch_active = 1u;
    net_browser_open_url_internal(url, 0u, 0u);
}

static void net_user_fetch_begin_request(const char *url, const char *method,
                                         const u8 *body, u32 body_len,
                                         const char *content_type,
                                         const char *accept)
{
    net_user_fetch_reset();
    net_append_capped(net_user_request_method, 0u,
                      sizeof(net_user_request_method), method);
    net_append_capped(net_user_request_content_type, 0u,
                      sizeof(net_user_request_content_type), content_type);
    net_append_capped(net_user_request_accept, 0u,
                      sizeof(net_user_request_accept), accept);
    if (body != 0 && body_len != 0u) {
        if (body_len > USER_NET_REQUEST_BODY_MAX) {
            body_len = USER_NET_REQUEST_BODY_MAX;
        }
        mem_copy(net_user_request_body, body, body_len);
        net_user_request_body_len = body_len;
    }
    net_user_fetch_active = 1u;
    net_browser_open_url_internal(url, 0u, 0u);
}

static void net_user_fetch_drive(void);
static u32 user_syscall_net_fetch(u32 url_ptr, u32 buf_ptr, u32 max_len);
static u32 user_syscall_net_fetch_ex(u32 request_ptr);
static u32 user_syscall_net_fetch_meta(u32 meta_ptr);
static u32 user_syscall_net_stream_open(u32 url_ptr);
static u32 user_syscall_net_stream_poll(u32 handle);
static u32 user_syscall_net_stream_read(u32 handle, u32 buf_ptr, u32 max_len);
static u32 user_syscall_net_stream_meta(u32 handle, u32 meta_ptr);
static u32 user_syscall_net_stream_close(u32 handle);

/* C half of the int 0x80 gate. Runs in ring 0 on the TSS esp0 stack. */
void syscall_dispatch(const struct InterruptFrame *frame)
{
    struct InterruptFrame *ret = (struct InterruptFrame *) frame;
    u32 number = frame->eax;
    if (number == SYS_WRITE) {
        user_syscall_write(frame->ebx);
        ret->eax = 1u;
        return;
    }
    if (number == SYS_FB_INFO) {
        ret->eax = user_syscall_fb_info(frame->ebx);
        return;
    }
    if (number == SYS_FB_FILL) {
        ret->eax = user_syscall_fb_fill(frame->ebx, frame->ecx, frame->edx,
                                        frame->esi, frame->edi);
        return;
    }
    if (number == SYS_FB_PRESENT) {
        ret->eax = user_syscall_fb_present();
        return;
    }
    if (number == SYS_EVENT_POLL) {
        ret->eax = user_syscall_event_poll(frame->ebx);
        return;
    }
    if (number == SYS_BROWSER_OPEN) {
        ret->eax = user_syscall_browser_open(frame->ebx);
        return;
    }
    if (number == SYS_YIELD) {
        ret->eax = user_syscall_yield();
        return;
    }
    if (number == SYS_MILLIS) {
        ret->eax = user_syscall_millis();
        return;
    }
    if (number == SYS_MALLOC) {
        ret->eax = user_syscall_malloc(frame->ebx);
        return;
    }
    if (number == SYS_FREE) {
        ret->eax = user_syscall_free(frame->ebx);
        return;
    }
    if (number == SYS_NET_FETCH) {
        ret->eax = user_syscall_net_fetch(frame->ebx, frame->ecx, frame->edx);
        return;
    }
    if (number == SYS_NET_FETCH_META) {
        ret->eax = user_syscall_net_fetch_meta(frame->ebx);
        return;
    }
    if (number == SYS_NET_STREAM_OPEN) {
        ret->eax = user_syscall_net_stream_open(frame->ebx);
        return;
    }
    if (number == SYS_NET_STREAM_POLL) {
        ret->eax = user_syscall_net_stream_poll(frame->ebx);
        return;
    }
    if (number == SYS_NET_STREAM_READ) {
        ret->eax = user_syscall_net_stream_read(frame->ebx, frame->ecx,
                                                frame->edx);
        return;
    }
    if (number == SYS_NET_STREAM_META) {
        ret->eax = user_syscall_net_stream_meta(frame->ebx, frame->ecx);
        return;
    }
    if (number == SYS_NET_STREAM_CLOSE) {
        ret->eax = user_syscall_net_stream_close(frame->ebx);
        return;
    }
    if (number == SYS_FB_TEXT) {
        ret->eax = user_syscall_fb_text(frame->ebx, frame->ecx, frame->edx,
                                        frame->esi);
        return;
    }
    if (number == SYS_FB_BLIT) {
        ret->eax = user_syscall_fb_blit(frame->ebx);
        return;
    }
    if (number == SYS_NET_FETCH_EX) {
        ret->eax = user_syscall_net_fetch_ex(frame->ebx);
        return;
    }
    if (number == SYS_EXIT) {
        serial_print("LeonOS user app exited\r\n");
        resume_to_kernel(); /* does not return */
        return;
    }
    serial_print("LeonOS user syscall bad number ");
    serial_print_hex(number);
    serial_write('\r');
    serial_write('\n');
}

static void leo_run_user_app(const char raw_name[11], const char *display_name)
{
    if (!using_fat32) {
        serial_print("LeonOS user app skipped no FAT32\r\n");
        return;
    }
    if (user_app_running) {
        serial_print("LeonOS user app skipped already running\r\n");
        return;
    }

    serial_print_line(msg_user_launch_begin);
    u32 cluster = 0;
    u32 size = 0;
    if (!find_loaded_file(display_name, &cluster, &size) &&
        !fat32_find_file_raw(raw_name, &cluster, &size)) {
        serial_print("LeonOS user app not found\r\n");
        return;
    }
    serial_print_line(msg_user_image_found);
    if (size < 32u || size > USER_APP_MAX_IMAGE_BYTES) {
        serial_print("LeonOS user app bad size\r\n");
        return;
    }
    if (!ata_read_sector(fat32_cluster_lba(cluster), fat32_rb_buf)) {
        serial_print("LeonOS user app read failed\r\n");
        return;
    }
    serial_print_line(msg_user_sector_read);
    if (fat32_rb_buf[0] != 'L' || fat32_rb_buf[1] != 'E' ||
        fat32_rb_buf[2] != 'O' || fat32_rb_buf[3] != '1') {
        serial_print("LeonOS user app bad magic\r\n");
        return;
    }
    serial_print_line(msg_user_magic_ok);
    u32 version = read32(fat32_rb_buf + 4);
    u32 entry_offset = read32(fat32_rb_buf + 8);
    u32 image_size = read32(fat32_rb_buf + 12);
    u32 bss_size = read32(fat32_rb_buf + 16);
    serial_print(msg_user_header_prefix);
    serial_print_hex(version);
    serial_print(msg_user_entry_prefix);
    serial_print_hex(entry_offset);
    serial_write('\r');
    serial_write('\n');
    if (version != 2u) {
        serial_print("LeonOS user app bad version\r\n");
        return;
    }
    if (image_size < 32u || image_size > size || image_size > USER_APP_MAX_IMAGE_BYTES) {
        serial_print("LeonOS user app bad image size\r\n");
        return;
    }
    if (entry_offset < 32u || entry_offset >= image_size) {
        serial_print("LeonOS user app bad entry\r\n");
        return;
    }
    if (bss_size > USER_APP_MAX_IMAGE_BYTES || image_size + bss_size < image_size ||
        image_size + bss_size > USER_APP_MAX_IMAGE_BYTES) {
        serial_print("LeonOS user app too large\r\n");
        return;
    }
    if (!pmm_ready || !paging_ready) {
        serial_print("LeonOS user app no allocator\r\n");
        return;
    }
    serial_print_line(msg_user_validated);

    u32 image_total = image_size + bss_size;
    u32 code_pages = align_up(image_total, PAGE_SIZE) / PAGE_SIZE;
    u8 is_netsurf = text_eq(display_name, "NETSURF.LEO");
    u32 stack_pages = is_netsurf ? USER_STACK_PAGES_NETSURF :
            USER_STACK_PAGES_DEFAULT;
    u32 stack_guard_pages = is_netsurf ? USER_STACK_GUARD_PAGES_NETSURF : 0u;
    u32 heap_pages = is_netsurf ? USER_HEAP_PAGES_NETSURF :
            USER_HEAP_PAGES_DEFAULT;
    if (code_pages + stack_guard_pages + stack_pages + heap_pages >
            USER_VIRT_PAGES) {
        if (code_pages + stack_guard_pages + stack_pages >= USER_VIRT_PAGES) {
            heap_pages = 0u;
        } else {
            heap_pages = USER_VIRT_PAGES - code_pages -
                    stack_guard_pages - stack_pages;
        }
    }
    if (code_pages == 0u || stack_pages == 0u || heap_pages == 0u) {
        serial_print("LeonOS user app bad size\r\n");
        return;
    }

    u32 code = pmm_alloc_contiguous_pages(code_pages);
    u32 stack = pmm_alloc_contiguous_pages(stack_pages);
    u32 heap = pmm_alloc_contiguous_pages(heap_pages);
    serial_print(msg_user_alloc_prefix);
    serial_print_hex(code);
    serial_print(msg_user_stack_prefix);
    serial_print_hex(stack);
    serial_write('\r');
    serial_write('\n');
    if (code == 0u || stack == 0u || heap == 0u) {
        serial_print("LeonOS user app no memory\r\n");
        if (code != 0u) { pmm_free_page(code); }
        if (stack != 0u) { pmm_free_page(stack); }
        if (heap != 0u) { pmm_free_page(heap); }
        return;
    }

    u8 *dest = (u8 *) code;
    serial_print("LeonOS user app copy begin bytes=");
    serial_print_dec(image_size);
    serial_write('\r');
    serial_write('\n');
    u32 copied = fat32_read_file_bytes(cluster, size, dest, image_size);
    if (copied != image_size) {
        serial_print("LeonOS user app read failed\r\n");
        for (u32 i = 0; i < code_pages; i += 1) { pmm_free_page(code + i * PAGE_SIZE); }
        for (u32 i = 0; i < stack_pages; i += 1) { pmm_free_page(stack + i * PAGE_SIZE); }
        for (u32 i = 0; i < heap_pages; i += 1) { pmm_free_page(heap + i * PAGE_SIZE); }
        return;
    }
    for (u32 i = image_size; i < code_pages * PAGE_SIZE; i += 1) {
        dest[i] = 0;
    }
    u8 *stk = (u8 *) stack;
    for (u32 i = 0; i < stack_pages * PAGE_SIZE; i += 1) {
        stk[i] = 0;
    }
    serial_print_line(msg_user_copy_ok);

    u32 user_total_pages = code_pages + stack_pages + heap_pages;
    if (user_total_pages > USER_VIRT_PAGES) {
        serial_print_line(msg_user_pages_bad);
        for (u32 i = 0; i < code_pages; i += 1) { pmm_free_page(code + i * PAGE_SIZE); }
        for (u32 i = 0; i < stack_pages; i += 1) { pmm_free_page(stack + i * PAGE_SIZE); }
        for (u32 i = 0; i < heap_pages; i += 1) { pmm_free_page(heap + i * PAGE_SIZE); }
        return;
    }

    for (u32 table = 0; table < USER_VIRT_TABLES; table += 1u) {
        for (u32 i = 0; i < 1024u; i += 1u) {
            user_page_tables[table][i] = 0u;
        }
    }
    for (u32 i = 0; i < code_pages; i += 1) {
        u32 page = i;
        user_page_tables[page / 1024u][page % 1024u] =
            (code + i * PAGE_SIZE) | 0x007u;
    }
    for (u32 i = 0; i < stack_pages; i += 1) {
        u32 page = code_pages + stack_guard_pages + i;
        user_page_tables[page / 1024u][page % 1024u] =
            (stack + i * PAGE_SIZE) | 0x007u;
    }
    for (u32 i = 0; i < heap_pages; i += 1) {
        u32 page = code_pages + stack_guard_pages + stack_pages + i;
        user_page_tables[page / 1024u][page % 1024u] =
            (heap + i * PAGE_SIZE) | 0x007u;
    }
    for (u32 table = 0; table < USER_VIRT_TABLES; table += 1u) {
        page_directory[USER_VIRT_PDE + table] =
            ((u32) user_page_tables[table]) | 0x007u;
    }
    u32 cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
    serial_print_line(msg_user_pages_ok);

    user_app_base = USER_VIRT_BASE;
    user_app_limit = USER_VIRT_BASE + code_pages * PAGE_SIZE;
    user_stack_base = user_app_limit + stack_guard_pages * PAGE_SIZE;
    user_stack_limit = user_stack_base + stack_pages * PAGE_SIZE;
    user_heap_base = user_stack_limit;
    user_heap_limit = user_heap_base + heap_pages * PAGE_SIZE;
    user_heap_next = user_heap_base;
    user_heap_first = 0u;
    event_clear();
    user_event_poll_reported = 0u;
    user_app_running = 1;
    user_app_netsurf_running = is_netsurf;

    u32 entry_addr = USER_VIRT_BASE + entry_offset;
    u32 user_esp = user_stack_limit - 16u; /* leave a little headroom, 16-aligned */

    serial_print(msg_user_enter_prefix);
    serial_print(display_name);
    serial_print(msg_user_base_prefix);
    serial_print_hex(USER_VIRT_BASE);
    serial_print(msg_user_entry_prefix);
    serial_print_hex(entry_addr);
    serial_write('\r');
    serial_write('\n');

    enter_user_mode(entry_addr, user_esp, USER_DATA_SEL, USER_CODE_SEL);

    /* Control returns here after the app's exit syscall (resume_to_kernel). */
    serial_print("LeonOS user app returned to kernel\r\n");

    for (u32 i = 0; i < user_total_pages; i += 1u) {
        user_page_tables[i / 1024u][i % 1024u] = 0u;
    }
    for (u32 table = 0; table < USER_VIRT_TABLES; table += 1u) {
        page_directory[USER_VIRT_PDE + table] = 0u;
    }
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
    for (u32 i = 0; i < code_pages; i += 1) { pmm_free_page(code + i * PAGE_SIZE); }
    for (u32 i = 0; i < stack_pages; i += 1) { pmm_free_page(stack + i * PAGE_SIZE); }
    for (u32 i = 0; i < heap_pages; i += 1) { pmm_free_page(heap + i * PAGE_SIZE); }
    user_app_base = 0;
    user_app_limit = 0;
    user_stack_base = 0;
    user_stack_limit = 0;
    user_heap_base = 0;
    user_heap_limit = 0;
    user_heap_next = 0;
    user_heap_first = 0u;
    user_app_running = 0;
    user_app_netsurf_running = 0u;
    user_fb_overlay_active = 0u;
    dirty = 1;
}

static void open_selected_file(void)
{
    if (file_count == 0) {
        return;
    }

    if (using_fat32) {
        open_selected_file_fat32();
        return;
    }

    struct FileEntry *entry = &files[selected_file];
    u32 cluster = entry->first_cluster;
    u32 copied = 0;
    const u8 *data = (const u8 *) g_boot->data_cache_address;

    while (cluster >= 2u && cluster < 0x0FF8u && copied < FILE_BUFFER_SIZE) {
        u32 sector_index = cluster - 2u;
        if (sector_index >= g_boot->data_cache_sectors) {
            break;
        }
        const u8 *src = data + sector_index * 512u;
        for (u32 i = 0; i < 512u && copied < FILE_BUFFER_SIZE && copied < entry->size; i += 1) {
            file_buffer[copied++] = src[i];
        }
        if (copied >= entry->size) {
            break;
        }
        cluster = fat_next((u16) cluster);
    }

    file_buffer_size = copied;
    file_loaded = 1;
    copy_name(open_name, entry->name);
    serial_print("LeonOS 32-bit FAT12 file opened\r\n");
    dirty = 1;
}

enum ShellSerialMsg {
    SHELL_MSG_WIN_RESTORED = 0,
    SHELL_MSG_WIN_MAXIMIZED,
    SHELL_MSG_WIN_MINIMIZED,
    SHELL_MSG_WIN_CLOSED,
    SHELL_MSG_TAB_0,
    SHELL_MSG_TAB_1,
    SHELL_MSG_START_OPEN,
    SHELL_MSG_START_CLOSED,
    SHELL_MSG_WIN_MOVED,
    SHELL_MSG_BOOT_READY,
    SHELL_MSG_APP_FILES,
    SHELL_MSG_APP_APPS,
    SHELL_MSG_APP_ABOUT,
    SHELL_MSG_APP_LOG,
    SHELL_MSG_APP_NET,
    SHELL_MSG_APP_HELLO,
    SHELL_MSG_APP_UHELLO,
    SHELL_MSG_APP_WRITE,
    SHELL_MSG_APP_UGFX,
    SHELL_MSG_APP_UCDEMO,
    SHELL_MSG_APP_UBROWSER,
    SHELL_MSG_APP_UNETRUN,
    SHELL_MSG_APP_UWEB,
    SHELL_MSG_APP_NETSURF,
    SHELL_MSG_APP_USTREAM
};

#include "tls_crypto.inc.c"

#define NET_TLS_RX_BUF_MAX 32768u
#define NET_TLS_PLAIN_MAX 18432u
#define NET_TLS_CH_MSG_MAX 288u
#define NET_TLS_OOO_MAX 256u
#define NET_BROWSER_TEXT_MAX 16384u
#define NET_BROWSER_RESOURCE_MAX 12u
#define NET_BROWSER_RESOURCE_URL_MAX 4096u
#define NET_BROWSER_HISTORY_MAX 8u
#define NET_BROWSER_HOST_MAX 64u
#define NET_BROWSER_PATH_MAX 4032u
#define NET_HTTPS_REQUEST_MAX 6144u
#define NET_TLS_APP_CHUNK_MAX 512u
#define NET_BROWSER_RENDER_MAX 128u
#define NET_BROWSER_RENDER_TEXT_MAX 220u
#define NET_HTTPS_HEADER_MAX 16384u
#define NET_TCP_RECV_WINDOW 32768u
#define NET_BROWSER_BODY_PARSE_LIMIT 262144u
#define NET_BROWSER_BODY_IDLE_TICKS 300u
#define NET_BROWSER_HEADER_ONLY_RETRY_MAX 7u
#define NET_USER_FETCH_EMPTY_BODY_RETRY_MAX 1u
#define NET_TLS_SYN_RETRY_TICKS 120u
#define NET_TLS_SYN_RETRY_MAX 3u
#define NET_BROWSER_RENDER_KIND_TEXT 1u
#define NET_BROWSER_RENDER_KIND_LINK 2u
#define NET_BROWSER_RENDER_KIND_HEADING 3u
#define NET_BROWSER_RENDER_KIND_TITLE 4u
#define NET_BROWSER_RENDER_KIND_LIST 5u
#define NET_BROWSER_RENDER_KIND_IMAGE 6u
#define NET_BROWSER_RENDER_KIND_CSS 7u
#define NET_BROWSER_RENDER_KIND_JS 8u
#define NET_BROWSER_RENDER_KIND_TABLE 9u
#define NET_BROWSER_RENDER_KIND_QUOTE 10u
#define NET_BROWSER_RENDER_KIND_META 11u
#define NET_BROWSER_RENDER_KIND_EMBED 12u
#define NET_BROWSER_RENDER_KIND_DIALOG 13u
#define NET_BROWSER_RENDER_FLAG_CENTER 0x01u
#define NET_BROWSER_RENDER_FLAG_BOX 0x02u
#define NET_BROWSER_RENDER_FLAG_INPUT 0x04u
#define NET_BROWSER_RENDER_FLAG_BUTTON 0x08u
#define NET_BROWSER_RENDER_FLAG_HIDDEN 0x10u
#define NET_BROWSER_RENDER_FLAG_BOLD 0x20u
#define NET_BROWSER_RENDER_FLAG_LARGE 0x40u
#define NET_BROWSER_RENDER_FLAG_MONO 0x80u
#define NET_BROWSER_CSS_STYLE_COLOR 0x0001u
#define NET_BROWSER_CSS_STYLE_BG 0x0002u
#define NET_BROWSER_CSS_STYLE_WIDTH 0x0004u
#define NET_BROWSER_CSS_STYLE_HEIGHT 0x0008u
#define NET_BROWSER_CSS_STYLE_MARGIN 0x0010u
#define NET_BROWSER_CSS_STYLE_PADDING 0x0020u
#define NET_BROWSER_CSS_STYLE_BORDER 0x0040u
#define NET_BROWSER_CSS_STYLE_DISPLAY_NONE 0x0080u
#define NET_BROWSER_CSS_STYLE_FONT_BOLD 0x0100u
#define NET_BROWSER_CSS_STYLE_FONT_LARGE 0x0200u
#define NET_BROWSER_CSS_STYLE_CENTER 0x0400u
#define NET_BROWSER_CSS_STYLE_DISPLAY_BLOCK 0x0800u
#define NET_BROWSER_DOM_KIND_BLOCK 1u
#define NET_BROWSER_DOM_KIND_INLINE 2u
#define NET_BROWSER_DOM_KIND_TABLE 3u
#define NET_BROWSER_DOM_KIND_CONTROL 4u
#define NET_BROWSER_DOM_KIND_RESOURCE 5u
#define NET_BROWSER_DOM_KIND_UNSUPPORTED 6u
#define NET_BROWSER_STYLE_STACK_MAX 8u
#define NET_BROWSER_CSS_RULE_MAX 16u
#define NET_BROWSER_CSS_SELECTOR_MAX 24u
#define NET_BROWSER_JS_WRITE_MAX 96u
#define NET_BROWSER_DOM_MAX 64u
#define NET_BROWSER_DOM_STACK_MAX 16u
#define NET_BROWSER_LAYOUT_MAX 160u
#define NET_BROWSER_LINK_MAX 16u
#define NET_BROWSER_FORM_MAX 8u
#define NET_BROWSER_CONTROL_MAX 40u
#define NET_BROWSER_CONTROL_NAME_MAX 32u
#define NET_BROWSER_CONTROL_VALUE_MAX 96u
#define NET_BROWSER_IMAGE_MAX 8u
#define NET_BROWSER_IMAGE_LABEL_MAX 96u
#define NET_BROWSER_IMAGE_SIDE_MAX 64u
#define NET_BROWSER_IMAGE_PIXELS_MAX (NET_BROWSER_IMAGE_SIDE_MAX * NET_BROWSER_IMAGE_SIDE_MAX)
#define NET_BROWSER_IMAGE_RAW_MAX 65535u
#define NET_BROWSER_PNG_RAW_MAX 16512u
#define NET_BROWSER_IMAGE_STATUS_EMPTY 0u
#define NET_BROWSER_IMAGE_STATUS_PENDING 1u
#define NET_BROWSER_IMAGE_STATUS_DECODED 2u
#define NET_BROWSER_IMAGE_STATUS_UNSUPPORTED 3u
#define NET_BROWSER_IMAGE_STATUS_ERROR 4u
#define NET_BROWSER_IMAGE_FORMAT_UNKNOWN 0u
#define NET_BROWSER_IMAGE_FORMAT_PNG 1u
#define NET_BROWSER_IMAGE_FORMAT_JPEG 2u
#define NET_BROWSER_IMAGE_FORMAT_OTHER 3u
#define NET_COOKIE_MAX 4u
#define NET_COOKIE_NAME_MAX 24u
#define NET_COOKIE_VALUE_MAX 96u
#define NET_BROWSER_TAG_NAME_MAX 24u
#define NET_BROWSER_ATTR_NAME_MAX 32u
#define NET_BROWSER_ENTITY_MAX 16u
#define NET_BROWSER_CONTROL_KIND_TEXT 1u
#define NET_BROWSER_CONTROL_KIND_SUBMIT 2u
#define NET_BROWSER_CONTROL_KIND_BUTTON 3u
#define NET_BROWSER_CONTROL_KIND_HIDDEN 4u
#define NET_BROWSER_CONTROL_KIND_OTHER 5u

static struct Sha256Ctx net_tls_transcript;
static u8 net_tls_client_hello_msg[NET_TLS_CH_MSG_MAX];
static u32 net_tls_client_hello_msg_len;
static u8 net_tls_rx_buf[NET_TLS_RX_BUF_MAX];
static u8 net_tls_ooo_buf[NET_TLS_OOO_MAX][NET_MTU];
static u8 net_tls_plain_buf[NET_TLS_PLAIN_MAX + 16u];
static u32 net_tls_rx_len;
static u32 net_tls_ooo_seq[NET_TLS_OOO_MAX];
static u32 net_tls_ooo_len[NET_TLS_OOO_MAX];
static u8 net_tls_ooo_valid[NET_TLS_OOO_MAX];
static u8 net_tls_ooo_log_count;
static u8 net_tls_ooo_idle_wait_count;
static u32 net_tls_ooo_reack_tick;
static u8 net_tls_ooo_reack_count;
static u8 net_tls_server_hs_key[32];
static u8 net_tls_server_hs_iv[12];
static u8 net_tls_client_hs_key[32];
static u8 net_tls_client_hs_iv[12];
static u8 net_tls_server_app_key[32];
static u8 net_tls_server_app_iv[12];
static u8 net_tls_client_app_key[32];
static u8 net_tls_client_app_iv[12];
static u8 net_tls_server_finished_key[32];
static u8 net_tls_client_finished_key[32];
static u8 net_tls_handshake_secret[32];
static u64 net_tls_server_hs_seq;
static u64 net_tls_client_hs_seq;
static u64 net_tls_server_app_seq;
static u64 net_tls_client_app_seq;
static u8 net_tls_handshake_keys_ready;
static u8 net_tls_server_finished_seen;
static u8 net_tls_client_finished_sent;
static u8 net_tls_primary_done;
static u8 net_tls_primary_close_sent;
static u32 net_tls_primary_done_tick;
static u8 net_https_get_sent;
static u32 net_https_get_tick;
static u8 net_https_response_seen;
static char net_https_status[4] = "---";
static u8 net_https_headers_done;
static u8 net_https_chunked;
static u8 net_https_body_complete;
static u8 net_browser_finalize_sent;
static u32 net_https_body_decoded_total32;
static u32 net_https_body_last_tick;
static u32 net_https_headers_tick;
static u8 net_browser_header_only_retry_count;
static u8 net_http_chunk_state;
static u8 net_http_chunk_seen_digit;
static u8 net_http_chunk_skip_ext;
static u32 net_http_chunk_remaining;
static u8 net_https_header_buf[NET_HTTPS_HEADER_MAX];
static u32 net_https_header_len;
static u8 net_https_header_log_sent;
static u8 net_https_body_raw_log_count;
static u16 net_https_body_raw_total;
static u8 net_https_body_decoded_log_count;
static u16 net_https_body_decoded_total;
static const char net_browser_default_url[] = "https://www.google.com/?igu=1&hl=en";
static char net_browser_host[NET_BROWSER_HOST_MAX] = "www.google.com";
static char net_browser_path[NET_BROWSER_PATH_MAX] = "/?igu=1&hl=en";
static char net_browser_current_url[NET_BROWSER_RESOURCE_URL_MAX] =
    "https://www.google.com/?igu=1&hl=en";
static char net_browser_open_host_scratch[NET_BROWSER_HOST_MAX];
static char net_browser_open_path_scratch[NET_BROWSER_PATH_MAX];
static char net_browser_open_full_scratch[NET_BROWSER_RESOURCE_URL_MAX];
static char net_https_request_buf[NET_HTTPS_REQUEST_MAX];
static u8 net_tls_app_record_buf[NET_TLS_APP_CHUNK_MAX + 32u];
static char net_browser_state[9] = "READY";
static char net_browser_history[NET_BROWSER_HISTORY_MAX][NET_BROWSER_RESOURCE_URL_MAX];
static u32 net_browser_history_scroll[NET_BROWSER_HISTORY_MAX];
static u8 net_browser_history_count;
static u8 net_browser_history_index;
static u8 net_browser_history_suppress;
static u16 net_browser_history_back_count;
static u16 net_browser_history_forward_count;
static u16 net_browser_history_reload_count;
static u16 net_browser_history_stop_count;
static u16 net_browser_history_complete_count;
static u16 net_browser_history_error_count;
static char net_browser_text[NET_BROWSER_TEXT_MAX] =
    "BROWSER READY\n\nhttps://www.google.com/?igu=1&hl=en";
static u32 net_browser_text_len =
    sizeof("BROWSER READY\n\nhttps://www.google.com/?igu=1&hl=en") - 1u;
static char net_cookie_host[NET_BROWSER_HOST_MAX];
static char net_cookie_name[NET_COOKIE_MAX][NET_COOKIE_NAME_MAX];
static char net_cookie_value[NET_COOKIE_MAX][NET_COOKIE_VALUE_MAX];
static u8 net_cookie_count;
static u32 net_browser_scroll;
static u8 net_browser_text_ready = 1u;
static u8 net_browser_html_in_tag;
static u8 net_browser_html_skip;
static u8 net_browser_html_entity;
static u8 net_browser_html_tag_len;
static u8 net_browser_html_tag_done;
static u8 net_browser_html_ignore_tag;
static u8 net_browser_html_attr_state;
static u8 net_browser_html_attr_quote;
static u8 net_browser_html_attr_name_len;
static u8 net_browser_html_attr_value_len;
static u8 net_browser_html_structure_sent;
static u16 net_browser_tag_count;
static u16 net_browser_attr_count;
static u16 net_browser_link_count;
static u16 net_browser_anchor_count;
static u16 net_browser_script_count;
static u16 net_browser_style_count;
static u16 net_browser_image_count;
static u16 net_browser_form_count;
static u16 net_browser_input_count;
static u16 net_browser_button_count;
static u16 net_browser_select_count;
static u16 net_browser_textarea_count;
static u16 net_browser_table_count;
static u16 net_browser_table_cell_count;
static u16 net_browser_meta_count;
static u16 net_browser_heading_count;
static u16 net_browser_svg_count;
static u16 net_browser_svg_shape_count;
static u16 net_browser_picture_count;
static u16 net_browser_media_count;
static u16 net_browser_custom_count;
static u16 net_browser_dialog_count;
static u16 net_browser_template_count;
static u16 net_browser_noscript_count;
static u16 net_browser_aria_attr_count;
static u16 net_browser_data_attr_count;
static u16 net_browser_js_attr_count;
static u16 net_browser_srcset_count;
static u16 net_browser_css_bytes;
static u16 net_browser_css_rule_count;
static u16 net_browser_css_decl_count;
static u16 net_browser_css_color_count;
static u16 net_browser_css_font_count;
static u16 net_browser_css_url_count;
static u16 net_browser_css_display_none_count;
static u16 net_browser_css_stored_rule_count;
static u16 net_browser_css_matched_rule_count;
static u16 net_browser_css_cascade_apply_count;
static u16 net_browser_css_color_apply_count;
static u16 net_browser_css_background_apply_count;
static u16 net_browser_css_width_apply_count;
static u16 net_browser_css_height_apply_count;
static u16 net_browser_css_margin_apply_count;
static u16 net_browser_css_padding_apply_count;
static u16 net_browser_css_border_apply_count;
static u16 net_browser_js_bytes;
static u16 net_browser_js_token_count;
static u16 net_browser_js_function_count;
static u16 net_browser_js_var_count;
static u16 net_browser_js_document_count;
static u16 net_browser_js_window_count;
static u16 net_browser_js_location_count;
static u16 net_browser_js_write_count;
static u16 net_browser_event_attr_count;
static u16 net_browser_js_url_count;
static u16 net_browser_unsupported_feature_count;
static u8 net_browser_js_required;
static u8 net_browser_unsupported_summary_added;
static u16 net_browser_dom_count;
static u16 net_browser_dom_text_bytes;
static u16 net_browser_dom_hidden_count;
static u16 net_browser_dom_supported_count;
static u16 net_browser_dom_block_count;
static u16 net_browser_dom_inline_count;
static u16 net_browser_dom_table_count;
static u16 net_browser_dom_control_count;
static u16 net_browser_dom_resource_count;
static u16 net_browser_dom_unsupported_count;
static u16 net_browser_dom_placeholder_count;
static u8 net_browser_dom_depth;
static u8 net_browser_dom_max_depth;
static u8 net_browser_dom_stack_len;
static u16 net_browser_layout_count;
static u16 net_browser_layout_line_count;
static u16 net_browser_layout_link_count;
static u16 net_browser_layout_control_count;
static u16 net_browser_layout_block_count;
static u16 net_browser_layout_inline_count;
static u16 net_browser_layout_table_count;
static u16 net_browser_layout_box_count;
static u16 net_browser_layout_wrap_count;
static u16 net_browser_layout_margin_count;
static u16 net_browser_layout_padding_count;
static u16 net_browser_layout_border_count;
static u16 net_browser_layout_width;
static u16 net_browser_layout_height;
static u8 net_browser_layout_serial_sent;
static u16 net_browser_form_model_count;
static u16 net_browser_control_model_count;
static u16 net_browser_control_focusable_count;
static u16 net_browser_control_editable_count;
static u16 net_browser_control_hidden_count;
static u16 net_browser_control_submit_count;
static u16 net_browser_form_focus_count;
static u16 net_browser_form_edit_count;
static u16 net_browser_form_submit_event_count;
static u8 net_browser_active_form = 0xFFu;
static u8 net_browser_focused_control = 0xFFu;
static u8 net_browser_form_caret;
static u16 net_browser_link_target_count;
static u16 net_browser_link_click_count;
static u8 net_browser_current_link_index;
static u8 net_browser_last_clicked_link;
static u16 net_browser_href_count;
static u16 net_browser_src_count;
static u16 net_browser_action_count;
static char net_browser_html_tag[NET_BROWSER_TAG_NAME_MAX];
static char net_browser_html_attr_name[NET_BROWSER_ATTR_NAME_MAX];
static char net_browser_html_attr_value[96];
static char net_browser_html_entity_buf[NET_BROWSER_ENTITY_MAX];
static char net_browser_current_class[48];
static char net_browser_current_id[32];
static char net_browser_first_href[96];
static char net_browser_first_src[96];
static char net_browser_first_action[96];
static char net_browser_first_rel[24];
static char net_browser_first_input_type[24];
static char net_browser_current_img_src[96];
static char net_browser_current_img_alt[96];
static u16 net_browser_resource_total;
static u16 net_browser_resource_count;
static u8 net_browser_resource_fetch_pending;
static u8 net_browser_resource_fetch_started;
static u8 net_browser_resource_fetch_done;
static u8 net_browser_resource_fetch_body_seen;
static u16 net_browser_resource_fetch_bytes;
static u32 net_browser_resource_fetch_start_tick;
static char net_browser_resource_fetch_status[4] = "---";
static char net_browser_resource_fetch_phase[5] = "IDLE";
static u16 net_browser_navigation_count;
static u16 net_tls_primary_source_port_next;
static u16 net_tls_resource_source_port_next;
static char net_browser_resource_type[NET_BROWSER_RESOURCE_MAX];
static char net_browser_resource_url[NET_BROWSER_RESOURCE_MAX][NET_BROWSER_RESOURCE_URL_MAX];
static char net_browser_fetch_url[NET_BROWSER_RESOURCE_URL_MAX];
static u8 net_browser_render_count;
static u8 net_browser_render_current;
static u8 net_browser_render_in_anchor;
static u8 net_browser_render_in_title;
static u8 net_browser_render_in_list;
static u8 net_browser_render_list_depth;
static u8 net_browser_render_in_quote;
static u8 net_browser_render_in_pre;
static u8 net_browser_render_in_code;
static u8 net_browser_render_in_table_cell;
static u8 net_browser_render_table_row_cell;
static u8 net_browser_render_heading;
static u8 net_browser_render_kind[NET_BROWSER_RENDER_MAX];
static u8 net_browser_render_flags[NET_BROWSER_RENDER_MAX];
static u8 net_browser_render_link[NET_BROWSER_RENDER_MAX];
static u8 net_browser_render_control[NET_BROWSER_RENDER_MAX];
static u8 net_browser_render_image[NET_BROWSER_RENDER_MAX];
static u16 net_browser_render_css_flags[NET_BROWSER_RENDER_MAX];
static u32 net_browser_render_css_color[NET_BROWSER_RENDER_MAX];
static u32 net_browser_render_css_bg[NET_BROWSER_RENDER_MAX];
static u16 net_browser_render_css_width[NET_BROWSER_RENDER_MAX];
static u16 net_browser_render_css_height[NET_BROWSER_RENDER_MAX];
static u16 net_browser_render_css_margin[NET_BROWSER_RENDER_MAX];
static u16 net_browser_render_css_padding[NET_BROWSER_RENDER_MAX];
static u16 net_browser_render_css_border[NET_BROWSER_RENDER_MAX];
static char net_browser_render_text[NET_BROWSER_RENDER_MAX][NET_BROWSER_RENDER_TEXT_MAX];
static u8 net_browser_render_active_flags;
static u8 net_browser_render_tag_flags;
static u16 net_browser_render_active_css_flags;
static u32 net_browser_render_active_css_color;
static u32 net_browser_render_active_css_bg;
static u16 net_browser_render_active_css_width;
static u16 net_browser_render_active_css_height;
static u16 net_browser_render_active_css_margin;
static u16 net_browser_render_active_css_padding;
static u16 net_browser_render_active_css_border;
static u16 net_browser_render_tag_css_flags;
static u32 net_browser_render_tag_css_color;
static u32 net_browser_render_tag_css_bg;
static u16 net_browser_render_tag_css_width;
static u16 net_browser_render_tag_css_height;
static u16 net_browser_render_tag_css_margin;
static u16 net_browser_render_tag_css_padding;
static u16 net_browser_render_tag_css_border;
static u16 net_browser_inline_css_flags;
static u32 net_browser_inline_css_color;
static u32 net_browser_inline_css_bg;
static u16 net_browser_inline_css_width;
static u16 net_browser_inline_css_height;
static u16 net_browser_inline_css_margin;
static u16 net_browser_inline_css_padding;
static u16 net_browser_inline_css_border;
static u8 net_browser_render_style_depth;
static char net_browser_render_style_tag[NET_BROWSER_STYLE_STACK_MAX][NET_BROWSER_TAG_NAME_MAX];
static u8 net_browser_render_style_flags[NET_BROWSER_STYLE_STACK_MAX];
static u16 net_browser_render_style_css_flags[NET_BROWSER_STYLE_STACK_MAX];
static u32 net_browser_render_style_css_color[NET_BROWSER_STYLE_STACK_MAX];
static u32 net_browser_render_style_css_bg[NET_BROWSER_STYLE_STACK_MAX];
static u16 net_browser_render_style_css_width[NET_BROWSER_STYLE_STACK_MAX];
static u16 net_browser_render_style_css_height[NET_BROWSER_STYLE_STACK_MAX];
static u16 net_browser_render_style_css_margin[NET_BROWSER_STYLE_STACK_MAX];
static u16 net_browser_render_style_css_padding[NET_BROWSER_STYLE_STACK_MAX];
static u16 net_browser_render_style_css_border[NET_BROWSER_STYLE_STACK_MAX];
static u8 net_browser_control_type_len;
static char net_browser_control_type[24];
static u8 net_browser_control_value_len;
static char net_browser_control_value[96];
static char net_browser_current_control_name[NET_BROWSER_CONTROL_NAME_MAX];
static char net_browser_current_control_actual[NET_BROWSER_CONTROL_VALUE_MAX];
static char net_browser_current_form_action[NET_BROWSER_RESOURCE_URL_MAX];
static char net_browser_current_form_method[8];
static u8 net_browser_css_in_style;
static u8 net_browser_css_summary_added;
static u8 net_browser_css_token_len;
static u8 net_browser_css_after_display;
static char net_browser_css_token[16];
static u8 net_browser_css_state;
static u8 net_browser_css_selector_len;
static u8 net_browser_css_prop_len;
static u8 net_browser_css_value_len;
static u8 net_browser_css_current_selector_kind;
static u8 net_browser_css_current_flags;
static u16 net_browser_css_current_style_flags;
static u32 net_browser_css_current_color;
static u32 net_browser_css_current_bg;
static u16 net_browser_css_current_width;
static u16 net_browser_css_current_height;
static u16 net_browser_css_current_margin;
static u16 net_browser_css_current_padding;
static u16 net_browser_css_current_border;
static char net_browser_css_selector[NET_BROWSER_CSS_SELECTOR_MAX];
static char net_browser_css_prop[24];
static char net_browser_css_value[48];
static u8 net_browser_css_rule_kind[NET_BROWSER_CSS_RULE_MAX];
static u8 net_browser_css_rule_flags[NET_BROWSER_CSS_RULE_MAX];
static u16 net_browser_css_rule_style_flags[NET_BROWSER_CSS_RULE_MAX];
static u32 net_browser_css_rule_color[NET_BROWSER_CSS_RULE_MAX];
static u32 net_browser_css_rule_bg[NET_BROWSER_CSS_RULE_MAX];
static u16 net_browser_css_rule_width[NET_BROWSER_CSS_RULE_MAX];
static u16 net_browser_css_rule_height[NET_BROWSER_CSS_RULE_MAX];
static u16 net_browser_css_rule_margin[NET_BROWSER_CSS_RULE_MAX];
static u16 net_browser_css_rule_padding[NET_BROWSER_CSS_RULE_MAX];
static u16 net_browser_css_rule_border[NET_BROWSER_CSS_RULE_MAX];
static char net_browser_css_rule_selector[NET_BROWSER_CSS_RULE_MAX][NET_BROWSER_CSS_SELECTOR_MAX];
static char net_browser_css_rule_class[NET_BROWSER_CSS_RULE_MAX][NET_BROWSER_CSS_SELECTOR_MAX];
static char net_browser_css_class_pending[NET_BROWSER_CSS_SELECTOR_MAX];
static u8 net_browser_dom_parent[NET_BROWSER_DOM_MAX];
static u8 net_browser_dom_node_depth[NET_BROWSER_DOM_MAX];
static u8 net_browser_dom_node_flags[NET_BROWSER_DOM_MAX];
static u8 net_browser_dom_node_kind[NET_BROWSER_DOM_MAX];
static char net_browser_dom_tag[NET_BROWSER_DOM_MAX][NET_BROWSER_TAG_NAME_MAX];
static u8 net_browser_dom_stack[NET_BROWSER_DOM_STACK_MAX];
static u16 net_browser_layout_x[NET_BROWSER_LAYOUT_MAX];
static u16 net_browser_layout_y[NET_BROWSER_LAYOUT_MAX];
static u16 net_browser_layout_w[NET_BROWSER_LAYOUT_MAX];
static u16 net_browser_layout_h[NET_BROWSER_LAYOUT_MAX];
static u8 net_browser_layout_kind[NET_BROWSER_LAYOUT_MAX];
static u8 net_browser_layout_flags[NET_BROWSER_LAYOUT_MAX];
static u8 net_browser_layout_link[NET_BROWSER_LAYOUT_MAX];
static u8 net_browser_layout_control[NET_BROWSER_LAYOUT_MAX];
static char net_browser_form_action_url[NET_BROWSER_FORM_MAX][NET_BROWSER_RESOURCE_URL_MAX];
static u8 net_browser_form_get[NET_BROWSER_FORM_MAX];
static u8 net_browser_control_form[NET_BROWSER_CONTROL_MAX];
static u8 net_browser_control_kind[NET_BROWSER_CONTROL_MAX];
static u8 net_browser_control_focusable[NET_BROWSER_CONTROL_MAX];
static u8 net_browser_control_editable[NET_BROWSER_CONTROL_MAX];
static char net_browser_control_name[NET_BROWSER_CONTROL_MAX][NET_BROWSER_CONTROL_NAME_MAX];
static char net_browser_control_value_store[NET_BROWSER_CONTROL_MAX][NET_BROWSER_CONTROL_VALUE_MAX];
static char net_browser_control_label[NET_BROWSER_CONTROL_MAX][NET_BROWSER_CONTROL_VALUE_MAX];
static char net_browser_last_form_submit_url[NET_BROWSER_RESOURCE_URL_MAX];
static char net_browser_link_url[NET_BROWSER_LINK_MAX][NET_BROWSER_RESOURCE_URL_MAX];
static char net_browser_clicked_url[NET_BROWSER_RESOURCE_URL_MAX];
static u8 net_browser_resource_image[NET_BROWSER_RESOURCE_MAX];
static u8 net_browser_fetch_image_index;
static u8 net_browser_image_slot_count;
static u8 net_browser_image_decode_count;
static u8 net_browser_image_unsupported_count;
static u8 net_browser_image_png_count;
static u8 net_browser_image_jpeg_count;
static u8 net_browser_image_status[NET_BROWSER_IMAGE_MAX];
static u8 net_browser_image_format[NET_BROWSER_IMAGE_MAX];
static u16 net_browser_image_width[NET_BROWSER_IMAGE_MAX];
static u16 net_browser_image_height[NET_BROWSER_IMAGE_MAX];
static u8 net_browser_direct_image_active;
static u8 net_browser_direct_image_slot;
static u8 net_browser_direct_image_format;
static u32 net_browser_image_pixels[NET_BROWSER_IMAGE_MAX][NET_BROWSER_IMAGE_PIXELS_MAX];
static char net_browser_image_url[NET_BROWSER_IMAGE_MAX][NET_BROWSER_RESOURCE_URL_MAX];
static char net_browser_image_label[NET_BROWSER_IMAGE_MAX][NET_BROWSER_IMAGE_LABEL_MAX];
static u8 net_browser_resource_body[NET_BROWSER_IMAGE_RAW_MAX];
static u32 net_browser_resource_body_len;
static u8 net_browser_resource_body_overflow;
static u8 net_browser_png_idat[NET_BROWSER_IMAGE_RAW_MAX];
static u8 net_browser_png_raw[NET_BROWSER_PNG_RAW_MAX];
static u8 net_browser_png_row[NET_BROWSER_IMAGE_SIDE_MAX * 4u];
static u8 net_browser_png_prev_row[NET_BROWSER_IMAGE_SIDE_MAX * 4u];
static u8 net_browser_js_in_script;
static u8 net_browser_js_summary_added;
static u8 net_browser_js_token_len;
static u8 net_browser_js_recent_len;
static u8 net_browser_js_docwrite_state;
static u8 net_browser_js_write_quote;
static u8 net_browser_js_write_len;
static char net_browser_js_token[24];
static char net_browser_js_recent[16];
static char net_browser_js_write_buf[NET_BROWSER_JS_WRITE_MAX];
static u8 net_browser_html_entity_len;

/* --- PCI + RTL8139 + tiny Ethernet/IP stack ---------------------------- */

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu
#define PCI_VENDOR_REALTEK 0x10ECu
#define PCI_DEVICE_RTL8139 0x8139u

#define RTL_REG_IDR0    0x00u
#define RTL_REG_TSD0    0x10u
#define RTL_REG_TSAD0   0x20u
#define RTL_REG_RBSTART 0x30u
#define RTL_REG_CR      0x37u
#define RTL_REG_CAPR    0x38u
#define RTL_REG_IMR     0x3Cu
#define RTL_REG_ISR     0x3Eu
#define RTL_REG_TCR     0x40u
#define RTL_REG_RCR     0x44u
#define RTL_REG_CONFIG1 0x52u
#define RTL8139_FALLBACK_IO 0xC000u

#define ETH_TYPE_ARP  0x0806u
#define ETH_TYPE_IPV4 0x0800u
#define IP_PROTO_ICMP 1u
#define IP_PROTO_TCP  6u
#define IP_PROTO_UDP  17u
#define NET_DNS_SOURCE_PORT 0x1E0Au
#define NET_DNS_TXID 0x1E0Au
#define NET_TCP_SOURCE_PORT 0x1E0Bu
#define NET_TLS_SOURCE_PORT 0x1E0Cu
#define NET_TLS_RESOURCE_SOURCE_PORT 0x2E0Cu
#define NET_TCP_MSS 536u
#define NET_TCP_INITIAL_SEQ 0x1E0AB005u
#define NET_TLS_INITIAL_SEQ 0x1E0AB443u
#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u

static u16 net_dns_source_port = NET_DNS_SOURCE_PORT;
static u16 net_dns_txid = NET_DNS_TXID;
static u32 net_dns_query_tick;
static u8 net_dns_retry_count;
static u8 net_dns_rx_diag_count;

static void net_set_last(const char *text)
{
    u32 i = 0;
    for (; text[i] != 0 && i < sizeof(net_last_event) - 1u; i += 1u) {
        net_last_event[i] = text[i];
    }
    net_last_event[i] = 0;
    dirty = 1;
}

static void serial_print_hex_byte(u8 value)
{
    serial_write(hex_digit((u8) (value >> 4)));
    serial_write(hex_digit(value));
}

static void serial_print_mac(const u8 mac[6])
{
    for (u32 i = 0; i < 6u; i += 1u) {
        if (i != 0u) {
            serial_write(':');
        }
        serial_print_hex_byte(mac[i]);
    }
}

static void serial_print_ipv4(const u8 ip[4])
{
    for (u32 i = 0; i < 4u; i += 1u) {
        if (i != 0u) {
            serial_write('.');
        }
        serial_print_dec(ip[i]);
    }
}

static u32 pci_addr(u8 bus, u8 slot, u8 func, u8 offset)
{
    return 0x80000000u |
           ((u32) bus << 16) |
           ((u32) slot << 11) |
           ((u32) func << 8) |
           ((u32) offset & 0xFCu);
}

static u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value)
{
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

static u8 pci_find_rtl8139(u8 *out_bus, u8 *out_slot, u8 *out_func)
{
    for (u32 bus = 0; bus < 256u; bus += 1u) {
        for (u32 slot = 0; slot < 32u; slot += 1u) {
            for (u32 func = 0; func < 8u; func += 1u) {
                u32 id = pci_read32((u8) bus, (u8) slot, (u8) func, 0x00u);
                if (id == 0xFFFFFFFFu) {
                    continue;
                }
                if ((u16) id == PCI_VENDOR_REALTEK &&
                    (u16) (id >> 16) == PCI_DEVICE_RTL8139) {
                    *out_bus = (u8) bus;
                    *out_slot = (u8) slot;
                    *out_func = (u8) func;
                    return 1u;
                }
            }
        }
    }
    return 0u;
}

static u16 net_checksum(const u8 *data, u32 len)
{
    u32 sum = 0;
    for (u32 i = 0; i + 1u < len; i += 2u) {
        sum += ((u16) data[i] << 8) | data[i + 1u];
    }
    if ((len & 1u) != 0u) {
        sum += ((u16) data[len - 1u] << 8);
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (u16) (~sum);
}

static u16 net_transport_checksum(const u8 src_ip[4], const u8 dst_ip[4],
                                  u8 protocol, const u8 *segment, u32 len)
{
    u8 scratch[NET_MTU + 12u];
    if (len > NET_MTU) {
        return 0xFFFFu;
    }

    mem_copy(scratch + 0u, src_ip, 4u);
    mem_copy(scratch + 4u, dst_ip, 4u);
    scratch[8] = 0u;
    scratch[9] = protocol;
    write_be16(scratch + 10u, (u16) len);
    mem_copy(scratch + 12u, segment, len);
    return net_checksum(scratch, len + 12u);
}

static void net_write_ipv4_header(u8 *ip, u16 total_len, u8 protocol, const u8 dst_ip[4])
{
    ip[0] = 0x45u;
    ip[1] = 0;
    write_be16(ip + 2, total_len);
    write_be16(ip + 4, 0x1E0Au);
    write_be16(ip + 6, 0);
    ip[8] = 64u;
    ip[9] = protocol;
    write_be16(ip + 10, 0);
    write_be32(ip + 12, net_ip);
    write_be32(ip + 16, dst_ip);
    write_be16(ip + 10, net_checksum(ip, 20u));
}

static u8 rtl8139_tx(const u8 *frame, u32 len);
static void net_handle_frame(const u8 *frame, u32 len);

static u8 net_send_frame(const u8 dst_mac[6], u16 ethertype, const u8 *payload, u32 payload_len)
{
    if (!net_ready || payload_len > NET_MTU) {
        return 0u;
    }
    mem_copy(net_tx_packet, dst_mac, 6u);
    mem_copy(net_tx_packet + 6u, net_mac, 6u);
    write_be16(net_tx_packet + 12u, ethertype);
    mem_copy(net_tx_packet + 14u, payload, payload_len);
    return rtl8139_tx(net_tx_packet, payload_len + 14u);
}

static u8 net_send_arp_request_for(const u8 target_ip[4], const char *last_event,
                                   const char *proof)
{
    u8 *frame = net_tx_packet;
    mem_zero(frame, 60u);
    for (u32 i = 0; i < 6u; i += 1u) {
        frame[i] = 0xFFu;
        frame[6u + i] = net_mac[i];
    }

    frame[12] = 0x08u;
    frame[13] = 0x06u;
    frame[14] = 0x00u;
    frame[15] = 0x01u;
    frame[16] = 0x08u;
    frame[17] = 0x00u;
    frame[18] = 0x06u;
    frame[19] = 0x04u;
    frame[20] = 0x00u;
    frame[21] = 0x01u;
    for (u32 i = 0; i < 6u; i += 1u) {
        frame[22u + i] = net_mac[i];
        frame[32u + i] = 0u;
    }
    for (u32 i = 0; i < 4u; i += 1u) {
        frame[28u + i] = net_ip[i];
        frame[38u + i] = target_ip[i];
    }

    u8 slot = rtl8139_tx_slot;
    u8 *buf = rtl8139_tx_buffers[slot];
    u32 send_len = 60u;
    u8 tx_ok = 0u;
    if (buf) {
        mem_zero(buf, send_len);
        for (u32 i = 0; i < 42u; i += 1u) {
            buf[i] = frame[i];
        }
        __asm__ volatile ("" : : : "memory");
        outl((u16) (rtl8139_io_base + RTL_REG_TSAD0 + slot * 4u), (u32) buf);
        outl((u16) (rtl8139_io_base + RTL_REG_TSD0 + slot * 4u), send_len);
        rtl8139_tx_slot = (u8) ((slot + 1u) & 3u);
        net_tx_frames += 1u;
        tx_ok = 1u;
    }

    if (tx_ok) {
        net_set_last(last_event);
        serial_print_line(proof);
    }
    return tx_ok;
}

static void net_send_arp_request(void)
{
    if (net_send_arp_request_for(net_gateway_ip, "ARP REQUEST SENT",
                                 msg_net_arp_request)) {
        net_arp_requested = 1u;
    }
}

static void net_send_dns_arp_request(void)
{
    if (net_send_arp_request_for(net_dns_ip, "DNS ARP REQUEST",
                                 msg_net_dns_arp_request)) {
        net_dns_arp_requested = 1u;
    }
}

static void net_send_arp_reply(const u8 dst_mac[6], const u8 dst_ip[4])
{
    u8 payload[28];
    write_be16(payload + 0, 1u);
    write_be16(payload + 2, ETH_TYPE_IPV4);
    payload[4] = 6u;
    payload[5] = 4u;
    write_be16(payload + 6, 2u);
    mem_copy(payload + 8, net_mac, 6u);
    write_be32(payload + 14, net_ip);
    mem_copy(payload + 18, dst_mac, 6u);
    write_be32(payload + 24, dst_ip);
    if (net_send_frame(dst_mac, ETH_TYPE_ARP, payload, sizeof(payload))) {
        net_set_last("ARP REPLY SENT");
    }
}

static void net_send_icmp_echo_request(void)
{
    u8 payload[20u + 16u];
    u8 *ip = payload;
    u8 *icmp = payload + 20u;
    const char probe[] = "LEONOS-NET";
    u32 icmp_len = 8u + 9u;
    mem_zero(payload, sizeof(payload));
    net_write_ipv4_header(ip, (u16) (20u + icmp_len), IP_PROTO_ICMP, net_gateway_ip);
    icmp[0] = 8u;
    icmp[1] = 0u;
    write_be16(icmp + 2u, 0);
    write_be16(icmp + 4u, 0x1E0Au);
    write_be16(icmp + 6u, 1u);
    for (u32 i = 0; i < 9u; i += 1u) {
        icmp[8u + i] = (u8) probe[i];
    }
    write_be16(icmp + 2u, net_checksum(icmp, icmp_len));
    if (net_send_frame(net_gateway_mac, ETH_TYPE_IPV4, payload, 20u + icmp_len)) {
        net_icmp_probe_sent = 1u;
        net_icmp_tx += 1u;
        net_set_last("ICMP REQUEST SENT");
        serial_print_line(msg_net_icmp_request);
    }
}

static u8 net_dns_host_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '.';
}

static u32 net_dns_encode_host(const char *host, u8 *qname, u32 qname_max)
{
    if (qname_max < 2u || host[0] == 0) {
        return 0u;
    }
    u32 pos = 0u;
    u32 label_pos = pos++;
    u32 label_len = 0u;
    for (u32 i = 0u; host[i] != 0; i += 1u) {
        char ch = host[i];
        if (!net_dns_host_char(ch)) {
            return 0u;
        }
        if (ch == '.') {
            if (label_len == 0u || label_len > 63u) {
                return 0u;
            }
            qname[label_pos] = (u8) label_len;
            label_pos = pos++;
            label_len = 0u;
            if (pos >= qname_max) {
                return 0u;
            }
            continue;
        }
        if (label_len >= 63u || pos + 1u >= qname_max) {
            return 0u;
        }
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char) (ch + ('a' - 'A'));
        }
        qname[pos++] = (u8) ch;
        label_len += 1u;
    }
    if (label_len == 0u || label_len > 63u || pos >= qname_max) {
        return 0u;
    }
    qname[label_pos] = (u8) label_len;
    qname[pos++] = 0u;
    return pos;
}

static void net_send_dns_query(void)
{
    u8 *frame = net_tx_packet;
    u8 *ip = frame + 14u;
    u8 *udp = ip + 20u;
    u8 *dns = udp + 8u;
    u8 qname[NET_BROWSER_HOST_MAX + 2u];
    u32 qname_len = net_dns_encode_host(net_browser_host, qname, sizeof(qname));
    if (qname_len == 0u) {
        net_set_last("DNS HOST BAD");
        return;
    }
    u32 dns_len = 12u + qname_len + 4u;
    u32 udp_len = 8u + dns_len;
    u32 ip_len = 20u + udp_len;

    mem_zero(frame, 14u + ip_len);
    mem_copy(frame, net_dns_mac, 6u);
    mem_copy(frame + 6u, net_mac, 6u);
    write_be16(frame + 12u, ETH_TYPE_IPV4);
    net_write_ipv4_header(ip, (u16) ip_len, IP_PROTO_UDP, net_dns_ip);
    write_be16(udp + 0u, net_dns_source_port);
    write_be16(udp + 2u, 53u);
    write_be16(udp + 4u, (u16) udp_len);
    write_be16(udp + 6u, 0u);

    write_be16(dns + 0u, net_dns_txid);
    write_be16(dns + 2u, 0x0100u);
    write_be16(dns + 4u, 1u);
    write_be16(dns + 6u, 0u);
    write_be16(dns + 8u, 0u);
    write_be16(dns + 10u, 0u);
    mem_copy(dns + 12u, qname, qname_len);
    write_be16(dns + 12u + qname_len, 1u);
    write_be16(dns + 14u + qname_len, 1u);

    if (rtl8139_tx(frame, 14u + ip_len)) {
        net_dns_query_sent = 1u;
        net_dns_query_tick = system_ticks;
        net_udp_tx += 1u;
        net_set_last("DNS QUERY SENT");
        serial_print(msg_net_dns_query);
        serial_print(" ");
        serial_print(net_browser_host);
        serial_print("\r\n");
    }
}

static u32 net_append_capped(char *dst, u32 pos, u32 dst_len, const char *src);
static u8 net_text_contains_ci(const char *text, const char *needle);
static const char *net_browser_first_same_host_resource_path(void);
static u8 net_browser_first_same_host_resource_slot(void);
static void net_browser_image_prepare_resource_fetch(void);
static void net_browser_image_decode_current_resource(void);
static void net_browser_image_finalize_direct_document(void);

static void net_browser_set_resource_phase(const char *phase)
{
    u32 i = 0u;
    for (; i < 4u && phase[i] != 0; i += 1u) {
        net_browser_resource_fetch_phase[i] = phase[i];
    }
    net_browser_resource_fetch_phase[i] = 0;
}

static u8 net_tcp_send(u8 flags, const u8 *payload, u32 payload_len)
{
    u32 header_len = ((flags & TCP_FLAG_SYN) != 0u) ? 24u : 20u;
    if (!net_gateway_mac_valid || !net_dns_reply_seen ||
        payload_len > (NET_MTU - 20u - header_len)) {
        return 0u;
    }

    u8 *frame = net_tx_packet;
    u8 *ip = frame + 14u;
    u8 *tcp = ip + 20u;
    u32 tcp_len = header_len + payload_len;
    u32 ip_len = 20u + tcp_len;

    mem_zero(frame, 14u + ip_len);
    mem_copy(frame, net_gateway_mac, 6u);
    mem_copy(frame + 6u, net_mac, 6u);
    write_be16(frame + 12u, ETH_TYPE_IPV4);
    net_write_ipv4_header(ip, (u16) ip_len, IP_PROTO_TCP, net_dns_a_record);

    write_be16(tcp + 0u, NET_TCP_SOURCE_PORT);
    write_be16(tcp + 2u, 80u);
    write_be32_value(tcp + 4u, net_tcp_next_seq);
    write_be32_value(tcp + 8u, net_tcp_ack);
    tcp[12] = (u8) ((header_len / 4u) << 4);
    tcp[13] = flags;
    write_be16(tcp + 14u, NET_TCP_RECV_WINDOW);
    write_be16(tcp + 16u, 0u);
    write_be16(tcp + 18u, 0u);
    if (header_len > 20u) {
        tcp[20] = 0x02u;
        tcp[21] = 0x04u;
        write_be16(tcp + 22u, NET_TCP_MSS);
    }
    if (payload_len != 0u) {
        mem_copy(tcp + header_len, payload, payload_len);
    }
    write_be16(tcp + 16u,
        net_transport_checksum(net_ip, net_dns_a_record, IP_PROTO_TCP, tcp, tcp_len));

    if (rtl8139_tx(frame, 14u + ip_len)) {
        net_tcp_tx += 1u;
        return 1u;
    }
    return 0u;
}

static void __attribute__((unused)) net_send_tcp_syn(void)
{
    net_tcp_next_seq = NET_TCP_INITIAL_SEQ;
    net_tcp_ack = 0u;
    if (net_tcp_send(TCP_FLAG_SYN, 0, 0u)) {
        net_tcp_syn_sent = 1u;
        net_tcp_next_seq = NET_TCP_INITIAL_SEQ + 1u;
        net_set_last("TCP SYN SENT");
        serial_print_line(msg_net_tcp_syn);
    }
}

static void net_send_tcp_ack(void)
{
    (void) net_tcp_send(TCP_FLAG_ACK, 0, 0u);
}

static void __attribute__((unused)) net_send_http_get(void)
{
    char request[256];
    u32 len = 0u;
    len = net_append_capped(request, len, sizeof(request), "GET ");
    len = net_append_capped(request, len, sizeof(request), net_browser_path);
    len = net_append_capped(request, len, sizeof(request), " HTTP/1.1\r\nHost: ");
    len = net_append_capped(request, len, sizeof(request), net_browser_host);
    len = net_append_capped(request, len, sizeof(request), "\r\n\r\n");
    if (net_tcp_send((u8) (TCP_FLAG_ACK | TCP_FLAG_PSH),
                     (const u8 *) request, len)) {
        net_http_get_sent = 1u;
        net_tcp_next_seq += len;
        net_set_last("HTTP GET SENT");
        serial_print_line(msg_net_http_get);
    }
}

static void __attribute__((unused)) net_send_http_resource_get(void)
{
    const char *path = net_browser_first_same_host_resource_path();
    char request[224];
    u32 len = 0u;
    if (path == 0 || net_http_get_sent) {
        return;
    }
    len = net_append_capped(request, len, sizeof(request), "GET ");
    len = net_append_capped(request, len, sizeof(request), path);
    len = net_append_capped(request, len, sizeof(request),
        " HTTP/1.1\r\n"
        "Host: ");
    len = net_append_capped(request, len, sizeof(request), net_browser_host);
    len = net_append_capped(request, len, sizeof(request),
        "\r\n"
        "User-Agent: LeonOS/0.1\r\n"
        "Accept: ");
    u8 rslot = net_browser_first_same_host_resource_slot();
    if (rslot != 0xFFu && net_browser_resource_type[rslot] == 'I') {
        len = net_append_capped(request, len, sizeof(request),
            "image/png,image/jpeg,*/*");
    } else {
        len = net_append_capped(request, len, sizeof(request), "text/html");
    }
    len = net_append_capped(request, len, sizeof(request),
        "\r\n"
        "Accept-Encoding: identity\r\n\r\n");
    if (net_tcp_send((u8) (TCP_FLAG_ACK | TCP_FLAG_PSH),
                     (const u8 *) request, len)) {
        net_http_get_sent = 1u;
        net_tcp_next_seq += len;
        net_set_last("RESOURCE HTTP GET");
        net_browser_set_resource_phase("GET");
        serial_print(msg_net_browser_resource_get);
        serial_print(path);
        serial_print("\r\n");
    }
}

static u8 net_tls_first_sack_block(u32 *left, u32 *right)
{
    u32 found_left = 0xFFFFFFFFu;
    u32 found_right = 0u;
    for (u32 i = 0u; i < NET_TLS_OOO_MAX; i += 1u) {
        if (!net_tls_ooo_valid[i]) {
            continue;
        }
        u32 seq = net_tls_ooo_seq[i];
        u32 end = seq + net_tls_ooo_len[i];
        if (end < seq || end <= net_tls_ack) {
            continue;
        }
        if (seq < found_left) {
            found_left = seq;
            found_right = end;
        }
    }
    if (found_left == 0xFFFFFFFFu) {
        return 0u;
    }

    u8 changed = 1u;
    while (changed) {
        changed = 0u;
        for (u32 i = 0u; i < NET_TLS_OOO_MAX; i += 1u) {
            if (!net_tls_ooo_valid[i]) {
                continue;
            }
            u32 seq = net_tls_ooo_seq[i];
            u32 end = seq + net_tls_ooo_len[i];
            if (end < seq || end <= net_tls_ack) {
                continue;
            }
            if (seq <= found_right && end > found_right) {
                found_right = end;
                changed = 1u;
            }
        }
    }

    *left = found_left;
    *right = found_right;
    return 1u;
}

static u8 net_tls_send(u8 flags, const u8 *payload, u32 payload_len)
{
    u8 ack_only = (flags == TCP_FLAG_ACK && payload_len == 0u);
    u32 sack_left = 0u;
    u32 sack_right = 0u;
    u8 send_sack = ack_only &&
        net_tls_first_sack_block(&sack_left, &sack_right);
    u32 header_len = ((flags & TCP_FLAG_SYN) != 0u) ? 28u :
        (send_sack ? 32u : 20u);
    if (!net_gateway_mac_valid || !net_dns_reply_seen ||
        payload_len > (NET_MTU - 20u - header_len)) {
        return 0u;
    }

    u8 *frame = net_tx_packet;
    u8 *ip = frame + 14u;
    u8 *tcp = ip + 20u;
    u32 tcp_len = header_len + payload_len;
    u32 ip_len = 20u + tcp_len;

    mem_zero(frame, 14u + ip_len);
    mem_copy(frame, net_gateway_mac, 6u);
    mem_copy(frame + 6u, net_mac, 6u);
    write_be16(frame + 12u, ETH_TYPE_IPV4);
    net_write_ipv4_header(ip, (u16) ip_len, IP_PROTO_TCP, net_dns_a_record);

    u16 source_port = net_tls_source_port != 0u ? net_tls_source_port
                                                : NET_TLS_SOURCE_PORT;
    write_be16(tcp + 0u, source_port);
    write_be16(tcp + 2u, 443u);
    write_be32_value(tcp + 4u, net_tls_next_seq);
    write_be32_value(tcp + 8u, net_tls_ack);
    tcp[12] = (u8) ((header_len / 4u) << 4);
    tcp[13] = flags;
    write_be16(tcp + 14u, NET_TCP_RECV_WINDOW);
    write_be16(tcp + 16u, 0u);
    write_be16(tcp + 18u, 0u);
    if ((flags & TCP_FLAG_SYN) != 0u) {
        tcp[20] = 0x02u;
        tcp[21] = 0x04u;
        write_be16(tcp + 22u, NET_TCP_MSS);
        tcp[24] = 0x04u;
        tcp[25] = 0x02u;
        tcp[26] = 0x01u;
        tcp[27] = 0x01u;
    } else if (send_sack) {
        tcp[20] = 0x01u;
        tcp[21] = 0x01u;
        tcp[22] = 0x05u;
        tcp[23] = 0x0Au;
        write_be32_value(tcp + 24u, sack_left);
        write_be32_value(tcp + 28u, sack_right);
    }
    if (payload_len != 0u) {
        mem_copy(tcp + header_len, payload, payload_len);
    }
    write_be16(tcp + 16u,
        net_transport_checksum(net_ip, net_dns_a_record, IP_PROTO_TCP, tcp, tcp_len));

    if (rtl8139_tx(frame, 14u + ip_len)) {
        net_tcp_tx += 1u;
        return 1u;
    }
    return 0u;
}

static void net_send_tls_syn(void)
{
    net_tls_next_seq = NET_TLS_INITIAL_SEQ;
    net_tls_ack = 0u;
    if (net_tls_send(TCP_FLAG_SYN, 0, 0u)) {
        net_tls_syn_sent = 1u;
        net_tls_syn_tick = system_ticks;
        net_tls_next_seq = NET_TLS_INITIAL_SEQ + 1u;
        net_set_last("TLS SYN SENT");
        if (net_tls_fetch_kind == 1u) {
            serial_print_line(msg_net_browser_resource_syn);
        } else {
            serial_print_line(msg_net_tls_syn);
        }
    }
}

static void net_send_tls_ack(void)
{
    (void) net_tls_send(TCP_FLAG_ACK, 0, 0u);
}

static void net_tls_clear_out_of_order(void)
{
    for (u32 i = 0u; i < NET_TLS_OOO_MAX; i += 1u) {
        net_tls_ooo_valid[i] = 0u;
        net_tls_ooo_seq[i] = 0u;
        net_tls_ooo_len[i] = 0u;
    }
    net_tls_ooo_idle_wait_count = 0u;
    net_tls_ooo_reack_tick = 0u;
    net_tls_ooo_reack_count = 0u;
}

static u8 net_tls_out_of_order_pending(void)
{
    for (u32 i = 0u; i < NET_TLS_OOO_MAX; i += 1u) {
        if (net_tls_ooo_valid[i]) {
            return 1u;
        }
    }
    return 0u;
}

static void net_tls_reset_for_resource_fetch(void)
{
    net_tls_fetch_kind = 1u;
    if (net_tls_resource_source_port_next < NET_TLS_RESOURCE_SOURCE_PORT) {
        net_tls_resource_source_port_next = NET_TLS_RESOURCE_SOURCE_PORT;
    }
    net_tls_source_port = net_tls_resource_source_port_next;
    net_tls_resource_source_port_next += 1u;
    if (net_tls_resource_source_port_next > NET_TLS_RESOURCE_SOURCE_PORT + 128u) {
        net_tls_resource_source_port_next = NET_TLS_RESOURCE_SOURCE_PORT;
    }
    net_tls_syn_sent = 0u;
    net_tls_connected = 0u;
    net_tls_syn_retry_count = 0u;
    net_tls_syn_tick = 0u;
    net_tls_fin_pending = 0u;
    net_tls_fin_seq = 0u;
    net_tls_client_hello_sent = 0u;
    net_tls_server_hello_seen = 0u;
    net_tls_next_seq = 0u;
    net_tls_ack = 0u;
    net_tls_rx_len = 0u;
    net_tls_clear_out_of_order();
    net_tls_ooo_log_count = 0u;
    net_tls_server_hs_seq = 0u;
    net_tls_client_hs_seq = 0u;
    net_tls_server_app_seq = 0u;
    net_tls_client_app_seq = 0u;
    net_tls_handshake_keys_ready = 0u;
    net_tls_server_finished_seen = 0u;
    net_tls_client_finished_sent = 0u;
    net_https_get_sent = 0u;
    net_https_get_tick = 0u;
    net_https_response_seen = 0u;
    net_https_headers_done = 0u;
    net_https_chunked = 0u;
    net_https_body_complete = 0u;
    net_browser_finalize_sent = 0u;
    net_https_body_decoded_total32 = 0u;
    net_https_body_last_tick = 0u;
    net_https_headers_tick = 0u;
    net_https_header_len = 0u;
    net_https_header_log_sent = 0u;
    net_https_body_raw_log_count = 0u;
    net_https_body_raw_total = 0u;
    net_https_body_decoded_log_count = 0u;
    net_https_body_decoded_total = 0u;
    net_http_chunk_state = 0u;
    net_http_chunk_seen_digit = 0u;
    net_http_chunk_skip_ext = 0u;
    net_http_chunk_remaining = 0u;
    net_https_status[0] = '-';
    net_https_status[1] = '-';
    net_https_status[2] = '-';
    net_https_status[3] = 0;
}

static void net_tls_request_primary_close(void)
{
    if (net_tls_fetch_kind != 0u ||
        net_tls_primary_close_sent ||
        !net_tls_connected) {
        return;
    }
    if (net_tls_send((u8) (TCP_FLAG_ACK | TCP_FLAG_FIN), 0, 0u)) {
        net_tls_primary_close_sent = 1u;
        net_tls_primary_done = 1u;
        net_tls_primary_done_tick = system_ticks;
        net_tls_next_seq += 1u;
        net_set_last("HTTPS PRIMARY CLOSE");
    }
}

static void net_tls_abort_primary_for_retry(void)
{
    if (net_tls_fetch_kind != 0u || !net_tls_connected) {
        return;
    }
    if (net_tls_send((u8) (TCP_FLAG_ACK | TCP_FLAG_RST), 0, 0u)) {
        net_tls_connected = 0u;
        net_tls_primary_close_sent = 1u;
        net_tls_primary_done = 1u;
        net_tls_primary_done_tick = system_ticks;
        net_set_last("HTTPS PRIMARY RST");
        serial_print("LeonOS net HTTPS primary RST for retry\r\n");
    }
}

static void net_tls_abort_current_for_navigation(void)
{
    if (!net_tls_connected) {
        return;
    }
    (void) net_tls_send((u8) (TCP_FLAG_ACK | TCP_FLAG_RST), 0, 0u);
    net_tls_connected = 0u;
    net_tls_primary_close_sent = 1u;
    net_tls_primary_done = 1u;
    net_tls_primary_done_tick = system_ticks;
    net_set_last("HTTPS NAV RST");
}

static const char *net_browser_same_host_path(const char *url)
{
    if (url[0] == 0) {
        return 0;
    }
    const char scheme[] = "https://";
    u32 i = 0u;
    while (scheme[i] != 0) {
        u8 ch = (u8) url[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (u8) (ch + ('a' - 'A'));
        }
        if (url[i] == 0 || ch != (u8) scheme[i]) {
            return 0;
        }
        i += 1u;
    }
    u32 host_i = 0u;
    while (net_browser_host[host_i] != 0) {
        u8 left = (u8) url[i + host_i];
        u8 right = (u8) net_browser_host[host_i];
        if (left >= 'A' && left <= 'Z') {
            left = (u8) (left + ('a' - 'A'));
        }
        if (right >= 'A' && right <= 'Z') {
            right = (u8) (right + ('a' - 'A'));
        }
        if (url[i + host_i] == 0 || left != right) {
            return 0;
        }
        host_i += 1u;
    }
    i += host_i;
    if (url[i] == '/' || url[i] == 0) {
        return url[i] == 0 ? "/" : url + i;
    }
    return 0;
}

static const char *net_browser_first_same_host_resource_path(void)
{
    const char *active = net_browser_same_host_path(net_browser_fetch_url);
    if (active != 0) {
        return active;
    }
    for (u32 r = 0u; r < net_browser_resource_count; r += 1u) {
        const char *path = net_browser_same_host_path(net_browser_resource_url[r]);
        if (path != 0) {
            return path;
        }
    }
    return 0;
}

static void net_browser_start_resource_fetch(void)
{
    if (net_browser_resource_fetch_started ||
        net_browser_first_same_host_resource_path() == 0) {
        return;
    }
    net_browser_resource_fetch_pending = 0u;
    net_browser_resource_fetch_started = 1u;
    net_browser_resource_fetch_done = 0u;
    net_browser_resource_fetch_body_seen = 0u;
    net_browser_resource_fetch_bytes = 0u;
    net_browser_resource_fetch_start_tick = system_ticks;
    net_browser_resource_fetch_status[0] = '-';
    net_browser_resource_fetch_status[1] = '-';
    net_browser_resource_fetch_status[2] = '-';
    net_browser_resource_fetch_status[3] = 0;
    net_browser_set_resource_phase("SYN");
    net_browser_image_prepare_resource_fetch();
    net_tls_reset_for_resource_fetch();
    net_set_last("RESOURCE FETCH QUEUED");
}

static void net_browser_request_resource_fetch(void)
{
    if (!net_browser_resource_fetch_started &&
        net_browser_first_same_host_resource_path() != 0) {
        net_browser_fetch_url[0] = 0;
        net_browser_resource_fetch_pending = 1u;
        net_browser_set_resource_phase("PEND");
    }
}

static void net_browser_finish_resource_fetch(void)
{
    if (!net_browser_resource_fetch_started ||
        net_browser_resource_fetch_done) {
        return;
    }
    net_browser_resource_fetch_done = 1u;
    if (net_browser_resource_fetch_status[0] == '-') {
        net_browser_resource_fetch_status[0] = 'E';
        net_browser_resource_fetch_status[1] = 'O';
        net_browser_resource_fetch_status[2] = 'F';
        net_browser_resource_fetch_status[3] = 0;
        serial_print(msg_net_browser_resource_status_prefix);
        serial_print(net_browser_resource_fetch_status);
        serial_print("\r\n");
    }
    if (net_browser_resource_fetch_status[0] != 'T') {
        net_browser_set_resource_phase("DONE");
    }
    if (net_browser_resource_fetch_status[0] == '2') {
        net_browser_image_decode_current_resource();
    }
    serial_print(msg_net_browser_resource_done_prefix);
    serial_print(net_browser_resource_fetch_status);
    serial_print(" bytes ");
    serial_print_dec(net_browser_resource_fetch_bytes);
    serial_print("\r\n");
    net_set_last("RESOURCE FETCH DONE");
}

static void net_browser_timeout_resource_fetch(void)
{
    if (!net_browser_resource_fetch_started ||
        net_browser_resource_fetch_done ||
        system_ticks - net_browser_resource_fetch_start_tick < 3000u) {
        return;
    }
    if (net_browser_resource_fetch_status[0] == '-') {
        net_browser_resource_fetch_status[0] = 'T';
        net_browser_resource_fetch_status[1] = 'M';
        net_browser_resource_fetch_status[2] = 'O';
        net_browser_resource_fetch_status[3] = 0;
        net_browser_set_resource_phase("TMO");
        serial_print(msg_net_browser_resource_status_prefix);
        serial_print(net_browser_resource_fetch_status);
        serial_print("\r\n");
    }
    net_browser_finish_resource_fetch();
}

static void net_send_tls_client_hello(void)
{
    u8 hello[288];
    u32 p = 0u;
    u32 host_len = 0u;
    const u8 random[32] = {
        0x1Eu, 0x0Au, 0x05u, 0x20u, 0x26u, 0x06u, 0x02u, 0x01u,
        0x4Cu, 0x45u, 0x4Fu, 0x4Eu, 0x4Fu, 0x53u, 0x54u, 0x4Cu,
        0x53u, 0x47u, 0x4Fu, 0x4Fu, 0x47u, 0x4Cu, 0x45u, 0x21u,
        0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u, 0x80u
    };
    const u8 session[32] = {
        0x4Cu, 0x45u, 0x4Fu, 0x4Eu, 0x4Fu, 0x53u, 0x20u, 0x54u,
        0x4Cu, 0x53u, 0x20u, 0x53u, 0x45u, 0x53u, 0x53u, 0x49u,
        0x4Fu, 0x4Eu, 0x20u, 0x47u, 0x4Fu, 0x4Fu, 0x47u, 0x4Cu,
        0x45u, 0x20u, 0x30u, 0x30u, 0x30u, 0x30u, 0x30u, 0x31u
    };
    const u8 x25519_public[32] = {
        0x85u, 0x20u, 0xF0u, 0x09u, 0x89u, 0x30u, 0xA7u, 0x54u,
        0x74u, 0x8Bu, 0x7Du, 0xDCu, 0xB4u, 0x3Eu, 0xF7u, 0x5Au,
        0x0Du, 0xBFu, 0x3Au, 0x0Du, 0x26u, 0x38u, 0x1Au, 0xF4u,
        0xEBu, 0xA4u, 0xA9u, 0x8Eu, 0xAAu, 0x9Bu, 0x4Eu, 0x6Au
    };

    while (net_browser_host[host_len] != 0u) {
        host_len += 1u;
    }
    if (host_len == 0u || host_len > NET_BROWSER_HOST_MAX - 1u) {
        net_set_last("TLS HOST BAD");
        return;
    }

    mem_zero(hello, sizeof(hello));
    hello[p++] = 0x16u; hello[p++] = 0x03u; hello[p++] = 0x01u;
    u32 record_len_pos = p; p += 2u;
    hello[p++] = 0x01u;
    u32 handshake_len_pos = p; p += 3u;
    u32 handshake_body_start = p;
    hello[p++] = 0x03u; hello[p++] = 0x03u;
    mem_copy(hello + p, random, 32u);
    if (net_tls_fetch_kind == 1u) {
        hello[p + 28u] = 'R';
        hello[p + 29u] = 'E';
        hello[p + 30u] = 'S';
        hello[p + 31u] = '1';
    }
    p += 32u;
    hello[p++] = 32u;
    mem_copy(hello + p, session, 32u);
    if (net_tls_fetch_kind == 1u) {
        hello[p + 28u] = 'R';
        hello[p + 29u] = 'E';
        hello[p + 30u] = 'S';
        hello[p + 31u] = '1';
    }
    p += 32u;
    write_be16(hello + p, 2u); p += 2u;
    write_be16(hello + p, 0x1303u); p += 2u;
    hello[p++] = 1u; hello[p++] = 0u;
    u32 extension_len_pos = p; p += 2u;
    u32 extension_start = p;

    write_be16(hello + p, 0x0000u); p += 2u;
    write_be16(hello + p, (u16) (host_len + 5u)); p += 2u;
    write_be16(hello + p, (u16) (host_len + 3u)); p += 2u;
    hello[p++] = 0u;
    write_be16(hello + p, (u16) host_len); p += 2u;
    if (p + host_len + 72u > sizeof(hello)) {
        net_set_last("TLS HELLO BIG");
        return;
    }
    for (u32 i = 0u; i < host_len; i += 1u) {
        hello[p++] = (u8) net_browser_host[i];
    }

    write_be16(hello + p, 0x002Bu); p += 2u;
    write_be16(hello + p, 3u); p += 2u;
    hello[p++] = 2u; write_be16(hello + p, 0x0304u); p += 2u;

    write_be16(hello + p, 0x000Au); p += 2u;
    write_be16(hello + p, 4u); p += 2u;
    write_be16(hello + p, 2u); p += 2u;
    write_be16(hello + p, 0x001Du); p += 2u;

    write_be16(hello + p, 0x000Du); p += 2u;
    write_be16(hello + p, 10u); p += 2u;
    write_be16(hello + p, 8u); p += 2u;
    write_be16(hello + p, 0x0403u); p += 2u;
    write_be16(hello + p, 0x0804u); p += 2u;
    write_be16(hello + p, 0x0401u); p += 2u;
    write_be16(hello + p, 0x0807u); p += 2u;

    write_be16(hello + p, 0x0033u); p += 2u;
    write_be16(hello + p, 38u); p += 2u;
    write_be16(hello + p, 36u); p += 2u;
    write_be16(hello + p, 0x001Du); p += 2u;
    write_be16(hello + p, 32u); p += 2u;
    mem_copy(hello + p, x25519_public, 32u); p += 32u;

    write_be16(hello + p, 0x0010u); p += 2u;
    write_be16(hello + p, 11u); p += 2u;
    write_be16(hello + p, 9u); p += 2u;
    hello[p++] = 8u; hello[p++] = 'h'; hello[p++] = 't'; hello[p++] = 't';
    hello[p++] = 'p'; hello[p++] = '/'; hello[p++] = '1'; hello[p++] = '.';
    hello[p++] = '1';

    u32 extension_len = p - extension_start;
    u32 handshake_len = p - handshake_body_start;
    if (extension_len > 65535u || handshake_len > 0xFFFFFFu || p < 5u) {
        net_set_last("TLS HELLO LEN");
        return;
    }
    write_be16(hello + extension_len_pos, (u16) extension_len);
    hello[handshake_len_pos + 0u] = (u8) (handshake_len >> 16);
    hello[handshake_len_pos + 1u] = (u8) (handshake_len >> 8);
    hello[handshake_len_pos + 2u] = (u8) handshake_len;
    write_be16(hello + record_len_pos, (u16) (p - 5u));

    if (net_tls_send((u8) (TCP_FLAG_ACK | TCP_FLAG_PSH), hello, p)) {
        net_tls_client_hello_msg_len = p - 5u;
        mem_copy(net_tls_client_hello_msg, hello + 5u, net_tls_client_hello_msg_len);
        sha256_init(&net_tls_transcript);
        sha256_update(&net_tls_transcript, net_tls_client_hello_msg,
                      net_tls_client_hello_msg_len);
        net_tls_client_hello_sent = 1u;
        net_tls_next_seq += p;
        net_set_last("TLS HELLO SENT");
        if (net_tls_fetch_kind == 1u) {
            net_browser_set_resource_phase("TLS");
        }
        serial_print_line(msg_net_tls_client_hello);
    }
}

static void net_tls_transcript_hash(u8 out[32])
{
    struct Sha256Ctx copy = net_tls_transcript;
    sha256_final(&copy, out);
}

static u8 net_tls_parse_server_hello(const u8 *msg, u32 msg_len,
                                     u8 server_pub[32])
{
    if (msg_len < 44u || msg[0] != 0x02u) {
        return 0u;
    }
    u32 body_len = ((u32) msg[1] << 16) | ((u32) msg[2] << 8) | msg[3];
    if (body_len + 4u > msg_len) {
        return 0u;
    }
    u32 p = 4u;
    if (read_be16(msg + p) != 0x0303u) {
        return 0u;
    }
    p += 2u + 32u;
    if (p >= msg_len) {
        return 0u;
    }
    u32 sid_len = msg[p++];
    if (p + sid_len + 4u > msg_len) {
        return 0u;
    }
    p += sid_len;
    if (read_be16(msg + p) != 0x1303u) {
        net_set_last("TLS CIPHER BAD");
        return 0u;
    }
    p += 2u;
    if (msg[p++] != 0u || p + 2u > msg_len) {
        return 0u;
    }
    u32 ext_len = read_be16(msg + p);
    p += 2u;
    u32 ext_end = p + ext_len;
    if (ext_end > msg_len) {
        return 0u;
    }
    while (p + 4u <= ext_end) {
        u16 ext_type = read_be16(msg + p);
        u32 ext_len = read_be16(msg + p + 2u);
        p += 4u;
        if (p + ext_len > ext_end) {
            return 0u;
        }
        if (ext_type == 0x0033u && ext_len >= 36u &&
            read_be16(msg + p) == 0x001Du &&
            read_be16(msg + p + 2u) == 32u) {
            mem_copy(server_pub, msg + p + 4u, 32u);
            return 1u;
        }
        p += ext_len;
    }
    return 0u;
}

static void net_tls_derive_handshake_keys(const u8 server_pub[32])
{
    static const u8 client_private[32] = {
        0x77u, 0x07u, 0x6Du, 0x0Au, 0x73u, 0x18u, 0xA5u, 0x7Du,
        0x3Cu, 0x16u, 0xC1u, 0x72u, 0x51u, 0xB2u, 0x66u, 0x45u,
        0xDFu, 0x4Cu, 0x2Fu, 0x87u, 0xEBu, 0xC0u, 0x99u, 0x2Au,
        0xB1u, 0x77u, 0xFBu, 0xA5u, 0x1Du, 0xB9u, 0x2Cu, 0x2Au
    };
    u8 zero[32];
    u8 empty_hash[32];
    u8 early_secret[32];
    u8 derived_secret[32];
    u8 shared_secret[32];
    u8 transcript_hash[32];
    u8 server_hs_secret[32];
    u8 client_hs_secret[32];

    mem_zero(zero, sizeof(zero));
    sha256_hash(zero, 0u, empty_hash);
    hkdf_extract(zero, 32u, zero, 32u, early_secret);
    tls_derive_secret(early_secret, "derived", empty_hash, derived_secret);
    x25519_scalarmult(shared_secret, client_private, server_pub);
    hkdf_extract(derived_secret, 32u, shared_secret, 32u, net_tls_handshake_secret);

    net_tls_transcript_hash(transcript_hash);
    tls_derive_secret(net_tls_handshake_secret, "s hs traffic", transcript_hash,
                      server_hs_secret);
    tls_derive_secret(net_tls_handshake_secret, "c hs traffic", transcript_hash,
                      client_hs_secret);

    tls_hkdf_expand_label(server_hs_secret, "key", 0, 0u, net_tls_server_hs_key, 32u);
    tls_hkdf_expand_label(server_hs_secret, "iv", 0, 0u, net_tls_server_hs_iv, 12u);
    tls_hkdf_expand_label(client_hs_secret, "key", 0, 0u, net_tls_client_hs_key, 32u);
    tls_hkdf_expand_label(client_hs_secret, "iv", 0, 0u, net_tls_client_hs_iv, 12u);
    tls_hkdf_expand_label(server_hs_secret, "finished", 0, 0u,
                          net_tls_server_finished_key, 32u);
    tls_hkdf_expand_label(client_hs_secret, "finished", 0, 0u,
                          net_tls_client_finished_key, 32u);

    net_tls_server_hs_seq = 0u;
    net_tls_client_hs_seq = 0u;
    net_tls_handshake_keys_ready = 1u;
    net_set_last("TLS KEYS READY");
    serial_print_line(msg_net_tls_keys_ready);
}

static void net_tls_derive_application_keys(void)
{
    u8 zero[32];
    u8 empty_hash[32];
    u8 derived_secret[32];
    u8 master_secret[32];
    u8 transcript_hash[32];
    u8 server_app_secret[32];
    u8 client_app_secret[32];

    mem_zero(zero, sizeof(zero));
    sha256_hash(zero, 0u, empty_hash);
    tls_derive_secret(net_tls_handshake_secret, "derived", empty_hash, derived_secret);
    hkdf_extract(derived_secret, 32u, zero, 32u, master_secret);
    net_tls_transcript_hash(transcript_hash);
    tls_derive_secret(master_secret, "s ap traffic", transcript_hash,
                      server_app_secret);
    tls_derive_secret(master_secret, "c ap traffic", transcript_hash,
                      client_app_secret);
    tls_hkdf_expand_label(server_app_secret, "key", 0, 0u, net_tls_server_app_key, 32u);
    tls_hkdf_expand_label(server_app_secret, "iv", 0, 0u, net_tls_server_app_iv, 12u);
    tls_hkdf_expand_label(client_app_secret, "key", 0, 0u, net_tls_client_app_key, 32u);
    tls_hkdf_expand_label(client_app_secret, "iv", 0, 0u, net_tls_client_app_iv, 12u);
    net_tls_server_app_seq = 0u;
    net_tls_client_app_seq = 0u;
}

static u32 net_tls_inner_content_len(u8 *plain, u32 plain_len, u8 *inner_type)
{
    while (plain_len != 0u) {
        plain_len -= 1u;
        if (plain[plain_len] != 0u) {
            *inner_type = plain[plain_len];
            return plain_len;
        }
    }
    *inner_type = 0u;
    return 0u;
}

static void net_tls_send_encrypted_finished(void)
{
    u8 transcript_hash[32];
    u8 verify[32];
    u8 finished_msg[36];
    u8 record[96];
    u8 aad[5];
    if (net_tls_client_finished_sent) {
        return;
    }

    net_tls_transcript_hash(transcript_hash);
    hmac_sha256(net_tls_client_finished_key, 32u, transcript_hash, 32u, verify);
    finished_msg[0] = 0x14u;
    finished_msg[1] = 0x00u; finished_msg[2] = 0x00u; finished_msg[3] = 0x20u;
    mem_copy(finished_msg + 4u, verify, 32u);

    record[0] = 0x17u; record[1] = 0x03u; record[2] = 0x03u;
    mem_copy(record + 5u, finished_msg, 36u);
    record[41u] = 0x16u;
    aad[0] = 0x17u; aad[1] = 0x03u; aad[2] = 0x03u; aad[3] = 0u; aad[4] = 0u;
    u32 cipher_len = chacha20_poly1305_encrypt(net_tls_client_hs_key,
        net_tls_client_hs_iv, net_tls_client_hs_seq, aad, record + 5u, 37u);
    net_tls_client_hs_seq += 1u;
    mem_copy(record, aad, 5u);
    if (net_tls_send((u8) (TCP_FLAG_ACK | TCP_FLAG_PSH), record, 5u + cipher_len)) {
        net_tls_next_seq += 5u + cipher_len;
        net_tls_client_finished_sent = 1u;
        sha256_update(&net_tls_transcript, finished_msg, 36u);
        net_set_last("TLS FINISHED SENT");
        serial_print_line(msg_net_tls_finished);
    }
}

static u32 net_append_capped(char *dst, u32 pos, u32 dst_len, const char *src);
static u32 net_append_u32_capped(char *dst, u32 pos, u32 dst_len, u32 value);
static u32 net_cookie_append_request_header(char *dst, u32 pos, u32 dst_len);

static u8 net_tls_send_application_data(const u8 *plain, u32 plain_len)
{
    u32 off = 0u;
    u8 sent_any = 0u;

    if (plain_len == 0u) {
        return 0u;
    }

    while (off < plain_len) {
        u32 chunk = plain_len - off;
        if (chunk > NET_TLS_APP_CHUNK_MAX) {
            chunk = NET_TLS_APP_CHUNK_MAX;
        }

        u8 *record = net_tls_app_record_buf;
        u8 aad[5];
        record[0] = 0x17u;
        record[1] = 0x03u;
        record[2] = 0x03u;
        mem_copy(record + 5u, plain + off, chunk);
        record[5u + chunk] = 0x17u;
        aad[0] = 0x17u;
        aad[1] = 0x03u;
        aad[2] = 0x03u;
        aad[3] = 0u;
        aad[4] = 0u;
        u32 cipher_len = chacha20_poly1305_encrypt(net_tls_client_app_key,
            net_tls_client_app_iv, net_tls_client_app_seq, aad,
            record + 5u, chunk + 1u);
        mem_copy(record, aad, 5u);

        u8 flags = TCP_FLAG_ACK;
        if (off + chunk >= plain_len) {
            flags |= TCP_FLAG_PSH;
        }
        if (!net_tls_send(flags, record, 5u + cipher_len)) {
            if (sent_any) {
                serial_print("LeonOS net HTTPS GET partial send fail\r\n");
            }
            return 0u;
        }
        net_tls_client_app_seq += 1u;
        net_tls_next_seq += 5u + cipher_len;
        sent_any = 1u;
        off += chunk;
    }

    return 1u;
}

static void net_tls_send_https_get(void)
{
    char *request = net_https_request_buf;
    const char *path = net_browser_path;
    const char *method = "GET";
    u8 user_primary_fetch = net_user_fetch_active && net_tls_fetch_kind == 0u;
    u32 request_body_len = 0u;
    if (net_tls_fetch_kind == 1u) {
        path = net_browser_first_same_host_resource_path();
        if (path == 0) {
            return;
        }
    }
    if (path == 0 || path[0] == 0) {
        path = "/";
    }
    if (user_primary_fetch && net_user_request_method[0] != 0) {
        method = net_user_request_method;
        request_body_len = net_user_request_body_len;
    }
    u8 method_is_get = method[0] == 'G' && method[1] == 'E' &&
                       method[2] == 'T' && method[3] == 0;
    u8 method_is_head = method[0] == 'H' && method[1] == 'E' &&
                        method[2] == 'A' && method[3] == 'D' &&
                        method[4] == 0;
    u32 len = 0u;
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX, method);
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX, " ");
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX, path);
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
        " HTTP/1.1\r\n"
        "Host: ");
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX, net_browser_host);
    len = net_cookie_append_request_header(request, len, NET_HTTPS_REQUEST_MAX);
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
        "\r\n"
        "User-Agent: Mozilla/5.0 (LeonOS; i386) NetSurf/3.11\r\n"
        "Accept: ");
    u8 rslot = net_tls_fetch_kind == 1u ? net_browser_first_same_host_resource_slot()
                                        : 0xFFu;
    if (user_primary_fetch && net_user_request_accept[0] != 0) {
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            net_user_request_accept);
    } else if (net_user_fetch_active &&
        (net_text_contains_ci(path, ".css") ||
         net_text_contains_ci(path, "/_/ss/") ||
         net_text_contains_ci(path, "/css"))) {
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            "text/css,*/*");
    } else if (net_user_fetch_active &&
               (net_text_contains_ci(path, ".js") ||
                net_text_contains_ci(path, "/_/js/"))) {
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            "application/javascript,text/javascript,*/*");
    } else if ((rslot != 0xFFu && net_browser_resource_type[rslot] == 'I') ||
               (net_user_fetch_active &&
                (net_text_contains_ci(path, ".png") ||
                 net_text_contains_ci(path, ".jpg") ||
                 net_text_contains_ci(path, ".jpeg") ||
                 net_text_contains_ci(path, ".gif") ||
                 net_text_contains_ci(path, ".ico")))) {
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            "image/png,image/jpeg,*/*");
    } else {
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            "text/html,application/xhtml+xml");
    }
    if (user_primary_fetch &&
        (request_body_len != 0u || (!method_is_get && !method_is_head))) {
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            "\r\nContent-Type: ");
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            net_user_request_content_type[0] != 0 ?
            net_user_request_content_type : "application/octet-stream");
        len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
            "\r\nContent-Length: ");
        len = net_append_u32_capped(request, len, NET_HTTPS_REQUEST_MAX,
                                   request_body_len);
    }
    len = net_append_capped(request, len, NET_HTTPS_REQUEST_MAX,
        "\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n");
    if (net_https_get_sent) {
        return;
    }
    if (len + 1u >= sizeof(net_https_request_buf)) {
        serial_print("LeonOS net HTTPS GET request too long\r\n");
        return;
    }
    u8 sent = net_tls_send_application_data((const u8 *) request, len);
    if (sent && request_body_len != 0u) {
        sent = net_tls_send_application_data(net_user_request_body,
                                             request_body_len);
    }
    if (sent) {
        net_https_get_sent = 1u;
        net_https_get_tick = system_ticks;
        if (net_tls_fetch_kind == 1u) {
            net_set_last("RESOURCE GET SENT");
            net_browser_set_resource_phase("GET");
            serial_print(msg_net_browser_resource_get);
            serial_print(path);
            serial_print("\r\n");
        } else {
            net_set_last("HTTPS GET SENT");
            if (method_is_get && request_body_len == 0u) {
                serial_print_line(msg_net_https_get);
            } else {
                serial_print("LeonOS net HTTPS request sent ");
                serial_print(method);
                serial_print(" body ");
                serial_print_dec(request_body_len);
                serial_print("\r\n");
            }
        }
    }
}

static u8 net_ascii_lower(u8 ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (u8) (ch + ('a' - 'A'));
    }
    return ch;
}

static void net_browser_render_reset(void);
static void net_browser_css_reset(void);
static void net_browser_dom_reset(void);
static void net_browser_js_reset(void);
static void net_browser_image_reset(void);
static void net_browser_image_reset_resource_body(void);
static void net_browser_image_prepare_resource_fetch(void);
static void net_browser_image_note_resource_body(const u8 *data, u32 len);
static void net_browser_image_note_direct_body(const u8 *data, u32 len);
static void net_browser_image_decode_current_resource(void);
static u8 net_browser_image_format_from_url(const char *url);
static void net_browser_image_start_direct_document(u8 format);
static void net_browser_image_finalize_direct_document(void);
static u8 net_browser_image_add_pre_normalized(const char *url, const char *alt);
static void net_browser_image_bind_resource_url(u8 image, const char *url);
static u8 net_browser_render_add_image_block(u8 image_index);
static void net_browser_set_state(const char *state);
static void net_browser_history_save_scroll(void);
static void net_browser_history_record(const char *url);
static void net_browser_open_url(const char *url);
static void net_browser_open_url_internal(const char *url, u8 add_history,
                                          u8 restore_scroll);
static u8 net_browser_open_local_history_url(const char *url);
static void net_browser_open_layout_selftest(void);
static void net_browser_open_image_selftest(void);
static void net_browser_open_css_selftest(void);
static void net_browser_open_unsupported_selftest(void);
static void net_browser_open_port_status_page(void);
static u8 net_browser_first_same_host_resource_slot(void);
static void net_browser_html_feed(const u8 *html, u32 len);
static u8 net_text_is(const char *left, const char *right);
static u32 net_cstr_len(const char *text, u32 max_len);

static u8 net_browser_css_render_flags(u16 style_flags)
{
    u8 flags = 0u;
    if ((style_flags & NET_BROWSER_CSS_STYLE_DISPLAY_NONE) != 0u) {
        flags |= NET_BROWSER_RENDER_FLAG_HIDDEN;
    }
    if ((style_flags & NET_BROWSER_CSS_STYLE_FONT_BOLD) != 0u) {
        flags |= NET_BROWSER_RENDER_FLAG_BOLD;
    }
    if ((style_flags & NET_BROWSER_CSS_STYLE_FONT_LARGE) != 0u) {
        flags |= NET_BROWSER_RENDER_FLAG_LARGE;
    }
    if ((style_flags & NET_BROWSER_CSS_STYLE_CENTER) != 0u) {
        flags |= NET_BROWSER_RENDER_FLAG_CENTER;
    }
    if ((style_flags & (NET_BROWSER_CSS_STYLE_BG |
                        NET_BROWSER_CSS_STYLE_WIDTH |
                        NET_BROWSER_CSS_STYLE_HEIGHT |
                        NET_BROWSER_CSS_STYLE_MARGIN |
                        NET_BROWSER_CSS_STYLE_PADDING |
                        NET_BROWSER_CSS_STYLE_BORDER |
                        NET_BROWSER_CSS_STYLE_DISPLAY_BLOCK)) != 0u) {
        flags |= NET_BROWSER_RENDER_FLAG_BOX;
    }
    return flags;
}

static void net_browser_css_clear_render_slot(u8 slot)
{
    if (slot >= NET_BROWSER_RENDER_MAX) {
        return;
    }
    net_browser_render_css_flags[slot] = 0u;
    net_browser_render_css_color[slot] = 0u;
    net_browser_render_css_bg[slot] = 0u;
    net_browser_render_css_width[slot] = 0u;
    net_browser_render_css_height[slot] = 0u;
    net_browser_render_css_margin[slot] = 0u;
    net_browser_render_css_padding[slot] = 0u;
    net_browser_render_css_border[slot] = 0u;
}

static void net_browser_css_copy_active_to_slot(u8 slot)
{
    if (slot >= NET_BROWSER_RENDER_MAX) {
        return;
    }
    net_browser_render_css_flags[slot] = net_browser_render_active_css_flags;
    net_browser_render_css_color[slot] = net_browser_render_active_css_color;
    net_browser_render_css_bg[slot] = net_browser_render_active_css_bg;
    net_browser_render_css_width[slot] = net_browser_render_active_css_width;
    net_browser_render_css_height[slot] = net_browser_render_active_css_height;
    net_browser_render_css_margin[slot] = net_browser_render_active_css_margin;
    net_browser_render_css_padding[slot] = net_browser_render_active_css_padding;
    net_browser_render_css_border[slot] = net_browser_render_active_css_border;
}

static void net_browser_css_apply_tag_to_slot(u8 slot)
{
    if (slot >= NET_BROWSER_RENDER_MAX) {
        return;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_COLOR) != 0u) {
        net_browser_render_css_color[slot] = net_browser_render_tag_css_color;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_BG) != 0u) {
        net_browser_render_css_bg[slot] = net_browser_render_tag_css_bg;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_WIDTH) != 0u) {
        net_browser_render_css_width[slot] = net_browser_render_tag_css_width;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_HEIGHT) != 0u) {
        net_browser_render_css_height[slot] = net_browser_render_tag_css_height;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_MARGIN) != 0u) {
        net_browser_render_css_margin[slot] = net_browser_render_tag_css_margin;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_PADDING) != 0u) {
        net_browser_render_css_padding[slot] = net_browser_render_tag_css_padding;
    }
    if ((net_browser_render_tag_css_flags & NET_BROWSER_CSS_STYLE_BORDER) != 0u) {
        net_browser_render_css_border[slot] = net_browser_render_tag_css_border;
    }
    net_browser_render_css_flags[slot] |= net_browser_render_tag_css_flags;
    net_browser_render_flags[slot] |= net_browser_css_render_flags(net_browser_render_tag_css_flags);
}

static void net_browser_css_clear_tag_style(void)
{
    net_browser_render_tag_css_flags = 0u;
    net_browser_render_tag_css_color = 0u;
    net_browser_render_tag_css_bg = 0u;
    net_browser_render_tag_css_width = 0u;
    net_browser_render_tag_css_height = 0u;
    net_browser_render_tag_css_margin = 0u;
    net_browser_render_tag_css_padding = 0u;
    net_browser_render_tag_css_border = 0u;
}

static void net_browser_css_clear_inline_style(void)
{
    net_browser_inline_css_flags = 0u;
    net_browser_inline_css_color = 0u;
    net_browser_inline_css_bg = 0u;
    net_browser_inline_css_width = 0u;
    net_browser_inline_css_height = 0u;
    net_browser_inline_css_margin = 0u;
    net_browser_inline_css_padding = 0u;
    net_browser_inline_css_border = 0u;
}

static void net_browser_css_apply_to_tag(u16 flags, u32 color, u32 bg,
                                         u16 width, u16 height,
                                         u16 margin, u16 padding,
                                         u16 border)
{
    if ((flags & NET_BROWSER_CSS_STYLE_COLOR) != 0u) {
        net_browser_render_tag_css_color = color;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_BG) != 0u) {
        net_browser_render_tag_css_bg = bg;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_WIDTH) != 0u) {
        net_browser_render_tag_css_width = width;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_HEIGHT) != 0u) {
        net_browser_render_tag_css_height = height;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_MARGIN) != 0u) {
        net_browser_render_tag_css_margin = margin;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_PADDING) != 0u) {
        net_browser_render_tag_css_padding = padding;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_BORDER) != 0u) {
        net_browser_render_tag_css_border = border;
    }
    net_browser_render_tag_css_flags |= flags;
}

static void net_browser_css_count_applied(u16 flags)
{
    if ((flags & NET_BROWSER_CSS_STYLE_COLOR) != 0u &&
        net_browser_css_color_apply_count < 65535u) {
        net_browser_css_color_apply_count += 1u;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_BG) != 0u &&
        net_browser_css_background_apply_count < 65535u) {
        net_browser_css_background_apply_count += 1u;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_WIDTH) != 0u &&
        net_browser_css_width_apply_count < 65535u) {
        net_browser_css_width_apply_count += 1u;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_HEIGHT) != 0u &&
        net_browser_css_height_apply_count < 65535u) {
        net_browser_css_height_apply_count += 1u;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_MARGIN) != 0u &&
        net_browser_css_margin_apply_count < 65535u) {
        net_browser_css_margin_apply_count += 1u;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_PADDING) != 0u &&
        net_browser_css_padding_apply_count < 65535u) {
        net_browser_css_padding_apply_count += 1u;
    }
    if ((flags & NET_BROWSER_CSS_STYLE_BORDER) != 0u &&
        net_browser_css_border_apply_count < 65535u) {
        net_browser_css_border_apply_count += 1u;
    }
}

static void net_browser_text_reset(void)
{
    const char title[] = "HTTPS PAGE\n\n";
    net_browser_text_len = 0u;
    net_browser_scroll = 0u;
    net_browser_text_ready = 0u;
    net_browser_status[0] = '-';
    net_browser_status[1] = '-';
    net_browser_status[2] = '-';
    net_browser_status[3] = 0;
    net_browser_line[0] = 'W';
    net_browser_line[1] = 'A';
    net_browser_line[2] = 'I';
    net_browser_line[3] = 'T';
    net_browser_line[4] = 'I';
    net_browser_line[5] = 'N';
    net_browser_line[6] = 'G';
    net_browser_line[7] = ' ';
    net_browser_line[8] = 'H';
    net_browser_line[9] = 'T';
    net_browser_line[10] = 'T';
    net_browser_line[11] = 'P';
    net_browser_line[12] = 'S';
    net_browser_line[13] = 0;
    net_https_headers_done = 0u;
    net_https_chunked = 0u;
    net_https_header_len = 0u;
    net_https_header_log_sent = 0u;
    net_https_body_raw_log_count = 0u;
    net_https_body_raw_total = 0u;
    net_https_body_decoded_log_count = 0u;
    net_https_body_decoded_total = 0u;
    net_https_body_complete = 0u;
    net_browser_finalize_sent = 0u;
    net_https_body_decoded_total32 = 0u;
    net_https_body_last_tick = 0u;
    net_https_headers_tick = 0u;
    net_http_chunk_state = 0u;
    net_http_chunk_seen_digit = 0u;
    net_http_chunk_skip_ext = 0u;
    net_http_chunk_remaining = 0u;
    net_tls_primary_done = 0u;
    net_tls_primary_close_sent = 0u;
    net_tls_primary_done_tick = 0u;
    net_browser_html_in_tag = 0u;
    net_browser_html_skip = 0u;
    net_browser_html_entity = 0u;
    net_browser_html_tag_len = 0u;
    net_browser_html_tag_done = 0u;
    net_browser_html_ignore_tag = 0u;
    net_browser_html_attr_state = 0u;
    net_browser_html_attr_quote = 0u;
    net_browser_html_attr_name_len = 0u;
    net_browser_html_attr_value_len = 0u;
    net_browser_html_entity_len = 0u;
    net_browser_html_structure_sent = 0u;
    net_browser_tag_count = 0u;
    net_browser_attr_count = 0u;
    net_browser_link_count = 0u;
    net_browser_anchor_count = 0u;
    net_browser_script_count = 0u;
    net_browser_style_count = 0u;
    net_browser_image_count = 0u;
    net_browser_form_count = 0u;
    net_browser_input_count = 0u;
    net_browser_button_count = 0u;
    net_browser_select_count = 0u;
    net_browser_textarea_count = 0u;
    net_browser_table_count = 0u;
    net_browser_table_cell_count = 0u;
    net_browser_meta_count = 0u;
    net_browser_heading_count = 0u;
    net_browser_svg_count = 0u;
    net_browser_svg_shape_count = 0u;
    net_browser_picture_count = 0u;
    net_browser_media_count = 0u;
    net_browser_custom_count = 0u;
    net_browser_dialog_count = 0u;
    net_browser_template_count = 0u;
    net_browser_noscript_count = 0u;
    net_browser_aria_attr_count = 0u;
    net_browser_data_attr_count = 0u;
    net_browser_js_attr_count = 0u;
    net_browser_srcset_count = 0u;
    net_browser_css_bytes = 0u;
    net_browser_css_rule_count = 0u;
    net_browser_css_decl_count = 0u;
    net_browser_css_color_count = 0u;
    net_browser_css_font_count = 0u;
    net_browser_css_url_count = 0u;
    net_browser_css_display_none_count = 0u;
    net_browser_css_stored_rule_count = 0u;
    net_browser_css_matched_rule_count = 0u;
    net_browser_css_cascade_apply_count = 0u;
    net_browser_css_color_apply_count = 0u;
    net_browser_css_background_apply_count = 0u;
    net_browser_css_width_apply_count = 0u;
    net_browser_css_height_apply_count = 0u;
    net_browser_css_margin_apply_count = 0u;
    net_browser_css_padding_apply_count = 0u;
    net_browser_css_border_apply_count = 0u;
    net_browser_js_bytes = 0u;
    net_browser_js_token_count = 0u;
    net_browser_js_function_count = 0u;
    net_browser_js_var_count = 0u;
    net_browser_js_document_count = 0u;
    net_browser_js_window_count = 0u;
    net_browser_js_location_count = 0u;
    net_browser_js_write_count = 0u;
    net_browser_event_attr_count = 0u;
    net_browser_js_url_count = 0u;
    net_browser_unsupported_feature_count = 0u;
    net_browser_js_required = 0u;
    net_browser_unsupported_summary_added = 0u;
    net_browser_dom_count = 0u;
    net_browser_dom_text_bytes = 0u;
    net_browser_dom_hidden_count = 0u;
    net_browser_dom_supported_count = 0u;
    net_browser_dom_block_count = 0u;
    net_browser_dom_inline_count = 0u;
    net_browser_dom_table_count = 0u;
    net_browser_dom_control_count = 0u;
    net_browser_dom_resource_count = 0u;
    net_browser_dom_unsupported_count = 0u;
    net_browser_dom_placeholder_count = 0u;
    net_browser_dom_depth = 0u;
    net_browser_dom_max_depth = 0u;
    net_browser_dom_stack_len = 0u;
    net_browser_layout_count = 0u;
    net_browser_layout_line_count = 0u;
    net_browser_layout_link_count = 0u;
    net_browser_layout_control_count = 0u;
    net_browser_layout_block_count = 0u;
    net_browser_layout_inline_count = 0u;
    net_browser_layout_table_count = 0u;
    net_browser_layout_box_count = 0u;
    net_browser_layout_wrap_count = 0u;
    net_browser_layout_margin_count = 0u;
    net_browser_layout_padding_count = 0u;
    net_browser_layout_border_count = 0u;
    net_browser_layout_width = 0u;
    net_browser_layout_height = 0u;
    net_browser_layout_serial_sent = 0u;
    net_browser_form_model_count = 0u;
    net_browser_control_model_count = 0u;
    net_browser_control_focusable_count = 0u;
    net_browser_control_editable_count = 0u;
    net_browser_control_hidden_count = 0u;
    net_browser_control_submit_count = 0u;
    net_browser_form_focus_count = 0u;
    net_browser_form_edit_count = 0u;
    net_browser_form_submit_event_count = 0u;
    net_browser_active_form = 0xFFu;
    net_browser_focused_control = 0xFFu;
    net_browser_form_caret = 0u;
    net_browser_link_target_count = 0u;
    net_browser_current_link_index = 0xFFu;
    net_browser_last_clicked_link = 0xFFu;
    net_browser_href_count = 0u;
    net_browser_src_count = 0u;
    net_browser_action_count = 0u;
    net_browser_html_attr_name[0] = 0;
    net_browser_html_attr_value[0] = 0;
    net_browser_current_class[0] = 0;
    net_browser_current_id[0] = 0;
    net_browser_first_href[0] = 0;
    net_browser_first_src[0] = 0;
    net_browser_first_action[0] = 0;
    net_browser_first_rel[0] = 0;
    net_browser_first_input_type[0] = 0;
    net_browser_current_img_src[0] = 0;
    net_browser_current_img_alt[0] = 0;
    net_browser_current_control_name[0] = 0;
    net_browser_current_control_actual[0] = 0;
    net_browser_current_form_action[0] = 0;
    net_browser_current_form_method[0] = 0;
    net_browser_last_form_submit_url[0] = 0;
    net_browser_resource_total = 0u;
    net_browser_resource_count = 0u;
    net_browser_resource_fetch_pending = 0u;
    net_browser_resource_fetch_started = 0u;
    net_browser_resource_fetch_done = 0u;
    net_browser_resource_fetch_body_seen = 0u;
    net_browser_resource_fetch_bytes = 0u;
    net_browser_resource_fetch_status[0] = '-';
    net_browser_resource_fetch_status[1] = '-';
    net_browser_resource_fetch_status[2] = '-';
    net_browser_resource_fetch_status[3] = 0;
    net_browser_set_resource_phase("IDLE");
    net_browser_fetch_url[0] = 0;
    net_browser_image_reset();
    net_tls_fetch_kind = 0u;
    net_tls_source_port = 0u;
    if (net_tls_primary_source_port_next < NET_TLS_SOURCE_PORT ||
        net_tls_primary_source_port_next > NET_TLS_SOURCE_PORT + 128u) {
        net_tls_primary_source_port_next = NET_TLS_SOURCE_PORT;
    }
    if (net_tls_resource_source_port_next < NET_TLS_RESOURCE_SOURCE_PORT ||
        net_tls_resource_source_port_next > NET_TLS_RESOURCE_SOURCE_PORT + 128u) {
        net_tls_resource_source_port_next = NET_TLS_RESOURCE_SOURCE_PORT;
    }
    net_browser_render_reset();
    net_browser_css_reset();
    net_browser_dom_reset();
    net_browser_js_reset();
    for (u32 r = 0u; r < NET_BROWSER_RESOURCE_MAX; r += 1u) {
        net_browser_resource_type[r] = 0;
        net_browser_resource_url[r][0] = 0;
        net_browser_resource_image[r] = 0xFFu;
    }
    for (u32 l = 0u; l < NET_BROWSER_LINK_MAX; l += 1u) {
        net_browser_link_url[l][0] = 0;
    }
    for (u32 f = 0u; f < NET_BROWSER_FORM_MAX; f += 1u) {
        net_browser_form_action_url[f][0] = 0;
        net_browser_form_get[f] = 1u;
    }
    for (u32 c = 0u; c < NET_BROWSER_CONTROL_MAX; c += 1u) {
        net_browser_control_form[c] = 0xFFu;
        net_browser_control_kind[c] = 0u;
        net_browser_control_focusable[c] = 0u;
        net_browser_control_editable[c] = 0u;
        net_browser_control_name[c][0] = 0;
        net_browser_control_value_store[c][0] = 0;
        net_browser_control_label[c][0] = 0;
    }
    for (u32 i = 0u; title[i] != 0 && net_browser_text_len < NET_BROWSER_TEXT_MAX - 1u; i += 1u) {
        net_browser_text[net_browser_text_len++] = title[i];
    }
    net_browser_text[net_browser_text_len] = 0;
    dirty = 1;
}

static void net_browser_css_finish_token(void);
static void net_browser_css_add_summary(void);
static void net_browser_js_finish_token(void);
static void net_browser_js_add_summary(void);
static void net_browser_unsupported_add_summary(void);
static void net_browser_note_placeholder(void);

static void net_browser_maybe_print_structure(void)
{
    if (net_browser_html_structure_sent || !net_browser_text_ready ||
        net_browser_tag_count == 0u) {
        return;
    }
    net_browser_css_finish_token();
    net_browser_js_finish_token();
    net_browser_css_add_summary();
    net_browser_js_add_summary();
    net_browser_unsupported_add_summary();
    net_browser_html_structure_sent = 1u;
    serial_print(msg_net_browser_structure);
    serial_print_dec(net_browser_tag_count);
    serial_print(" anchors ");
    serial_print_dec(net_browser_anchor_count);
    serial_print(" links ");
    serial_print_dec(net_browser_link_count);
    serial_print(" scripts ");
    serial_print_dec(net_browser_script_count);
    serial_print(" styles ");
    serial_print_dec(net_browser_style_count);
    serial_print(" images ");
    serial_print_dec(net_browser_image_count);
    serial_print(" forms ");
    serial_print_dec(net_browser_form_count);
    serial_print(" inputs ");
    serial_print_dec(net_browser_input_count);
    serial_print(" buttons ");
    serial_print_dec(net_browser_button_count);
    serial_print(" selects ");
    serial_print_dec(net_browser_select_count);
    serial_print(" textareas ");
    serial_print_dec(net_browser_textarea_count);
    serial_print("\r\n");
    serial_print("LeonOS net browser HTML support headings ");
    serial_print_dec(net_browser_heading_count);
    serial_print(" tables ");
    serial_print_dec(net_browser_table_count);
    serial_print(" cells ");
    serial_print_dec(net_browser_table_cell_count);
    serial_print(" meta ");
    serial_print_dec(net_browser_meta_count);
    serial_print(" svg ");
    serial_print_dec(net_browser_svg_count);
    serial_print(" svgshapes ");
    serial_print_dec(net_browser_svg_shape_count);
    serial_print(" picture ");
    serial_print_dec(net_browser_picture_count);
    serial_print(" media ");
    serial_print_dec(net_browser_media_count);
    serial_print(" custom ");
    serial_print_dec(net_browser_custom_count);
    serial_print(" dialogs ");
    serial_print_dec(net_browser_dialog_count);
    serial_print("\r\n");
    serial_print("LeonOS net browser HTML attrs aria ");
    serial_print_dec(net_browser_aria_attr_count);
    serial_print(" data ");
    serial_print_dec(net_browser_data_attr_count);
    serial_print(" js ");
    serial_print_dec(net_browser_js_attr_count);
    serial_print(" events ");
    serial_print_dec(net_browser_event_attr_count);
    serial_print(" jsurls ");
    serial_print_dec(net_browser_js_url_count);
    serial_print(" srcset ");
    serial_print_dec(net_browser_srcset_count);
    serial_print(" templates ");
    serial_print_dec(net_browser_template_count);
    serial_print(" noscript ");
    serial_print_dec(net_browser_noscript_count);
    serial_print(" entities yes forms yes controls yes pre yes\r\n");
    serial_print(msg_net_browser_resources);
    serial_print_dec(net_browser_attr_count);
    serial_print(" href ");
    serial_print_dec(net_browser_href_count);
    serial_print(" src ");
    serial_print_dec(net_browser_src_count);
    serial_print(" actions ");
    serial_print_dec(net_browser_action_count);
    serial_print("\r\n");
    serial_print(msg_net_browser_queue);
    serial_print_dec(net_browser_resource_total);
    serial_print(" kept ");
    serial_print_dec(net_browser_resource_count);
    if (net_browser_resource_count != 0u) {
        serial_print(" first ");
        serial_write(net_browser_resource_type[0]);
        serial_write(' ');
        serial_print(net_browser_resource_url[0]);
    }
    serial_print("\r\n");
    serial_print(msg_net_browser_render);
    serial_print_dec(net_browser_render_count);
    serial_print("\r\n");
    serial_print(msg_net_browser_dom);
    serial_print_dec(net_browser_dom_count);
    serial_print(" depth ");
    serial_print_dec(net_browser_dom_max_depth);
    serial_print(" text ");
    serial_print_dec(net_browser_dom_text_bytes);
    serial_print(" hidden ");
    serial_print_dec(net_browser_dom_hidden_count);
    serial_print(" supported ");
    serial_print_dec(net_browser_dom_supported_count);
    serial_print(" block ");
    serial_print_dec(net_browser_dom_block_count);
    serial_print(" inline ");
    serial_print_dec(net_browser_dom_inline_count);
    serial_print(" table ");
    serial_print_dec(net_browser_dom_table_count);
    serial_print(" controls ");
    serial_print_dec(net_browser_dom_control_count);
    serial_print(" resources ");
    serial_print_dec(net_browser_dom_resource_count);
    serial_print(" unsupported ");
    serial_print_dec(net_browser_dom_unsupported_count);
    serial_print(" placeholders ");
    serial_print_dec(net_browser_dom_placeholder_count);
    serial_print("\r\n");
    serial_print("LeonOS net browser form model forms ");
    serial_print_dec(net_browser_form_model_count);
    serial_print(" controls ");
    serial_print_dec(net_browser_control_model_count);
    serial_print(" focusable ");
    serial_print_dec(net_browser_control_focusable_count);
    serial_print(" editable ");
    serial_print_dec(net_browser_control_editable_count);
    serial_print(" hidden ");
    serial_print_dec(net_browser_control_hidden_count);
    serial_print(" submit ");
    serial_print_dec(net_browser_control_submit_count);
    serial_print("\r\n");
    if (net_browser_css_bytes != 0u) {
        serial_print(msg_net_browser_css);
        serial_print_dec(net_browser_css_rule_count);
        serial_print(" decls ");
        serial_print_dec(net_browser_css_decl_count);
        serial_print(" bytes ");
        serial_print_dec(net_browser_css_bytes);
        serial_print(" colors ");
        serial_print_dec(net_browser_css_color_count);
        serial_print(" fonts ");
        serial_print_dec(net_browser_css_font_count);
        serial_print(" urls ");
        serial_print_dec(net_browser_css_url_count);
        serial_print(" hidden ");
        serial_print_dec(net_browser_css_display_none_count);
        serial_print(" stored ");
        serial_print_dec(net_browser_css_stored_rule_count);
        serial_print(" matched ");
        serial_print_dec(net_browser_css_matched_rule_count);
        serial_print(" cascade ");
        serial_print_dec(net_browser_css_cascade_apply_count);
        serial_print(" colorapplied ");
        serial_print_dec(net_browser_css_color_apply_count);
        serial_print(" bgapplied ");
        serial_print_dec(net_browser_css_background_apply_count);
        serial_print(" width ");
        serial_print_dec(net_browser_css_width_apply_count);
        serial_print(" height ");
        serial_print_dec(net_browser_css_height_apply_count);
        serial_print(" margin ");
        serial_print_dec(net_browser_css_margin_apply_count);
        serial_print(" padding ");
        serial_print_dec(net_browser_css_padding_apply_count);
        serial_print(" border ");
        serial_print_dec(net_browser_css_border_apply_count);
        serial_print("\r\n");
    }
    serial_print(msg_net_browser_js);
    serial_print_dec(net_browser_script_count);
    serial_print(" bytes ");
    serial_print_dec(net_browser_js_bytes);
    serial_print(" tokens ");
    serial_print_dec(net_browser_js_token_count);
    serial_print(" funcs ");
    serial_print_dec(net_browser_js_function_count);
    serial_print(" vars ");
    serial_print_dec(net_browser_js_var_count);
    serial_print(" doc ");
    serial_print_dec(net_browser_js_document_count);
    serial_print(" win ");
    serial_print_dec(net_browser_js_window_count);
    serial_print(" loc ");
    serial_print_dec(net_browser_js_location_count);
    serial_print(" writes ");
    serial_print_dec(net_browser_js_write_count);
    serial_print("\r\n");
    serial_print(msg_net_browser_unsupported);
    serial_print("mode NOJS jsrequired ");
    serial_print(net_browser_js_required ? "yes" : "no");
    serial_print(" features ");
    serial_print_dec(net_browser_unsupported_feature_count);
    serial_print(" events ");
    serial_print_dec(net_browser_event_attr_count);
    serial_print(" jsurls ");
    serial_print_dec(net_browser_js_url_count);
    serial_print(" noscript ");
    serial_print_dec(net_browser_noscript_count);
    serial_print(" svg ");
    serial_print_dec(net_browser_svg_count);
    serial_print(" media ");
    serial_print_dec(net_browser_media_count);
    serial_print(" custom ");
    serial_print_dec(net_browser_custom_count);
    serial_print(" templates ");
    serial_print_dec(net_browser_template_count);
    serial_print(" srcset ");
    serial_print_dec(net_browser_srcset_count);
    serial_print("\r\n");
}

static u8 net_user_fetch_allows_empty_body(void)
{
    if (net_user_fetch_status_code == 204u ||
        net_user_fetch_status_code == 304u) {
        return 1u;
    }
    if (net_user_fetch_status_code >= 300u &&
        net_user_fetch_status_code < 400u &&
        net_user_fetch_location[0] != 0) {
        return 1u;
    }
    if (net_user_fetch_content_length_seen &&
        net_user_fetch_content_length == 0u) {
        return 1u;
    }
    if (net_user_fetch_status_code >= 200u &&
        net_user_fetch_status_code < 300u &&
        net_browser_header_only_retry_count >= NET_USER_FETCH_EMPTY_BODY_RETRY_MAX) {
        return 1u;
    }
    return 0u;
}

static u8 net_user_fetch_body_complete_for_finish(void)
{
	if (net_user_fetch_allows_empty_body()) {
		return 1u;
	}
	if ((net_user_fetch_flags & USER_NET_FETCH_FLAG_CHUNKED) != 0u &&
	    net_https_body_complete) {
		return 1u;
	}
	if (net_user_fetch_len == 0u) {
		return 0u;
	}
	if (net_user_fetch_content_length_seen &&
		net_user_fetch_len < net_user_fetch_content_length) {
        return 0u;
    }
    return 1u;
}

static void net_user_fetch_clear_response_for_retry(void)
{
    net_user_fetch_len = 0u;
    net_user_fetch_content_length = 0u;
    net_user_fetch_content_length_seen = 0u;
    net_user_fetch_status_code = 0u;
    net_user_fetch_flags = 0u;
    net_user_fetch_content_type[0] = 0;
    net_user_fetch_location[0] = 0;
    net_user_stream_read_pos = 0u;
    net_user_stream_last_len = 0u;
    net_user_stream_idle_polls = 0u;
}

static u8 net_user_fetch_retry_empty_response(const char *reason)
{
    if (!net_user_fetch_active ||
        net_user_fetch_done ||
        net_user_fetch_len != 0u ||
        (net_user_fetch_flags & USER_NET_FETCH_FLAG_HEADERS) == 0u ||
        net_user_fetch_allows_empty_body() ||
        net_browser_header_only_retry_count >= NET_USER_FETCH_EMPTY_BODY_RETRY_MAX) {
        return 0u;
    }
    if (net_user_fetch_status_code >= 200u &&
        net_user_fetch_status_code < 300u) {
        serial_print("LeonOS user net fetch empty body ");
        serial_print(reason);
        serial_print(" retry ");
        serial_print_dec((u32) net_browser_header_only_retry_count + 1u);
        serial_print(" ");
        serial_print(net_browser_current_url);
        serial_print("\r\n");
        net_user_fetch_clear_response_for_retry();
        net_browser_retry_current_url_after_https_stall("empty-fin");
        return 1u;
    }
    return 0u;
}

static void net_browser_finalize_primary_response(const char *reason)
{
    if (net_user_fetch_active) {
        if (net_user_fetch_retry_empty_response(reason)) {
            return;
        }
        if (net_user_fetch_status_code == 0u &&
            net_user_fetch_len == 0u &&
            (net_user_fetch_flags & USER_NET_FETCH_FLAG_HEADERS) == 0u &&
            net_browser_header_only_retry_count <
                    NET_BROWSER_HEADER_ONLY_RETRY_MAX) {
            net_browser_retry_current_url_after_https_stall("headerless-fin");
            return;
        }
        u8 ok = (net_user_fetch_status_code != 0u &&
                 (net_user_fetch_flags & USER_NET_FETCH_FLAG_HEADERS) != 0u &&
                 net_user_fetch_body_complete_for_finish());
        if (!ok) {
            serial_print("LeonOS user net fetch incomplete ");
            serial_print(reason);
            serial_print(" status ");
            serial_print_dec(net_user_fetch_status_code);
            serial_print(" flags ");
            serial_print_dec(net_user_fetch_flags);
            serial_print(" header_len ");
            serial_print_dec(net_https_header_len);
            serial_print(" headers_done ");
            serial_print(net_https_headers_done ? "yes" : "no");
            serial_print(" decoded ");
            serial_print_dec(net_https_body_decoded_total32);
            serial_print(" content_length ");
            if (net_user_fetch_content_length_seen) {
                serial_print_dec(net_user_fetch_content_length);
            } else {
                serial_print("unknown");
            }
            serial_print("\r\n");
        }
        net_user_fetch_finish(ok);
        return;
    }
    if (net_user_fetch_ignore_tail) {
        return;
    }
    if (net_browser_finalize_sent || net_tls_fetch_kind != 0u) {
        return;
    }
    net_browser_finalize_sent = 1u;
    if (!net_browser_text_ready &&
        (net_browser_text_len != 0u || net_browser_tag_count != 0u)) {
        net_browser_text_ready = 1u;
    }
    if (net_browser_direct_image_active) {
        net_browser_image_finalize_direct_document();
    }
    net_browser_maybe_print_structure();
    serial_print("LeonOS net browser response complete ");
    serial_print(reason);
    serial_print(" decoded ");
    serial_print_dec(net_https_body_decoded_total32);
    serial_print("\r\n");
    if (net_browser_history_complete_count < 65535u) {
        net_browser_history_complete_count += 1u;
    }
    net_browser_set_state("COMPLETE");
    net_browser_request_resource_fetch();
    net_tls_request_primary_close();
}

static void net_browser_note_decoded_body(u32 len)
{
    if (net_user_fetch_active) {
        net_https_body_last_tick = system_ticks;
        net_tls_ooo_idle_wait_count = 0u;
        if (0xFFFFFFFFu - net_https_body_decoded_total32 < len) {
            net_https_body_decoded_total32 = 0xFFFFFFFFu;
        } else {
            net_https_body_decoded_total32 += len;
        }
        if (net_user_fetch_len >= USER_NET_FETCH_MAX) {
            net_https_body_complete = 1u;
            net_browser_finalize_primary_response("user-cap");
        }
        return;
    }
    if (net_tls_fetch_kind != 0u || net_browser_finalize_sent) {
        return;
    }
    net_https_body_last_tick = system_ticks;
    net_tls_ooo_idle_wait_count = 0u;
    if (0xFFFFFFFFu - net_https_body_decoded_total32 < len) {
        net_https_body_decoded_total32 = 0xFFFFFFFFu;
    } else {
        net_https_body_decoded_total32 += len;
    }
    u32 limit = net_browser_direct_image_active
        ? NET_BROWSER_IMAGE_RAW_MAX
        : NET_BROWSER_BODY_PARSE_LIMIT;
    if (net_https_body_decoded_total32 >= limit) {
        net_https_body_complete = 1u;
        net_browser_finalize_primary_response("cap");
    }
}

static void net_https_log_primary_decoded_bytes(u32 len)
{
    if (net_tls_fetch_kind != 0u || net_https_body_decoded_log_count >= 8u) {
        return;
    }
    net_https_body_decoded_log_count += 1u;
    if (net_https_body_decoded_total + len > 65535u) {
        net_https_body_decoded_total = 65535u;
    } else {
        net_https_body_decoded_total += (u16) len;
    }
    serial_print("LeonOS net HTTPS body decoded bytes ");
    serial_print_dec(len);
    serial_print(" total ");
    serial_print_dec(net_https_body_decoded_total);
    serial_print("\r\n");
}

static void net_browser_render_reset(void)
{
    net_browser_render_count = 0u;
    net_browser_render_current = 0xFFu;
    net_browser_render_in_anchor = 0u;
    net_browser_render_in_title = 0u;
    net_browser_render_in_list = 0u;
    net_browser_render_list_depth = 0u;
    net_browser_render_in_quote = 0u;
    net_browser_render_in_pre = 0u;
    net_browser_render_in_code = 0u;
    net_browser_render_in_table_cell = 0u;
    net_browser_render_table_row_cell = 0u;
    net_browser_render_heading = 0u;
    net_browser_render_active_flags = 0u;
    net_browser_render_tag_flags = 0u;
    net_browser_render_active_css_flags = 0u;
    net_browser_render_active_css_color = 0u;
    net_browser_render_active_css_bg = 0u;
    net_browser_render_active_css_width = 0u;
    net_browser_render_active_css_height = 0u;
    net_browser_render_active_css_margin = 0u;
    net_browser_render_active_css_padding = 0u;
    net_browser_render_active_css_border = 0u;
    net_browser_css_clear_tag_style();
    net_browser_css_clear_inline_style();
    net_browser_render_style_depth = 0u;
    net_browser_control_type_len = 0u;
    net_browser_control_type[0] = 0;
    net_browser_control_value_len = 0u;
    net_browser_control_value[0] = 0;
    for (u32 i = 0u; i < NET_BROWSER_RENDER_MAX; i += 1u) {
        net_browser_render_kind[i] = 0u;
        net_browser_render_flags[i] = 0u;
        net_browser_render_link[i] = 0xFFu;
        net_browser_render_control[i] = 0xFFu;
        net_browser_render_image[i] = 0xFFu;
        net_browser_css_clear_render_slot((u8) i);
        net_browser_render_text[i][0] = 0;
    }
    for (u32 i = 0u; i < NET_BROWSER_STYLE_STACK_MAX; i += 1u) {
        net_browser_render_style_flags[i] = 0u;
        net_browser_render_style_css_flags[i] = 0u;
        net_browser_render_style_css_color[i] = 0u;
        net_browser_render_style_css_bg[i] = 0u;
        net_browser_render_style_css_width[i] = 0u;
        net_browser_render_style_css_height[i] = 0u;
        net_browser_render_style_css_margin[i] = 0u;
        net_browser_render_style_css_padding[i] = 0u;
        net_browser_render_style_css_border[i] = 0u;
        net_browser_render_style_tag[i][0] = 0;
    }
}

static u8 net_browser_render_effective_kind(void)
{
    if (net_browser_render_in_title) {
        return NET_BROWSER_RENDER_KIND_TITLE;
    }
    if (net_browser_render_heading != 0u) {
        return NET_BROWSER_RENDER_KIND_HEADING;
    }
    if (net_browser_render_in_anchor) {
        return NET_BROWSER_RENDER_KIND_LINK;
    }
    if (net_browser_render_in_quote) {
        return NET_BROWSER_RENDER_KIND_QUOTE;
    }
    if (net_browser_render_in_table_cell) {
        return NET_BROWSER_RENDER_KIND_TABLE;
    }
    if (net_browser_render_in_list) {
        return NET_BROWSER_RENDER_KIND_LIST;
    }
    return NET_BROWSER_RENDER_KIND_TEXT;
}

static u8 net_browser_render_effective_flags(void)
{
    u8 flags = net_browser_render_active_flags;
    if (net_browser_render_heading != 0u) {
        flags |= NET_BROWSER_RENDER_FLAG_BOLD | NET_BROWSER_RENDER_FLAG_LARGE;
    }
    if (net_browser_render_in_pre || net_browser_render_in_code) {
        flags |= NET_BROWSER_RENDER_FLAG_MONO;
    }
    if (net_browser_render_in_quote) {
        flags |= NET_BROWSER_RENDER_FLAG_BOX;
    }
    return flags;
}

static void net_browser_render_break(void)
{
    net_browser_render_current = 0xFFu;
}

static u32 net_browser_render_text_len(u8 slot)
{
    u32 len = 0u;
    while (len < NET_BROWSER_RENDER_TEXT_MAX - 1u &&
           net_browser_render_text[slot][len] != 0) {
        len += 1u;
    }
    return len;
}

static u8 net_browser_render_begin(u8 kind, u8 flags)
{
    if (net_browser_render_count >= NET_BROWSER_RENDER_MAX) {
        net_browser_render_current = 0xFFu;
        return 0u;
    }
    u8 slot = net_browser_render_count++;
    net_browser_render_kind[slot] = kind;
    net_browser_render_flags[slot] = flags;
    net_browser_css_copy_active_to_slot(slot);
    net_browser_render_link[slot] =
        kind == NET_BROWSER_RENDER_KIND_LINK ? net_browser_current_link_index : 0xFFu;
    net_browser_render_control[slot] = 0xFFu;
    net_browser_render_image[slot] = 0xFFu;
    net_browser_render_text[slot][0] = 0;
    net_browser_render_current = slot;
    if (kind == NET_BROWSER_RENDER_KIND_LIST) {
        u32 out = 0u;
        u8 depth = net_browser_render_list_depth;
        if (depth > 3u) {
            depth = 3u;
        }
        for (u8 d = 1u; d < depth &&
             out + 2u < NET_BROWSER_RENDER_TEXT_MAX - 1u; d += 1u) {
            net_browser_render_text[slot][out++] = ' ';
            net_browser_render_text[slot][out++] = ' ';
        }
        net_browser_render_text[slot][out++] = '-';
        net_browser_render_text[slot][out++] = ' ';
        net_browser_render_text[slot][out] = 0;
    }
    return 1u;
}

static void net_browser_render_append_char(char ch)
{
    if (ch == '\n') {
        net_browser_render_break();
        return;
    }
    if ((u8) ch < 32u || (u8) ch > 126u) {
        return;
    }
    u8 kind = net_browser_render_effective_kind();
    u8 flags = net_browser_render_effective_flags();
    if ((flags & NET_BROWSER_RENDER_FLAG_HIDDEN) != 0u) {
        return;
    }
    if (net_browser_render_current == 0xFFu ||
        net_browser_render_kind[net_browser_render_current] != kind ||
        net_browser_render_flags[net_browser_render_current] != flags) {
        if (!net_browser_render_begin(kind, flags)) {
            return;
        }
    }
    u8 slot = net_browser_render_current;
    u32 len = net_browser_render_text_len(slot);
    if (ch == ' ' &&
        !net_browser_render_in_pre &&
        (len == 0u ||
         net_browser_render_text[slot][len - 1u] == ' ')) {
        return;
    }
    if (len >= NET_BROWSER_RENDER_TEXT_MAX - 1u) {
        net_browser_render_break();
        return;
    }
    net_browser_render_text[slot][len++] = ch;
    net_browser_render_text[slot][len] = 0;
}

static void net_browser_render_add_block_flags(u8 kind, u8 flags,
                                               const char *prefix,
                                               const char *value)
{
    if (net_browser_render_count >= NET_BROWSER_RENDER_MAX) {
        return;
    }
    u8 slot = net_browser_render_count++;
    u32 out = 0u;
    net_browser_render_kind[slot] = kind;
    net_browser_render_flags[slot] = flags;
    net_browser_css_copy_active_to_slot(slot);
    net_browser_render_link[slot] = 0xFFu;
    net_browser_render_control[slot] = 0xFFu;
    net_browser_render_image[slot] = 0xFFu;
    for (u32 i = 0u; prefix[i] != 0 && out < NET_BROWSER_RENDER_TEXT_MAX - 1u; i += 1u) {
        net_browser_render_text[slot][out++] = prefix[i];
    }
    for (u32 i = 0u; value[i] != 0 && out < NET_BROWSER_RENDER_TEXT_MAX - 1u; i += 1u) {
        char ch = value[i];
        if ((u8) ch < 32u || (u8) ch > 126u) {
            ch = ' ';
        }
        net_browser_render_text[slot][out++] = ch;
    }
    net_browser_render_text[slot][out] = 0;
    net_browser_render_break();
}

static void net_browser_render_insert_block_flags(u8 kind, u8 flags,
                                                  const char *prefix,
                                                  const char *value)
{
    if (net_browser_render_count >= NET_BROWSER_RENDER_MAX) {
        return;
    }
    for (u32 i = net_browser_render_count; i > 0u; i -= 1u) {
        u32 from = i - 1u;
        net_browser_render_kind[i] = net_browser_render_kind[from];
        net_browser_render_flags[i] = net_browser_render_flags[from];
        net_browser_render_link[i] = net_browser_render_link[from];
        net_browser_render_control[i] = net_browser_render_control[from];
        net_browser_render_image[i] = net_browser_render_image[from];
        net_browser_render_css_flags[i] = net_browser_render_css_flags[from];
        net_browser_render_css_color[i] = net_browser_render_css_color[from];
        net_browser_render_css_bg[i] = net_browser_render_css_bg[from];
        net_browser_render_css_width[i] = net_browser_render_css_width[from];
        net_browser_render_css_height[i] = net_browser_render_css_height[from];
        net_browser_render_css_margin[i] = net_browser_render_css_margin[from];
        net_browser_render_css_padding[i] = net_browser_render_css_padding[from];
        net_browser_render_css_border[i] = net_browser_render_css_border[from];
        for (u32 j = 0u; j < NET_BROWSER_RENDER_TEXT_MAX; j += 1u) {
            net_browser_render_text[i][j] = net_browser_render_text[from][j];
            if (net_browser_render_text[from][j] == 0) {
                break;
            }
        }
    }
    net_browser_render_count += 1u;
    net_browser_render_kind[0] = kind;
    net_browser_render_flags[0] = flags;
    net_browser_render_link[0] = 0xFFu;
    net_browser_render_control[0] = 0xFFu;
    net_browser_render_image[0] = 0xFFu;
    net_browser_css_clear_render_slot(0u);
    u32 out = 0u;
    for (u32 i = 0u; prefix[i] != 0 && out < NET_BROWSER_RENDER_TEXT_MAX - 1u; i += 1u) {
        net_browser_render_text[0][out++] = prefix[i];
    }
    for (u32 i = 0u; value[i] != 0 && out < NET_BROWSER_RENDER_TEXT_MAX - 1u; i += 1u) {
        char ch = value[i];
        if ((u8) ch < 32u || (u8) ch > 126u) {
            ch = ' ';
        }
        net_browser_render_text[0][out++] = ch;
    }
    net_browser_render_text[0][out] = 0;
    net_browser_render_break();
}

static u8 net_browser_render_add_control_block(u8 kind, u8 flags,
                                               u8 control_index)
{
    if (net_browser_render_count >= NET_BROWSER_RENDER_MAX) {
        return 0xFFu;
    }
    u8 slot = net_browser_render_count++;
    net_browser_render_kind[slot] = kind;
    net_browser_render_flags[slot] = flags;
    net_browser_css_copy_active_to_slot(slot);
    net_browser_render_link[slot] = 0xFFu;
    net_browser_render_control[slot] = control_index;
    net_browser_render_image[slot] = 0xFFu;
    net_browser_render_text[slot][0] = 0;
    net_browser_render_break();
    return slot;
}

#include "browser_images.inc.c"

static void net_browser_css_add_summary(void)
{
    if (net_browser_css_summary_added ||
        (net_browser_style_count == 0u && net_browser_css_bytes == 0u)) {
        return;
    }
    net_browser_css_summary_added = 1u;
    net_browser_note_placeholder();
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_CSS,
                                       NET_BROWSER_RENDER_FLAG_BOX,
                                       "CSS ",
                                       "stylesheet parsed; subset only");
}

static void net_browser_append_char(char ch)
{
    if (net_browser_text_len >= NET_BROWSER_TEXT_MAX - 1u) {
        return;
    }
    if (ch == '\t' || ch == '\r') {
        ch = ' ';
    }
    if (ch == '\n') {
        if (!net_browser_render_in_pre &&
            (net_browser_text_len == 0u ||
             net_browser_text[net_browser_text_len - 1u] == '\n')) {
            return;
        }
    } else if (ch == ' ') {
        if (!net_browser_render_in_pre &&
            (net_browser_text_len == 0u ||
             net_browser_text[net_browser_text_len - 1u] == ' ' ||
             net_browser_text[net_browser_text_len - 1u] == '\n')) {
            return;
        }
    } else if ((u8) ch < 32u || (u8) ch > 126u) {
        return;
    }
    net_browser_text[net_browser_text_len++] = ch;
    net_browser_text[net_browser_text_len] = 0;
    if (ch != '\n' && net_browser_dom_text_bytes < 65535u) {
        net_browser_dom_text_bytes += 1u;
    }
    net_browser_render_append_char(ch);
    if (!net_browser_text_ready && net_browser_text_len > 32u) {
        net_browser_text_ready = 1u;
        serial_print("LeonOS net HTTPS body text bytes ");
        serial_print_dec(net_browser_text_len);
        serial_print("\r\n");
    }
    dirty = 1;
}

static u8 net_tag_is(const char *name)
{
    u32 i = 0u;
    while (name[i] != 0) {
        if (i >= net_browser_html_tag_len ||
            net_browser_html_tag[i] != name[i]) {
            return 0u;
        }
        i += 1u;
    }
    return i == net_browser_html_tag_len;
}

static u8 net_tag_starts_with(const char *name)
{
    u32 i = 0u;
    while (name[i] != 0) {
        if (i >= net_browser_html_tag_len ||
            net_browser_html_tag[i] != name[i]) {
            return 0u;
        }
        i += 1u;
    }
    return 1u;
}

static u8 net_tag_is_open(const char *name)
{
    return net_browser_html_tag_len != 0u &&
           net_browser_html_tag[0] != '/' &&
           net_tag_is(name);
}

static u8 net_tag_name_is(const char *name)
{
    u32 off = (net_browser_html_tag_len != 0u &&
               net_browser_html_tag[0] == '/') ? 1u : 0u;
    u32 i = 0u;
    while (name[i] != 0) {
        if (off + i >= net_browser_html_tag_len ||
            net_browser_html_tag[off + i] != name[i]) {
            return 0u;
        }
        i += 1u;
    }
    return off + i == net_browser_html_tag_len;
}

static u8 net_tag_is_heading_open(void)
{
    return net_browser_html_tag_len == 2u &&
           net_browser_html_tag[0] == 'h' &&
           net_browser_html_tag[1] >= '1' &&
           net_browser_html_tag[1] <= '6';
}

static u8 net_tag_is_heading_close(void)
{
    return net_browser_html_tag_len == 3u &&
           net_browser_html_tag[0] == '/' &&
           net_browser_html_tag[1] == 'h' &&
           net_browser_html_tag[2] >= '1' &&
           net_browser_html_tag[2] <= '6';
}

static u8 net_tag_is_block_name(void)
{
    return net_tag_name_is("p") || net_tag_name_is("div") ||
           net_tag_name_is("section") || net_tag_name_is("article") ||
           net_tag_name_is("header") || net_tag_name_is("footer") ||
           net_tag_name_is("main") || net_tag_name_is("nav") ||
           net_tag_name_is("aside") || net_tag_name_is("address") ||
           net_tag_name_is("figure") || net_tag_name_is("figcaption") ||
           net_tag_name_is("details") || net_tag_name_is("summary") ||
           net_tag_name_is("fieldset") || net_tag_name_is("legend") ||
           net_tag_name_is("form") || net_tag_name_is("hgroup") ||
           net_tag_name_is("search") || net_tag_name_is("dialog") ||
           net_tag_name_is("dl") || net_tag_name_is("dt") ||
           net_tag_name_is("dd") || net_tag_name_is("noscript");
}

static u8 net_tag_is_list_container(void)
{
    return net_tag_name_is("ul") || net_tag_name_is("ol") ||
           net_tag_name_is("menu");
}

static u8 net_tag_is_table_cell(void)
{
    return net_tag_name_is("td") || net_tag_name_is("th");
}

static u8 net_tag_has_dash(void)
{
    u32 off = (net_browser_html_tag_len != 0u &&
               net_browser_html_tag[0] == '/') ? 1u : 0u;
    for (u32 i = off; i < net_browser_html_tag_len; i += 1u) {
        if (net_browser_html_tag[i] == '-') {
            return 1u;
        }
    }
    return 0u;
}

static u8 net_tag_is_svg_shape(void)
{
    return net_tag_name_is("path") || net_tag_name_is("rect") ||
           net_tag_name_is("circle") || net_tag_name_is("ellipse") ||
           net_tag_name_is("line") || net_tag_name_is("polyline") ||
           net_tag_name_is("polygon") || net_tag_name_is("image") ||
           net_tag_name_is("g") || net_tag_name_is("use");
}

static u8 net_tag_is_media_name(void)
{
    return net_tag_name_is("audio") || net_tag_name_is("video") ||
           net_tag_name_is("canvas") || net_tag_name_is("iframe") ||
           net_tag_name_is("object") || net_tag_name_is("embed") ||
           net_tag_name_is("map") || net_tag_name_is("area");
}

static u8 net_tag_is_table_section(void)
{
    return net_tag_name_is("thead") || net_tag_name_is("tbody") ||
           net_tag_name_is("tfoot") || net_tag_name_is("caption") ||
           net_tag_name_is("colgroup") || net_tag_name_is("col");
}

static u8 net_tag_is_inline_name(void)
{
    return net_tag_name_is("a") || net_tag_name_is("span") ||
           net_tag_name_is("b") || net_tag_name_is("strong") ||
           net_tag_name_is("em") || net_tag_name_is("i") ||
           net_tag_name_is("small") || net_tag_name_is("label") ||
           net_tag_name_is("abbr") || net_tag_name_is("cite") ||
           net_tag_name_is("time") || net_tag_name_is("data") ||
           net_tag_name_is("code") || net_tag_name_is("kbd") ||
           net_tag_name_is("samp") || net_tag_name_is("var") ||
           net_tag_name_is("sup") || net_tag_name_is("sub") ||
           net_tag_name_is("mark") || net_tag_name_is("q") ||
           net_tag_name_is("br") || net_tag_name_is("wbr");
}

static u8 net_tag_is_control_name(void)
{
    return net_tag_name_is("input") || net_tag_name_is("button") ||
           net_tag_name_is("textarea") || net_tag_name_is("select") ||
           net_tag_name_is("option") || net_tag_name_is("output") ||
           net_tag_name_is("meter") || net_tag_name_is("progress");
}

static u8 net_tag_is_table_name(void)
{
    return net_tag_name_is("table") || net_tag_name_is("tr") ||
           net_tag_is_table_cell() || net_tag_is_table_section();
}

static u8 net_tag_is_unsupported_browser_name(void)
{
    return net_tag_name_is("script") || net_tag_name_is("style") ||
           net_tag_name_is("template") || net_tag_name_is("svg") ||
           net_tag_is_svg_shape() || net_tag_name_is("picture") ||
           net_tag_name_is("source") || net_tag_is_media_name() ||
           net_tag_has_dash();
}

static u8 net_browser_dom_kind_for_current_tag(void)
{
    if (net_tag_is_unsupported_browser_name()) {
        return NET_BROWSER_DOM_KIND_UNSUPPORTED;
    }
    if (net_tag_is_table_name()) {
        return NET_BROWSER_DOM_KIND_TABLE;
    }
    if (net_tag_is_control_name()) {
        return NET_BROWSER_DOM_KIND_CONTROL;
    }
    if (net_tag_name_is("img") || net_tag_name_is("link") ||
        net_tag_name_is("meta")) {
        return NET_BROWSER_DOM_KIND_RESOURCE;
    }
    if (net_tag_is_inline_name()) {
        return NET_BROWSER_DOM_KIND_INLINE;
    }
    return NET_BROWSER_DOM_KIND_BLOCK;
}

static void net_browser_dom_note_kind(u8 kind)
{
    if (kind != NET_BROWSER_DOM_KIND_UNSUPPORTED &&
        net_browser_dom_supported_count < 65535u) {
        net_browser_dom_supported_count += 1u;
    }
    if (kind == NET_BROWSER_DOM_KIND_BLOCK &&
        net_browser_dom_block_count < 65535u) {
        net_browser_dom_block_count += 1u;
    } else if (kind == NET_BROWSER_DOM_KIND_INLINE &&
               net_browser_dom_inline_count < 65535u) {
        net_browser_dom_inline_count += 1u;
    } else if (kind == NET_BROWSER_DOM_KIND_TABLE &&
               net_browser_dom_table_count < 65535u) {
        net_browser_dom_table_count += 1u;
    } else if (kind == NET_BROWSER_DOM_KIND_CONTROL &&
               net_browser_dom_control_count < 65535u) {
        net_browser_dom_control_count += 1u;
    } else if (kind == NET_BROWSER_DOM_KIND_RESOURCE &&
               net_browser_dom_resource_count < 65535u) {
        net_browser_dom_resource_count += 1u;
    } else if (kind == NET_BROWSER_DOM_KIND_UNSUPPORTED &&
               net_browser_dom_unsupported_count < 65535u) {
        net_browser_dom_unsupported_count += 1u;
    }
}

static void net_browser_note_placeholder(void)
{
    if (net_browser_dom_placeholder_count < 65535u) {
        net_browser_dom_placeholder_count += 1u;
    }
}

static u8 net_attr_starts_with(const char *prefix)
{
    u32 i = 0u;
    while (prefix[i] != 0) {
        if (i >= net_browser_html_attr_name_len ||
            net_browser_html_attr_name[i] != prefix[i]) {
            return 0u;
        }
        i += 1u;
    }
    return 1u;
}

static u8 net_html_space(u8 ch)
{
    return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\f';
}

static u8 net_attr_name_char(u8 lower)
{
    return (lower >= 'a' && lower <= 'z') ||
           (lower >= '0' && lower <= '9') ||
           lower == '-' || lower == '_' ||
           lower == ':' || lower == '.';
}

static u8 net_text_is(const char *left, const char *right)
{
    u32 i = 0u;
    while (left[i] != 0 && right[i] != 0) {
        if (left[i] != right[i]) {
            return 0u;
        }
        i += 1u;
    }
    return left[i] == 0 && right[i] == 0;
}

static u8 net_text_contains_ci(const char *text, const char *needle)
{
    if (needle[0] == 0) {
        return 1u;
    }
    for (u32 i = 0u; text[i] != 0; i += 1u) {
        u32 j = 0u;
        while (needle[j] != 0 &&
               text[i + j] != 0 &&
               net_ascii_lower((u8) text[i + j]) == (u8) needle[j]) {
            j += 1u;
        }
        if (needle[j] == 0) {
            return 1u;
        }
    }
    return 0u;
}

static u8 net_text_starts_with_ci(const char *text, const char *prefix);
static u32 net_append_capped(char *dst, u32 pos, u32 dst_len, const char *src);

static void net_browser_render_recompute_flags(void)
{
    net_browser_render_active_flags = 0u;
    net_browser_render_active_css_flags = 0u;
    net_browser_render_active_css_color = 0u;
    net_browser_render_active_css_bg = 0u;
    net_browser_render_active_css_width = 0u;
    net_browser_render_active_css_height = 0u;
    net_browser_render_active_css_margin = 0u;
    net_browser_render_active_css_padding = 0u;
    net_browser_render_active_css_border = 0u;
    for (u32 i = 0u; i < net_browser_render_style_depth; i += 1u) {
        net_browser_render_active_flags |= net_browser_render_style_flags[i];
        u16 css = net_browser_render_style_css_flags[i];
        if ((css & NET_BROWSER_CSS_STYLE_COLOR) != 0u) {
            net_browser_render_active_css_color = net_browser_render_style_css_color[i];
        }
        if ((css & NET_BROWSER_CSS_STYLE_BG) != 0u) {
            net_browser_render_active_css_bg = net_browser_render_style_css_bg[i];
        }
        if ((css & NET_BROWSER_CSS_STYLE_WIDTH) != 0u) {
            net_browser_render_active_css_width = net_browser_render_style_css_width[i];
        }
        if ((css & NET_BROWSER_CSS_STYLE_HEIGHT) != 0u) {
            net_browser_render_active_css_height = net_browser_render_style_css_height[i];
        }
        if ((css & NET_BROWSER_CSS_STYLE_MARGIN) != 0u) {
            net_browser_render_active_css_margin = net_browser_render_style_css_margin[i];
        }
        if ((css & NET_BROWSER_CSS_STYLE_PADDING) != 0u) {
            net_browser_render_active_css_padding = net_browser_render_style_css_padding[i];
        }
        if ((css & NET_BROWSER_CSS_STYLE_BORDER) != 0u) {
            net_browser_render_active_css_border = net_browser_render_style_css_border[i];
        }
        net_browser_render_active_css_flags |= css;
    }
}

static u8 net_tag_is_void_element(void)
{
    return net_tag_is("area") || net_tag_is("base") || net_tag_is("br") ||
           net_tag_is("col") || net_tag_is("embed") || net_tag_is("hr") ||
           net_tag_is("img") || net_tag_is("input") || net_tag_is("link") ||
           net_tag_is("meta") || net_tag_is("param") || net_tag_is("source") ||
           net_tag_is("track") || net_tag_is("wbr");
}

static void net_browser_render_push_tag_flags(void)
{
    u16 push_css = net_browser_render_tag_css_flags;
    u8 push_flags = net_browser_render_tag_flags;
    if (net_tag_is("html") || net_tag_is("body")) {
        push_css &= NET_BROWSER_CSS_STYLE_COLOR |
                    NET_BROWSER_CSS_STYLE_FONT_BOLD |
                    NET_BROWSER_CSS_STYLE_FONT_LARGE |
                    NET_BROWSER_CSS_STYLE_CENTER |
                    NET_BROWSER_CSS_STYLE_DISPLAY_NONE;
        push_flags &= (u8) ~(NET_BROWSER_RENDER_FLAG_BOX |
                             NET_BROWSER_RENDER_FLAG_INPUT |
                             NET_BROWSER_RENDER_FLAG_BUTTON);
        push_flags |= net_browser_css_render_flags(push_css);
    }
    if ((push_flags == 0u && push_css == 0u) ||
        net_browser_render_style_depth >= NET_BROWSER_STYLE_STACK_MAX ||
        net_tag_is_void_element()) {
        return;
    }
    u8 slot = net_browser_render_style_depth++;
    u32 i = 0u;
    for (; i < sizeof(net_browser_render_style_tag[slot]) - 1u &&
           i < net_browser_html_tag_len &&
           net_browser_html_tag[i] != 0; i += 1u) {
        net_browser_render_style_tag[slot][i] = net_browser_html_tag[i];
    }
    net_browser_render_style_tag[slot][i] = 0;
    net_browser_render_style_flags[slot] = push_flags;
    net_browser_render_style_css_flags[slot] = push_css;
    net_browser_render_style_css_color[slot] = net_browser_render_tag_css_color;
    net_browser_render_style_css_bg[slot] = net_browser_render_tag_css_bg;
    net_browser_render_style_css_width[slot] = net_browser_render_tag_css_width;
    net_browser_render_style_css_height[slot] = net_browser_render_tag_css_height;
    net_browser_render_style_css_margin[slot] = net_browser_render_tag_css_margin;
    net_browser_render_style_css_padding[slot] = net_browser_render_tag_css_padding;
    net_browser_render_style_css_border[slot] = net_browser_render_tag_css_border;
    net_browser_render_recompute_flags();
}

static void net_browser_render_pop_tag_flags(void)
{
    if (net_browser_html_tag_len < 2u || net_browser_html_tag[0] != '/') {
        return;
    }
    for (u32 scan = net_browser_render_style_depth; scan != 0u; scan -= 1u) {
        u32 slot = scan - 1u;
        if (net_text_is(net_browser_render_style_tag[slot],
                        &net_browser_html_tag[1])) {
            for (u32 move = slot; move + 1u < net_browser_render_style_depth; move += 1u) {
                net_browser_render_style_flags[move] =
                    net_browser_render_style_flags[move + 1u];
                net_browser_render_style_css_flags[move] =
                    net_browser_render_style_css_flags[move + 1u];
                net_browser_render_style_css_color[move] =
                    net_browser_render_style_css_color[move + 1u];
                net_browser_render_style_css_bg[move] =
                    net_browser_render_style_css_bg[move + 1u];
                net_browser_render_style_css_width[move] =
                    net_browser_render_style_css_width[move + 1u];
                net_browser_render_style_css_height[move] =
                    net_browser_render_style_css_height[move + 1u];
                net_browser_render_style_css_margin[move] =
                    net_browser_render_style_css_margin[move + 1u];
                net_browser_render_style_css_padding[move] =
                    net_browser_render_style_css_padding[move + 1u];
                net_browser_render_style_css_border[move] =
                    net_browser_render_style_css_border[move + 1u];
                for (u32 i = 0u; i < sizeof(net_browser_render_style_tag[move]); i += 1u) {
                    net_browser_render_style_tag[move][i] =
                        net_browser_render_style_tag[move + 1u][i];
                }
            }
            net_browser_render_style_depth -= 1u;
            net_browser_render_style_flags[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_flags[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_color[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_bg[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_width[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_height[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_margin[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_padding[net_browser_render_style_depth] = 0u;
            net_browser_render_style_css_border[net_browser_render_style_depth] = 0u;
            net_browser_render_style_tag[net_browser_render_style_depth][0] = 0;
            net_browser_render_recompute_flags();
            net_browser_render_break();
            return;
        }
    }
}

static void net_browser_control_value_set_from(const char *prefix,
                                               const char *value)
{
    if (net_browser_control_value_len != 0u) {
        return;
    }
    u32 out = 0u;
    for (u32 i = 0u; prefix[i] != 0 && out < sizeof(net_browser_control_value) - 1u; i += 1u) {
        net_browser_control_value[out++] = prefix[i];
    }
    for (u32 i = 0u; value[i] != 0 &&
           out < sizeof(net_browser_control_value) - 1u; i += 1u) {
        net_browser_control_value[out++] = value[i];
    }
    net_browser_control_value[out] = 0;
    net_browser_control_value_len = (u8) out;
}

static void net_browser_control_value_set(const char *prefix)
{
    net_browser_control_value_set_from(prefix, net_browser_html_attr_value);
}

static void net_browser_control_type_set(void)
{
    if (net_browser_control_type_len != 0u) {
        return;
    }
    u32 out = 0u;
    for (u32 i = 0u; net_browser_html_attr_value[i] != 0 &&
           out < sizeof(net_browser_control_type) - 1u; i += 1u) {
        net_browser_control_type[out++] =
            (char) net_ascii_lower((u8) net_browser_html_attr_value[i]);
    }
    net_browser_control_type[out] = 0;
    net_browser_control_type_len = (u8) out;
}

static u8 net_browser_control_kind_from_current(void);
static u8 net_browser_register_current_control(u8 kind);
static void net_browser_control_write_render_text(u8 slot, u8 control);

static void net_browser_render_emit_input_control(void)
{
    u8 kind = net_browser_control_kind_from_current();
    u8 control = net_browser_register_current_control(kind);
    if ((net_browser_render_tag_flags & NET_BROWSER_RENDER_FLAG_HIDDEN) != 0u ||
        kind == NET_BROWSER_CONTROL_KIND_HIDDEN) {
        return;
    }
    u8 flags = NET_BROWSER_RENDER_FLAG_INPUT | NET_BROWSER_RENDER_FLAG_BOX;
    if (kind == NET_BROWSER_CONTROL_KIND_SUBMIT ||
        kind == NET_BROWSER_CONTROL_KIND_BUTTON) {
        flags = NET_BROWSER_RENDER_FLAG_BUTTON | NET_BROWSER_RENDER_FLAG_BOX |
                NET_BROWSER_RENDER_FLAG_CENTER;
    }
    if (net_browser_control_value_len == 0u) {
        if (kind == NET_BROWSER_CONTROL_KIND_SUBMIT ||
            kind == NET_BROWSER_CONTROL_KIND_BUTTON) {
            net_browser_control_value_set_from("", "Search");
        } else if (net_text_is(net_browser_control_type, "text") ||
                   net_text_is(net_browser_control_type, "search") ||
                   net_browser_control_type[0] == 0) {
            net_browser_control_value_set_from("", "Search");
        } else {
            net_browser_control_value_set_from("type ", net_browser_control_type);
        }
    }
    if (control < NET_BROWSER_CONTROL_MAX) {
        u8 slot = net_browser_render_add_control_block(NET_BROWSER_RENDER_KIND_TEXT,
                                                       flags,
                                                       control);
        if (slot != 0xFFu) {
            net_browser_css_apply_tag_to_slot(slot);
            net_browser_control_write_render_text(slot, control);
        }
    } else {
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_TEXT,
                                           flags,
                                           "INPUT ", net_browser_control_value);
    }
}

static u8 net_css_ident_char(u8 ch)
{
    ch = net_ascii_lower(ch);
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_';
}

static u8 net_css_hex_nibble(char ch, u8 *value)
{
    if (ch >= '0' && ch <= '9') {
        *value = (u8) (ch - '0');
        return 1u;
    }
    ch = (char) net_ascii_lower((u8) ch);
    if (ch >= 'a' && ch <= 'f') {
        *value = (u8) (10u + ch - 'a');
        return 1u;
    }
    return 0u;
}

static u8 net_css_parse_named_color(const char *value, u32 *color)
{
    if (net_text_contains_ci(value, "black")) {
        *color = 0x00000000u;
        return 1u;
    }
    if (net_text_contains_ci(value, "white")) {
        *color = 0x00FFFFFFu;
        return 1u;
    }
    if (net_text_contains_ci(value, "red")) {
        *color = 0x00CC3333u;
        return 1u;
    }
    if (net_text_contains_ci(value, "green")) {
        *color = 0x002E7D32u;
        return 1u;
    }
    if (net_text_contains_ci(value, "blue")) {
        *color = 0x001A5FB4u;
        return 1u;
    }
    if (net_text_contains_ci(value, "gray") ||
        net_text_contains_ci(value, "grey")) {
        *color = 0x006B7280u;
        return 1u;
    }
    if (net_text_contains_ci(value, "silver")) {
        *color = 0x00CBD5E1u;
        return 1u;
    }
    if (net_text_contains_ci(value, "yellow")) {
        *color = 0x00FACC15u;
        return 1u;
    }
    if (net_text_contains_ci(value, "orange")) {
        *color = 0x00EA7600u;
        return 1u;
    }
    if (net_text_contains_ci(value, "purple")) {
        *color = 0x007C3AEDu;
        return 1u;
    }
    if (net_text_contains_ci(value, "cyan")) {
        *color = 0x000EA5E9u;
        return 1u;
    }
    return 0u;
}

static u8 net_css_read_uint(const char *text, u32 *pos, u32 *value)
{
    while (text[*pos] == ' ' || text[*pos] == '\t') {
        *pos += 1u;
    }
    u32 out = 0u;
    u8 seen = 0u;
    while (text[*pos] >= '0' && text[*pos] <= '9') {
        seen = 1u;
        if (out < 100000u) {
            out = out * 10u + (u32) (text[*pos] - '0');
        }
        *pos += 1u;
    }
    if (!seen) {
        return 0u;
    }
    *value = out;
    return 1u;
}

static u8 net_css_parse_length_px(const char *value, u16 *px)
{
    u32 pos = 0u;
    u32 parsed = 0u;
    if (!net_css_read_uint(value, &pos, &parsed)) {
        return 0u;
    }
    if (parsed > 1800u) {
        parsed = 1800u;
    }
    *px = (u16) parsed;
    return 1u;
}

static u8 net_css_parse_color(const char *value, u32 *color)
{
    for (u32 i = 0u; value[i] != 0; i += 1u) {
        if (value[i] == '#') {
            u8 a = 0u;
            u8 b = 0u;
            u8 c = 0u;
            u8 d = 0u;
            u8 e = 0u;
            u8 f = 0u;
            if (net_css_hex_nibble(value[i + 1u], &a) &&
                net_css_hex_nibble(value[i + 2u], &b) &&
                net_css_hex_nibble(value[i + 3u], &c)) {
                if (net_css_hex_nibble(value[i + 4u], &d) &&
                    net_css_hex_nibble(value[i + 5u], &e) &&
                    net_css_hex_nibble(value[i + 6u], &f)) {
                    *color = ((u32) a << 20) | ((u32) b << 16) |
                             ((u32) c << 12) | ((u32) d << 8) |
                             ((u32) e << 4) | (u32) f;
                    return 1u;
                }
                *color = ((u32) a << 20) | ((u32) a << 16) |
                         ((u32) b << 12) | ((u32) b << 8) |
                         ((u32) c << 4) | (u32) c;
                return 1u;
            }
        }
        if (value[i] == 'r' && value[i + 1u] == 'g' &&
            value[i + 2u] == 'b' && value[i + 3u] == '(') {
            u32 pos = i + 4u;
            u32 r = 0u;
            u32 g = 0u;
            u32 b = 0u;
            if (net_css_read_uint(value, &pos, &r)) {
                while (value[pos] != 0 && value[pos] != ',') {
                    pos += 1u;
                }
                if (value[pos] == ',') {
                    pos += 1u;
                }
                if (net_css_read_uint(value, &pos, &g)) {
                    while (value[pos] != 0 && value[pos] != ',') {
                        pos += 1u;
                    }
                    if (value[pos] == ',') {
                        pos += 1u;
                    }
                    if (net_css_read_uint(value, &pos, &b)) {
                        if (r > 255u) {
                            r = 255u;
                        }
                        if (g > 255u) {
                            g = 255u;
                        }
                        if (b > 255u) {
                            b = 255u;
                        }
                        *color = (r << 16) | (g << 8) | b;
                        return 1u;
                    }
                }
            }
        }
    }
    return net_css_parse_named_color(value, color);
}

static void net_browser_css_apply_decl_fields(const char *prop,
                                              const char *value,
                                              u16 *style_flags,
                                              u8 *render_flags,
                                              u32 *color,
                                              u32 *bg,
                                              u16 *width,
                                              u16 *height,
                                              u16 *margin,
                                              u16 *padding,
                                              u16 *border)
{
    u16 len = 0u;
    u32 parsed_color = 0u;
    if (net_text_is(prop, "display")) {
        if (net_text_contains_ci(value, "none")) {
            *style_flags |= NET_BROWSER_CSS_STYLE_DISPLAY_NONE;
        } else if (net_text_contains_ci(value, "block") ||
                   net_text_contains_ci(value, "flex") ||
                   net_text_contains_ci(value, "grid") ||
                   net_text_contains_ci(value, "table") ||
                   net_text_contains_ci(value, "list-item")) {
            *style_flags |= NET_BROWSER_CSS_STYLE_DISPLAY_BLOCK;
        }
    } else if (net_text_is(prop, "visibility") &&
               net_text_contains_ci(value, "hidden")) {
        *style_flags |= NET_BROWSER_CSS_STYLE_DISPLAY_NONE;
    } else if (net_text_is(prop, "text-align") &&
               net_text_contains_ci(value, "center")) {
        *style_flags |= NET_BROWSER_CSS_STYLE_CENTER;
    } else if (net_text_is(prop, "font-weight") &&
               (net_text_contains_ci(value, "bold") ||
                net_text_contains_ci(value, "600") ||
                net_text_contains_ci(value, "700") ||
                net_text_contains_ci(value, "800") ||
                net_text_contains_ci(value, "900"))) {
        *style_flags |= NET_BROWSER_CSS_STYLE_FONT_BOLD;
    } else if (net_text_is(prop, "font-size")) {
        u16 px = 0u;
        if (net_css_parse_length_px(value, &px) && px >= 18u) {
            *style_flags |= NET_BROWSER_CSS_STYLE_FONT_LARGE;
        }
    } else if (net_text_is(prop, "line-height")) {
        u16 px = 0u;
        if (net_css_parse_length_px(value, &px) && px >= 22u) {
            *style_flags |= NET_BROWSER_CSS_STYLE_FONT_LARGE;
        } else if (!net_css_parse_length_px(value, &px)) {
            *style_flags |= NET_BROWSER_CSS_STYLE_FONT_LARGE;
        }
    } else if (net_text_is(prop, "max-width")) {
        if (net_css_parse_length_px(value, &len)) {
            *width = len;
            *style_flags |= NET_BROWSER_CSS_STYLE_WIDTH;
        }
    } else if (net_text_is(prop, "color")) {
        if (net_css_parse_color(value, &parsed_color)) {
            *color = parsed_color;
            *style_flags |= NET_BROWSER_CSS_STYLE_COLOR;
        }
    } else if (net_text_starts_with_ci(prop, "background")) {
        if (net_css_parse_color(value, &parsed_color)) {
            *bg = parsed_color;
        } else if (!net_text_contains_ci(value, "none") &&
                   !net_text_contains_ci(value, "transparent")) {
            *bg = 0x00F6F8FAu;
        } else {
            *render_flags |= net_browser_css_render_flags(*style_flags);
            return;
        }
        *style_flags |= NET_BROWSER_CSS_STYLE_BG;
    } else if (net_text_is(prop, "width")) {
        if (net_css_parse_length_px(value, &len)) {
            *width = len;
            *style_flags |= NET_BROWSER_CSS_STYLE_WIDTH;
        }
    } else if (net_text_is(prop, "height")) {
        if (net_css_parse_length_px(value, &len)) {
            *height = len;
            *style_flags |= NET_BROWSER_CSS_STYLE_HEIGHT;
        }
    } else if (net_text_is(prop, "margin") ||
               net_text_starts_with_ci(prop, "margin-")) {
        if (net_css_parse_length_px(value, &len)) {
            *margin = len;
            *style_flags |= NET_BROWSER_CSS_STYLE_MARGIN;
        }
    } else if (net_text_is(prop, "padding") ||
               net_text_starts_with_ci(prop, "padding-")) {
        if (net_css_parse_length_px(value, &len)) {
            *padding = len;
            *style_flags |= NET_BROWSER_CSS_STYLE_PADDING;
        }
    } else if (net_text_is(prop, "border") ||
               net_text_starts_with_ci(prop, "border-")) {
        if (net_text_contains_ci(value, "none")) {
            *render_flags |= net_browser_css_render_flags(*style_flags);
            return;
        }
        if (net_css_parse_length_px(value, &len) && len == 0u) {
            *render_flags |= net_browser_css_render_flags(*style_flags);
            return;
        }
        if (len == 0u) {
            len = 1u;
        }
        if (len > 8u) {
            len = 8u;
        }
        *border = len;
        *style_flags |= NET_BROWSER_CSS_STYLE_BORDER;
    }
    *render_flags |= net_browser_css_render_flags(*style_flags);
}

static void net_browser_css_reset_rule_parse(void)
{
    net_browser_css_state = 0u;
    net_browser_css_selector_len = 0u;
    net_browser_css_prop_len = 0u;
    net_browser_css_value_len = 0u;
    net_browser_css_current_selector_kind = 0u;
    net_browser_css_current_flags = 0u;
    net_browser_css_current_style_flags = 0u;
    net_browser_css_current_color = 0u;
    net_browser_css_current_bg = 0u;
    net_browser_css_current_width = 0u;
    net_browser_css_current_height = 0u;
    net_browser_css_current_margin = 0u;
    net_browser_css_current_padding = 0u;
    net_browser_css_current_border = 0u;
    net_browser_css_selector[0] = 0;
    net_browser_css_prop[0] = 0;
    net_browser_css_value[0] = 0;
}

static void net_browser_css_commit_selector(void)
{
    u32 pos = 0u;
    u32 out = 0u;
    net_browser_css_current_selector_kind = 0u;
    net_browser_css_class_pending[0] = 0;
    while (pos < net_browser_css_selector_len &&
           net_html_space((u8) net_browser_css_selector[pos])) {
        pos += 1u;
    }
    if (pos >= net_browser_css_selector_len ||
        net_browser_css_selector[pos] == '@') {
        return;
    }
    if (net_browser_css_selector[pos] == '.') {
        net_browser_css_current_selector_kind = 'C';
        pos += 1u;
    } else if (net_browser_css_selector[pos] == '#') {
        net_browser_css_current_selector_kind = 'I';
        pos += 1u;
    } else if (net_css_ident_char((u8) net_browser_css_selector[pos])) {
        net_browser_css_current_selector_kind = 'T';
    } else {
        return;
    }
    out = 0u;
    while (pos < net_browser_css_selector_len &&
           net_css_ident_char((u8) net_browser_css_selector[pos]) &&
           out < NET_BROWSER_CSS_SELECTOR_MAX - 1u) {
        net_browser_css_selector[out++] =
            (char) net_ascii_lower((u8) net_browser_css_selector[pos]);
        pos += 1u;
    }
    net_browser_css_selector[out] = 0;
    if (out == 0u) {
        net_browser_css_current_selector_kind = 0u;
        return;
    }
    if (net_browser_css_current_selector_kind == 'T' &&
        pos < net_browser_css_selector_len &&
        net_browser_css_selector[pos] == '.') {
        pos += 1u;
        u32 cls = 0u;
        while (pos < net_browser_css_selector_len &&
               net_css_ident_char((u8) net_browser_css_selector[pos]) &&
               cls < NET_BROWSER_CSS_SELECTOR_MAX - 1u) {
            net_browser_css_class_pending[cls++] =
                (char) net_ascii_lower((u8) net_browser_css_selector[pos]);
            pos += 1u;
        }
        net_browser_css_class_pending[cls] = 0;
        if (cls != 0u) {
            net_browser_css_current_selector_kind = 'B';
        }
    }
}

static void net_browser_css_commit_decl(void)
{
    net_browser_css_prop[net_browser_css_prop_len] = 0;
    net_browser_css_value[net_browser_css_value_len] = 0;
    if (net_browser_css_prop_len != 0u) {
        net_browser_css_apply_decl_fields(net_browser_css_prop,
                                          net_browser_css_value,
                                          &net_browser_css_current_style_flags,
                                          &net_browser_css_current_flags,
                                          &net_browser_css_current_color,
                                          &net_browser_css_current_bg,
                                          &net_browser_css_current_width,
                                          &net_browser_css_current_height,
                                          &net_browser_css_current_margin,
                                          &net_browser_css_current_padding,
                                          &net_browser_css_current_border);
    }
    net_browser_css_prop_len = 0u;
    net_browser_css_value_len = 0u;
    net_browser_css_prop[0] = 0;
    net_browser_css_value[0] = 0;
}

static void net_browser_css_store_rule(void)
{
    if (net_browser_css_current_selector_kind == 0u ||
        net_browser_css_selector[0] == 0 ||
        (net_browser_css_current_flags == 0u &&
         net_browser_css_current_style_flags == 0u) ||
        net_browser_css_stored_rule_count >= NET_BROWSER_CSS_RULE_MAX) {
        return;
    }
    u16 slot = net_browser_css_stored_rule_count++;
    net_browser_css_rule_kind[slot] = net_browser_css_current_selector_kind;
    net_browser_css_rule_flags[slot] = net_browser_css_current_flags;
    net_browser_css_rule_style_flags[slot] = net_browser_css_current_style_flags;
    net_browser_css_rule_color[slot] = net_browser_css_current_color;
    net_browser_css_rule_bg[slot] = net_browser_css_current_bg;
    net_browser_css_rule_width[slot] = net_browser_css_current_width;
    net_browser_css_rule_height[slot] = net_browser_css_current_height;
    net_browser_css_rule_margin[slot] = net_browser_css_current_margin;
    net_browser_css_rule_padding[slot] = net_browser_css_current_padding;
    net_browser_css_rule_border[slot] = net_browser_css_current_border;
    net_append_capped(net_browser_css_rule_selector[slot], 0u,
                      NET_BROWSER_CSS_SELECTOR_MAX, net_browser_css_selector);
    net_browser_css_rule_class[slot][0] = 0;
    if (net_browser_css_current_selector_kind == 'B') {
        net_append_capped(net_browser_css_rule_class[slot], 0u,
                          NET_BROWSER_CSS_SELECTOR_MAX,
                          net_browser_css_class_pending);
    }
}

static void net_browser_css_store_rule_group(void)
{
    char group[NET_BROWSER_CSS_SELECTOR_MAX];
    u32 group_len = net_browser_css_selector_len;
    if (group_len >= NET_BROWSER_CSS_SELECTOR_MAX) {
        group_len = NET_BROWSER_CSS_SELECTOR_MAX - 1u;
    }
    for (u32 i = 0u; i < group_len; i += 1u) {
        group[i] = net_browser_css_selector[i];
    }
    group[group_len] = 0;

    u32 start = 0u;
    for (u32 i = 0u; i <= group_len; i += 1u) {
        if (i == group_len || group[i] == ',') {
            u32 end = i;
            while (start < end && net_html_space((u8) group[start])) {
                start += 1u;
            }
            while (end > start && net_html_space((u8) group[end - 1u])) {
                end -= 1u;
            }
            u32 len = 0u;
            for (u32 j = start; j < end && len < NET_BROWSER_CSS_SELECTOR_MAX - 1u;
                 j += 1u) {
                net_browser_css_selector[len++] = group[j];
            }
            net_browser_css_selector[len] = 0;
            net_browser_css_selector_len = (u8) len;
            if (len != 0u) {
                net_browser_css_commit_selector();
                net_browser_css_store_rule();
            }
            start = i + 1u;
        }
    }
}

static void net_browser_css_parse_feed(u8 ch)
{
    u8 lower = net_ascii_lower(ch);
    if (net_browser_css_state == 0u) {
        if (ch == '{') {
            net_browser_css_selector[net_browser_css_selector_len] = 0;
            net_browser_css_prop_len = 0u;
            net_browser_css_value_len = 0u;
            net_browser_css_current_flags = 0u;
            net_browser_css_current_style_flags = 0u;
            net_browser_css_current_color = 0u;
            net_browser_css_current_bg = 0u;
            net_browser_css_current_width = 0u;
            net_browser_css_current_height = 0u;
            net_browser_css_current_margin = 0u;
            net_browser_css_current_padding = 0u;
            net_browser_css_current_border = 0u;
            net_browser_css_state = 1u;
            return;
        }
        if (net_browser_css_selector_len < NET_BROWSER_CSS_SELECTOR_MAX - 1u &&
            (net_css_ident_char(lower) || lower == '.' || lower == '#' ||
             lower == '-' || lower == '_' || net_html_space(lower))) {
            net_browser_css_selector[net_browser_css_selector_len++] =
                (char) lower;
        }
        return;
    }
    if (net_browser_css_state == 1u) {
        if (ch == '}') {
            net_browser_css_store_rule_group();
            net_browser_css_reset_rule_parse();
            return;
        }
        if (ch == ':') {
            net_browser_css_state = 2u;
            return;
        }
        if (ch == ';') {
            net_browser_css_prop_len = 0u;
            net_browser_css_prop[0] = 0;
            return;
        }
        if (!net_html_space(ch) &&
            net_browser_css_prop_len < sizeof(net_browser_css_prop) - 1u &&
            (net_css_ident_char(lower) || lower == '-')) {
            net_browser_css_prop[net_browser_css_prop_len++] = (char) lower;
        }
        return;
    }
    if (net_browser_css_state == 2u) {
        if (ch == ';' || ch == '}') {
            net_browser_css_commit_decl();
            if (ch == '}') {
                net_browser_css_store_rule_group();
                net_browser_css_reset_rule_parse();
            } else {
                net_browser_css_state = 1u;
            }
            return;
        }
        if (net_browser_css_value_len < sizeof(net_browser_css_value) - 1u &&
            lower >= 32u && lower <= 126u) {
            net_browser_css_value[net_browser_css_value_len++] = (char) lower;
        }
    }
}

static u8 net_browser_class_contains(const char *classes, const char *needle)
{
    u32 needle_len = 0u;
    while (needle[needle_len] != 0) {
        needle_len += 1u;
    }
    if (needle_len == 0u) {
        return 0u;
    }
    u32 i = 0u;
    while (classes[i] != 0) {
        while (classes[i] == ' ') {
            i += 1u;
        }
        u32 start = i;
        u32 len = 0u;
        while (classes[i] != 0 && classes[i] != ' ') {
            i += 1u;
            len += 1u;
        }
        if (len == needle_len) {
            u8 ok = 1u;
            for (u32 j = 0u; j < len; j += 1u) {
                if (net_ascii_lower((u8) classes[start + j]) !=
                    (u8) needle[j]) {
                    ok = 0u;
                    break;
                }
            }
            if (ok) {
                return 1u;
            }
        }
    }
    return 0u;
}

static u8 net_browser_css_apply_styles_for_current_tag(void)
{
    net_browser_css_clear_tag_style();
    for (u32 i = 0u; i < net_browser_css_stored_rule_count; i += 1u) {
        u8 matched = 0u;
        if (net_browser_css_rule_kind[i] == 'T' &&
            net_tag_is(net_browser_css_rule_selector[i])) {
            matched = 1u;
        } else if (net_browser_css_rule_kind[i] == 'C' &&
                   net_browser_class_contains(net_browser_current_class,
                                              net_browser_css_rule_selector[i])) {
            matched = 1u;
        } else if (net_browser_css_rule_kind[i] == 'I' &&
                   net_text_is(net_browser_current_id,
                               net_browser_css_rule_selector[i])) {
            matched = 1u;
        } else if (net_browser_css_rule_kind[i] == 'B' &&
                   net_tag_is(net_browser_css_rule_selector[i]) &&
                   net_browser_class_contains(net_browser_current_class,
                                              net_browser_css_rule_class[i])) {
            matched = 1u;
        }
        if (matched) {
            net_browser_css_apply_to_tag(net_browser_css_rule_style_flags[i],
                                         net_browser_css_rule_color[i],
                                         net_browser_css_rule_bg[i],
                                         net_browser_css_rule_width[i],
                                         net_browser_css_rule_height[i],
                                         net_browser_css_rule_margin[i],
                                         net_browser_css_rule_padding[i],
                                         net_browser_css_rule_border[i]);
            net_browser_render_tag_flags |= net_browser_css_rule_flags[i];
            if (net_browser_css_matched_rule_count < 65535u) {
                net_browser_css_matched_rule_count += 1u;
            }
            if (net_browser_css_cascade_apply_count < 65535u) {
                net_browser_css_cascade_apply_count += 1u;
            }
            net_browser_css_count_applied(net_browser_css_rule_style_flags[i]);
        }
    }
    if (net_browser_inline_css_flags != 0u) {
        net_browser_css_apply_to_tag(net_browser_inline_css_flags,
                                     net_browser_inline_css_color,
                                     net_browser_inline_css_bg,
                                     net_browser_inline_css_width,
                                     net_browser_inline_css_height,
                                     net_browser_inline_css_margin,
                                     net_browser_inline_css_padding,
                                     net_browser_inline_css_border);
        net_browser_css_count_applied(net_browser_inline_css_flags);
    }
    return net_browser_css_render_flags(net_browser_render_tag_css_flags);
}

static void net_browser_css_reset(void)
{
    net_browser_css_in_style = 0u;
    net_browser_css_summary_added = 0u;
    net_browser_css_token_len = 0u;
    net_browser_css_after_display = 0u;
    net_browser_css_token[0] = 0;
    net_browser_css_reset_rule_parse();
    for (u32 i = 0u; i < NET_BROWSER_CSS_RULE_MAX; i += 1u) {
        net_browser_css_rule_kind[i] = 0u;
        net_browser_css_rule_flags[i] = 0u;
        net_browser_css_rule_style_flags[i] = 0u;
        net_browser_css_rule_color[i] = 0u;
        net_browser_css_rule_bg[i] = 0u;
        net_browser_css_rule_width[i] = 0u;
        net_browser_css_rule_height[i] = 0u;
        net_browser_css_rule_margin[i] = 0u;
        net_browser_css_rule_padding[i] = 0u;
        net_browser_css_rule_border[i] = 0u;
        net_browser_css_rule_selector[i][0] = 0;
        net_browser_css_rule_class[i][0] = 0;
    }
}

static u8 net_browser_css_token_starts(const char *prefix)
{
    u32 i = 0u;
    while (prefix[i] != 0) {
        if (net_browser_css_token[i] != prefix[i]) {
            return 0u;
        }
        i += 1u;
    }
    return 1u;
}

static void net_browser_css_finish_token(void)
{
    if (net_browser_css_token_len == 0u) {
        return;
    }
    net_browser_css_token[net_browser_css_token_len] = 0;
    if (net_text_is(net_browser_css_token, "color") ||
        net_text_is(net_browser_css_token, "background-color")) {
        net_browser_css_color_count += 1u;
    }
    if (net_browser_css_token_starts("font")) {
        net_browser_css_font_count += 1u;
    }
    if (net_text_is(net_browser_css_token, "url")) {
        net_browser_css_url_count += 1u;
    }
    if (net_browser_css_after_display &&
        net_text_is(net_browser_css_token, "none")) {
        net_browser_css_display_none_count += 1u;
    }
    net_browser_css_after_display = net_text_is(net_browser_css_token, "display");
    net_browser_css_token_len = 0u;
    net_browser_css_token[0] = 0;
}

static u8 net_browser_css_token_char(u8 ch)
{
    ch = net_ascii_lower(ch);
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-';
}

static void net_browser_css_feed(u8 ch)
{
    if (net_browser_css_bytes < 65535u) {
        net_browser_css_bytes += 1u;
    }
    net_browser_css_parse_feed(ch);
    if (ch == '{') {
        net_browser_css_rule_count += 1u;
    } else if (ch == ':') {
        net_browser_css_decl_count += 1u;
    } else if (ch == '#') {
        net_browser_css_color_count += 1u;
    }
    if (net_browser_css_token_char(ch)) {
        if (net_browser_css_token_len < sizeof(net_browser_css_token) - 1u) {
            net_browser_css_token[net_browser_css_token_len++] =
                (char) net_ascii_lower(ch);
        }
    } else {
        net_browser_css_finish_token();
    }
}

static void net_browser_css_inline_commit(char *prop, u8 prop_len,
                                          char *value, u8 value_len)
{
    u8 render_flags = 0u;
    prop[prop_len] = 0;
    value[value_len] = 0;
    if (prop_len == 0u) {
        return;
    }
    net_browser_css_apply_decl_fields(prop, value,
                                      &net_browser_inline_css_flags,
                                      &render_flags,
                                      &net_browser_inline_css_color,
                                      &net_browser_inline_css_bg,
                                      &net_browser_inline_css_width,
                                      &net_browser_inline_css_height,
                                      &net_browser_inline_css_margin,
                                      &net_browser_inline_css_padding,
                                      &net_browser_inline_css_border);
}

static void net_browser_css_parse_inline_style(const char *style)
{
    char prop[24];
    char value[48];
    u8 prop_len = 0u;
    u8 value_len = 0u;
    u8 state = 0u;
    prop[0] = 0;
    value[0] = 0;
    for (u32 i = 0u;; i += 1u) {
        u8 ch = (u8) style[i];
        u8 lower = net_ascii_lower(ch);
        if (state == 0u) {
            if (ch == 0) {
                break;
            }
            if (ch == ':') {
                state = 1u;
            } else if (ch == ';') {
                prop_len = 0u;
                value_len = 0u;
            } else if (!net_html_space(ch) &&
                       prop_len < sizeof(prop) - 1u &&
                       (net_css_ident_char(lower) || lower == '-')) {
                prop[prop_len++] = (char) lower;
            }
        } else {
            if (ch == ';' || ch == 0) {
                net_browser_css_inline_commit(prop, prop_len, value, value_len);
                prop_len = 0u;
                value_len = 0u;
                prop[0] = 0;
                value[0] = 0;
                state = 0u;
                if (ch == 0) {
                    break;
                }
            } else if (value_len < sizeof(value) - 1u &&
                       lower >= 32u && lower <= 126u) {
                value[value_len++] = (char) lower;
            }
        }
    }
}

static void net_browser_dom_reset(void)
{
    net_browser_dom_count = 0u;
    net_browser_dom_text_bytes = 0u;
    net_browser_dom_hidden_count = 0u;
    net_browser_dom_supported_count = 0u;
    net_browser_dom_block_count = 0u;
    net_browser_dom_inline_count = 0u;
    net_browser_dom_table_count = 0u;
    net_browser_dom_control_count = 0u;
    net_browser_dom_resource_count = 0u;
    net_browser_dom_unsupported_count = 0u;
    net_browser_dom_placeholder_count = 0u;
    net_browser_dom_depth = 0u;
    net_browser_dom_max_depth = 0u;
    net_browser_dom_stack_len = 0u;
    for (u32 i = 0u; i < NET_BROWSER_DOM_MAX; i += 1u) {
        net_browser_dom_parent[i] = 0xFFu;
        net_browser_dom_node_depth[i] = 0u;
        net_browser_dom_node_flags[i] = 0u;
        net_browser_dom_node_kind[i] = 0u;
        net_browser_dom_tag[i][0] = 0;
    }
    for (u32 i = 0u; i < NET_BROWSER_DOM_STACK_MAX; i += 1u) {
        net_browser_dom_stack[i] = 0xFFu;
    }
}

static void net_browser_dom_open_current_tag(void)
{
    if (net_browser_html_tag_len == 0u || net_browser_html_tag[0] == '/' ||
        net_browser_dom_count >= NET_BROWSER_DOM_MAX) {
        return;
    }
    u16 slot16 = net_browser_dom_count++;
    u8 slot = (u8) slot16;
    net_browser_dom_parent[slot] = net_browser_dom_stack_len != 0u
        ? net_browser_dom_stack[net_browser_dom_stack_len - 1u]
        : 0xFFu;
    net_browser_dom_node_depth[slot] = net_browser_dom_depth;
    net_browser_dom_node_flags[slot] = net_browser_render_tag_flags;
    u8 kind = net_browser_dom_kind_for_current_tag();
    net_browser_dom_node_kind[slot] = kind;
    net_browser_dom_note_kind(kind);
    if ((net_browser_render_tag_flags & NET_BROWSER_RENDER_FLAG_HIDDEN) != 0u &&
        net_browser_dom_hidden_count < 65535u) {
        net_browser_dom_hidden_count += 1u;
    }
    u32 i = 0u;
    for (; i < sizeof(net_browser_dom_tag[slot]) - 1u &&
           i < net_browser_html_tag_len &&
           net_browser_html_tag[i] != 0; i += 1u) {
        net_browser_dom_tag[slot][i] = net_browser_html_tag[i];
    }
    net_browser_dom_tag[slot][i] = 0;
    if (!net_tag_is_void_element()) {
        if (net_browser_dom_stack_len < NET_BROWSER_DOM_STACK_MAX) {
            net_browser_dom_stack[net_browser_dom_stack_len++] = slot;
        }
        if (net_browser_dom_depth < 255u) {
            net_browser_dom_depth += 1u;
            if (net_browser_dom_depth > net_browser_dom_max_depth) {
                net_browser_dom_max_depth = net_browser_dom_depth;
            }
        }
    }
}

static void net_browser_dom_close_current_tag(void)
{
    if (net_browser_html_tag_len < 2u || net_browser_html_tag[0] != '/') {
        return;
    }
    if (net_browser_dom_depth != 0u) {
        net_browser_dom_depth -= 1u;
    }
    for (u32 scan = net_browser_dom_stack_len; scan != 0u; scan -= 1u) {
        u32 idx = scan - 1u;
        u8 node = net_browser_dom_stack[idx];
        if (node < NET_BROWSER_DOM_MAX &&
            net_text_is(net_browser_dom_tag[node], &net_browser_html_tag[1])) {
            net_browser_dom_stack_len = (u8) idx;
            return;
        }
    }
    if (net_browser_dom_stack_len != 0u) {
        net_browser_dom_stack_len -= 1u;
    }
}

static void net_browser_layout_reset(u16 width, u16 height)
{
    net_browser_layout_count = 0u;
    net_browser_layout_line_count = 0u;
    net_browser_layout_link_count = 0u;
    net_browser_layout_control_count = 0u;
    net_browser_layout_block_count = 0u;
    net_browser_layout_inline_count = 0u;
    net_browser_layout_table_count = 0u;
    net_browser_layout_box_count = 0u;
    net_browser_layout_wrap_count = 0u;
    net_browser_layout_margin_count = 0u;
    net_browser_layout_padding_count = 0u;
    net_browser_layout_border_count = 0u;
    net_browser_layout_width = width;
    net_browser_layout_height = height;
    for (u32 i = 0u; i < NET_BROWSER_LAYOUT_MAX; i += 1u) {
        net_browser_layout_x[i] = 0u;
        net_browser_layout_y[i] = 0u;
        net_browser_layout_w[i] = 0u;
        net_browser_layout_h[i] = 0u;
        net_browser_layout_kind[i] = 0u;
        net_browser_layout_flags[i] = 0u;
        net_browser_layout_link[i] = 0xFFu;
        net_browser_layout_control[i] = 0xFFu;
    }
}

static void net_browser_layout_record(u16 x, u16 y, u16 w, u16 h,
                                      u8 kind, u8 flags, u8 link_index,
                                      u8 control_index)
{
    if (net_browser_layout_line_count < 65535u) {
        net_browser_layout_line_count += 1u;
    }
    if (kind == NET_BROWSER_RENDER_KIND_LINK &&
        net_browser_layout_link_count < 65535u) {
        net_browser_layout_link_count += 1u;
    }
    if ((flags & (NET_BROWSER_RENDER_FLAG_INPUT |
                  NET_BROWSER_RENDER_FLAG_BUTTON)) != 0u &&
        net_browser_layout_control_count < 65535u) {
        net_browser_layout_control_count += 1u;
    }
    u8 block_like =
        (flags & (NET_BROWSER_RENDER_FLAG_BOX |
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
    if (kind == NET_BROWSER_RENDER_KIND_TABLE &&
        net_browser_layout_table_count < 65535u) {
        net_browser_layout_table_count += 1u;
    }
    if (block_like) {
        if (net_browser_layout_block_count < 65535u) {
            net_browser_layout_block_count += 1u;
        }
    } else if (net_browser_layout_inline_count < 65535u) {
        net_browser_layout_inline_count += 1u;
    }
    if ((flags & (NET_BROWSER_RENDER_FLAG_BOX |
                  NET_BROWSER_RENDER_FLAG_INPUT |
                  NET_BROWSER_RENDER_FLAG_BUTTON)) != 0u ||
        kind == NET_BROWSER_RENDER_KIND_IMAGE ||
        kind == NET_BROWSER_RENDER_KIND_TABLE ||
        kind == NET_BROWSER_RENDER_KIND_QUOTE ||
        kind == NET_BROWSER_RENDER_KIND_META ||
        kind == NET_BROWSER_RENDER_KIND_EMBED ||
        kind == NET_BROWSER_RENDER_KIND_DIALOG ||
        kind == NET_BROWSER_RENDER_KIND_CSS ||
        kind == NET_BROWSER_RENDER_KIND_JS) {
        if (net_browser_layout_box_count < 65535u) {
            net_browser_layout_box_count += 1u;
        }
        if (net_browser_layout_padding_count < 65535u) {
            net_browser_layout_padding_count += 1u;
        }
        if (net_browser_layout_border_count < 65535u) {
            net_browser_layout_border_count += 1u;
        }
    }
    if (x != 0u && net_browser_layout_margin_count < 65535u) {
        net_browser_layout_margin_count += 1u;
    }
    if (net_browser_layout_count >= NET_BROWSER_LAYOUT_MAX) {
        return;
    }
    u16 slot = net_browser_layout_count++;
    net_browser_layout_x[slot] = x;
    net_browser_layout_y[slot] = y;
    net_browser_layout_w[slot] = w;
    net_browser_layout_h[slot] = h;
    net_browser_layout_kind[slot] = kind;
    net_browser_layout_flags[slot] = flags;
    net_browser_layout_link[slot] = link_index;
    net_browser_layout_control[slot] = control_index;
}

static void net_browser_layout_note_wrap(void)
{
    if (net_browser_layout_wrap_count < 65535u) {
        net_browser_layout_wrap_count += 1u;
    }
}

static void net_browser_link_click(u8 link_index)
{
    if (link_index >= net_browser_link_target_count) {
        return;
    }
    net_browser_last_clicked_link = link_index;
    if (net_browser_link_click_count < 65535u) {
        net_browser_link_click_count += 1u;
    }
    net_append_capped(net_browser_clicked_url, 0u,
                      sizeof(net_browser_clicked_url),
                      net_browser_link_url[link_index]);
    net_set_last("BROWSER LINK CLICK");
    serial_print(msg_net_browser_link_click);
    serial_print_dec(link_index);
    serial_write(' ');
    serial_print(net_browser_clicked_url);
    serial_print("\r\n");
    if (net_browser_navigation_count < 65535u) {
        net_browser_navigation_count += 1u;
    }
    serial_print(msg_net_browser_navigate);
    serial_print(net_browser_clicked_url);
    serial_print("\r\n");
    net_browser_open_url(net_browser_clicked_url);
    dirty = 1;
}

static void net_browser_layout_maybe_print(void)
{
    if (net_browser_layout_serial_sent ||
        net_browser_layout_count == 0u ||
        !net_browser_html_structure_sent) {
        return;
    }
    net_browser_layout_serial_sent = 1u;
    serial_print(msg_net_browser_layout);
    serial_print_dec(net_browser_layout_count);
    serial_print(" lines ");
    serial_print_dec(net_browser_layout_line_count);
    serial_print(" links ");
    serial_print_dec(net_browser_layout_link_count);
    serial_print(" controls ");
    serial_print_dec(net_browser_layout_control_count);
    serial_print(" block ");
    serial_print_dec(net_browser_layout_block_count);
    serial_print(" inline ");
    serial_print_dec(net_browser_layout_inline_count);
    serial_print(" table ");
    serial_print_dec(net_browser_layout_table_count);
    serial_print(" boxes ");
    serial_print_dec(net_browser_layout_box_count);
    serial_print(" wraps ");
    serial_print_dec(net_browser_layout_wrap_count);
    serial_print(" margin ");
    serial_print_dec(net_browser_layout_margin_count);
    serial_print(" padding ");
    serial_print_dec(net_browser_layout_padding_count);
    serial_print(" border ");
    serial_print_dec(net_browser_layout_border_count);
    serial_print(" viewport ");
    serial_print_dec(net_browser_layout_width);
    serial_write('x');
    serial_print_dec(net_browser_layout_height);
    for (u32 i = 0u; i < net_browser_layout_count; i += 1u) {
        if (net_browser_layout_kind[i] == NET_BROWSER_RENDER_KIND_LINK &&
            net_browser_layout_link[i] != 0xFFu) {
            serial_print(" firstlink ");
            serial_print_dec(net_browser_layout_link[i]);
            serial_write(' ');
            serial_print_dec(net_browser_layout_x[i]);
            serial_write(' ');
            serial_print_dec(net_browser_layout_y[i]);
            serial_write(' ');
            serial_print_dec(net_browser_layout_w[i]);
            serial_write(' ');
            serial_print_dec(net_browser_layout_h[i]);
            serial_write(' ');
            serial_print(net_browser_link_url[net_browser_layout_link[i]]);
            break;
        }
    }
    serial_print("\r\n");
}

static void net_browser_js_reset(void)
{
    net_browser_js_in_script = 0u;
    net_browser_js_summary_added = 0u;
    net_browser_js_token_len = 0u;
    net_browser_js_recent_len = 0u;
    net_browser_js_docwrite_state = 0u;
    net_browser_js_write_quote = 0u;
    net_browser_js_write_len = 0u;
    net_browser_js_token[0] = 0;
    net_browser_js_recent[0] = 0;
    net_browser_js_write_buf[0] = 0;
}

static u8 net_browser_js_token_char(u8 ch)
{
    ch = net_ascii_lower(ch);
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '$';
}

static void net_browser_js_finish_token(void)
{
    if (net_browser_js_token_len == 0u) {
        return;
    }
    net_browser_js_token[net_browser_js_token_len] = 0;
    if (net_browser_js_token_count < 65535u) {
        net_browser_js_token_count += 1u;
    }
    if (net_text_is(net_browser_js_token, "function")) {
        net_browser_js_function_count += 1u;
    } else if (net_text_is(net_browser_js_token, "var") ||
               net_text_is(net_browser_js_token, "let") ||
               net_text_is(net_browser_js_token, "const")) {
        net_browser_js_var_count += 1u;
    } else if (net_text_is(net_browser_js_token, "document")) {
        net_browser_js_document_count += 1u;
    } else if (net_text_is(net_browser_js_token, "window")) {
        net_browser_js_window_count += 1u;
    } else if (net_text_is(net_browser_js_token, "location")) {
        net_browser_js_location_count += 1u;
    }
    net_browser_js_token_len = 0u;
    net_browser_js_token[0] = 0;
}

static void net_browser_js_recent_push(u8 ch)
{
    ch = net_ascii_lower(ch);
    if (ch < 32u || ch > 126u) {
        return;
    }
    if (net_browser_js_recent_len < sizeof(net_browser_js_recent) - 1u) {
        net_browser_js_recent[net_browser_js_recent_len++] = (char) ch;
    } else {
        for (u32 i = 1u; i < sizeof(net_browser_js_recent) - 1u; i += 1u) {
            net_browser_js_recent[i - 1u] = net_browser_js_recent[i];
        }
        net_browser_js_recent[sizeof(net_browser_js_recent) - 2u] = (char) ch;
    }
    net_browser_js_recent[net_browser_js_recent_len] = 0;
}

static u8 net_browser_js_recent_ends(const char *needle)
{
    u32 needle_len = 0u;
    while (needle[needle_len] != 0) {
        needle_len += 1u;
    }
    if (needle_len == 0u || net_browser_js_recent_len < needle_len) {
        return 0u;
    }
    u32 start = net_browser_js_recent_len - needle_len;
    for (u32 i = 0u; i < needle_len; i += 1u) {
        if (net_browser_js_recent[start + i] != needle[i]) {
            return 0u;
        }
    }
    return 1u;
}

static void net_browser_js_emit_write(void)
{
    net_browser_js_write_buf[net_browser_js_write_len] = 0;
    if (net_browser_js_write_len != 0u) {
        net_browser_js_write_count += 1u;
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_JS,
                                           NET_BROWSER_RENDER_FLAG_BOX,
                                           "JS STATIC PREVIEW ",
                                           net_browser_js_write_buf);
    }
    net_browser_js_write_len = 0u;
    net_browser_js_write_buf[0] = 0;
}

static void net_browser_js_docwrite_feed(u8 ch)
{
    if (net_browser_js_docwrite_state == 3u) {
        if (ch == net_browser_js_write_quote) {
            net_browser_js_emit_write();
            net_browser_js_docwrite_state = 0u;
            net_browser_js_write_quote = 0u;
            return;
        }
        if (ch >= 32u && ch <= 126u &&
            net_browser_js_write_len < NET_BROWSER_JS_WRITE_MAX - 1u) {
            net_browser_js_write_buf[net_browser_js_write_len++] = (char) ch;
        }
        return;
    }
    if (net_browser_js_docwrite_state == 2u) {
        if (ch == '"' || ch == '\'') {
            net_browser_js_write_quote = ch;
            net_browser_js_write_len = 0u;
            net_browser_js_write_buf[0] = 0;
            net_browser_js_docwrite_state = 3u;
        } else if (!net_html_space(ch)) {
            net_browser_js_docwrite_state = 0u;
        }
        return;
    }
    if (net_browser_js_docwrite_state == 1u) {
        if (ch == '(') {
            net_browser_js_docwrite_state = 2u;
        } else if (!net_html_space(ch)) {
            net_browser_js_docwrite_state = 0u;
        }
    }
}

static void net_browser_js_feed(u8 ch)
{
    if (net_browser_js_bytes < 65535u) {
        net_browser_js_bytes += 1u;
    }
    net_browser_js_docwrite_feed(ch);
    u8 lower = net_ascii_lower(ch);
    net_browser_js_recent_push(lower);
    if (net_browser_js_recent_ends("document.write")) {
        net_browser_js_docwrite_state = 1u;
    }
    if (net_browser_js_token_char(lower)) {
        if (net_browser_js_token_len < sizeof(net_browser_js_token) - 1u) {
            net_browser_js_token[net_browser_js_token_len++] = (char) lower;
        }
    } else {
        net_browser_js_finish_token();
    }
}

static void net_browser_js_add_summary(void)
{
    if (net_browser_js_summary_added ||
        (net_browser_script_count == 0u && net_browser_js_bytes == 0u)) {
        return;
    }
    net_browser_js_summary_added = 1u;
    net_browser_note_placeholder();
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_JS,
                                       NET_BROWSER_RENDER_FLAG_BOX,
                                       "JS DISABLED ",
                                       "strict NOJS mode; scripts are parsed for warnings only");
}

static void net_browser_unsupported_add_summary(void)
{
    if (net_browser_unsupported_summary_added) {
        return;
    }
    net_browser_unsupported_summary_added = 1u;
    net_browser_unsupported_feature_count =
        net_browser_script_count +
        net_browser_event_attr_count +
        net_browser_js_url_count +
        net_browser_noscript_count +
        net_browser_svg_count +
        net_browser_svg_shape_count +
        net_browser_picture_count +
        net_browser_media_count +
        net_browser_custom_count +
        net_browser_template_count +
        net_browser_srcset_count;
    net_browser_js_required =
        (net_browser_script_count != 0u ||
         net_browser_event_attr_count != 0u ||
         net_browser_js_url_count != 0u ||
         net_browser_noscript_count != 0u ||
         net_browser_js_document_count != 0u ||
         net_browser_js_window_count != 0u ||
         net_browser_js_location_count != 0u) ? 1u : 0u;
    if (net_browser_js_required) {
        net_browser_note_placeholder();
        net_browser_render_insert_block_flags(NET_BROWSER_RENDER_KIND_JS,
            (u8) (NET_BROWSER_RENDER_FLAG_BOX | NET_BROWSER_RENDER_FLAG_BOLD),
            "NOJS MODE: ",
            "JavaScript is required for full page behavior; scripts are not executed");
        serial_print("LeonOS net browser unsupported banner NOJS\r\n");
    } else if (net_browser_unsupported_feature_count != 0u) {
        net_browser_note_placeholder();
        net_browser_render_insert_block_flags(NET_BROWSER_RENDER_KIND_EMBED,
            (u8) (NET_BROWSER_RENDER_FLAG_BOX | NET_BROWSER_RENDER_FLAG_BOLD),
            "UNSUPPORTED: ",
            "static HTML/CSS only; unsupported features are labeled");
        serial_print("LeonOS net browser unsupported banner STATIC\r\n");
    }
}

static void net_browser_attr_reset_current(void)
{
    net_browser_html_attr_state = 0u;
    net_browser_html_attr_quote = 0u;
    net_browser_html_attr_name_len = 0u;
    net_browser_html_attr_value_len = 0u;
    net_browser_html_attr_name[0] = 0;
    net_browser_html_attr_value[0] = 0;
}

static void net_copy_attr_value(char *dst, u32 dst_len)
{
    if (dst_len == 0u) {
        return;
    }
    u32 i = 0u;
    while (i + 1u < dst_len && net_browser_html_attr_value[i] != 0) {
        dst[i] = net_browser_html_attr_value[i];
        i += 1u;
    }
    dst[i] = 0;
}

static void net_copy_attr_value_lower(char *dst, u32 dst_len)
{
    if (dst_len == 0u) {
        return;
    }
    u32 i = 0u;
    while (i + 1u < dst_len && net_browser_html_attr_value[i] != 0) {
        dst[i] = (char) net_ascii_lower((u8) net_browser_html_attr_value[i]);
        i += 1u;
    }
    dst[i] = 0;
}

static u8 net_char_eq_ci(char left, char right)
{
    u8 a = net_ascii_lower((u8) left);
    u8 b = net_ascii_lower((u8) right);
    return a == b;
}

static u8 net_text_starts_with_ci(const char *text, const char *prefix)
{
    u32 i = 0u;
    while (prefix[i] != 0) {
        if (text[i] == 0 || !net_char_eq_ci(text[i], prefix[i])) {
            return 0u;
        }
        i += 1u;
    }
    return 1u;
}

static u32 net_append_capped(char *dst, u32 pos, u32 dst_len, const char *src)
{
    if (dst_len == 0u) {
        return 0u;
    }
    for (u32 i = 0u; src[i] != 0 && pos + 1u < dst_len; i += 1u) {
        dst[pos++] = src[i];
    }
    dst[pos] = 0;
    return pos;
}

static u32 net_append_u32_capped(char *dst, u32 pos, u32 dst_len, u32 value)
{
    char tmp[10];
    u32 n = 0u;
    if (value == 0u) {
        return net_append_capped(dst, pos, dst_len, "0");
    }
    while (value != 0u && n < sizeof(tmp)) {
        tmp[n++] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
    while (n != 0u && pos + 1u < dst_len) {
        dst[pos++] = tmp[--n];
    }
    if (dst_len != 0u) {
        dst[pos] = 0;
    }
    return pos;
}

static void net_browser_copy_text(char *dst, u32 dst_len, const char *src)
{
    net_append_capped(dst, 0u, dst_len, src);
}

static void net_browser_set_state(const char *state)
{
    net_browser_copy_text(net_browser_state, sizeof(net_browser_state), state);
    serial_print(msg_net_browser_state);
    serial_print(net_browser_state);
    serial_write(' ');
    serial_print(net_browser_current_url);
    serial_print("\r\n");
}

static u8 net_browser_can_back(void)
{
    return net_browser_history_count != 0u && net_browser_history_index != 0u;
}

static u8 net_browser_can_forward(void)
{
    return net_browser_history_count != 0u &&
           net_browser_history_index + 1u < net_browser_history_count;
}

static u8 net_browser_is_loading(void)
{
    return net_browser_fetch_enabled ||
           net_browser_resource_fetch_pending ||
           (net_browser_resource_fetch_started && !net_browser_resource_fetch_done);
}

static void net_browser_history_save_scroll(void)
{
    if (net_browser_history_count != 0u &&
        net_browser_history_index < NET_BROWSER_HISTORY_MAX) {
        net_browser_history_scroll[net_browser_history_index] = net_browser_scroll;
    }
}

static void net_browser_history_record(const char *url)
{
    if (net_browser_history_suppress || url[0] == 0) {
        return;
    }
    if (net_browser_history_count != 0u &&
        net_browser_history_index < net_browser_history_count &&
        net_text_is(net_browser_history[net_browser_history_index], url)) {
        return;
    }
    if (net_browser_history_count != 0u &&
        net_browser_history_index + 1u < net_browser_history_count) {
        net_browser_history_count = (u8) (net_browser_history_index + 1u);
    }
    if (net_browser_history_count < NET_BROWSER_HISTORY_MAX) {
        net_browser_history_index = net_browser_history_count++;
    } else {
        for (u32 i = 1u; i < NET_BROWSER_HISTORY_MAX; i += 1u) {
            net_browser_copy_text(net_browser_history[i - 1u],
                                  NET_BROWSER_RESOURCE_URL_MAX,
                                  net_browser_history[i]);
            net_browser_history_scroll[i - 1u] = net_browser_history_scroll[i];
        }
        net_browser_history_index = NET_BROWSER_HISTORY_MAX - 1u;
    }
    net_browser_copy_text(net_browser_history[net_browser_history_index],
                          NET_BROWSER_RESOURCE_URL_MAX, url);
    net_browser_history_scroll[net_browser_history_index] = 0u;
    serial_print(msg_net_browser_history);
    serial_print("push ");
    serial_print_dec(net_browser_history_index);
    serial_write('/');
    serial_print_dec(net_browser_history_count);
    serial_write(' ');
    serial_print(url);
    serial_print("\r\n");
}

static void net_cookie_clear(void)
{
    net_cookie_host[0] = 0;
    net_cookie_count = 0u;
    for (u32 i = 0u; i < NET_COOKIE_MAX; i += 1u) {
        net_cookie_name[i][0] = 0;
        net_cookie_value[i][0] = 0;
    }
}

static void net_cookie_copy_ascii(char *dst, u32 dst_len,
                                  const u8 *src, u32 src_len)
{
    if (dst_len == 0u) {
        return;
    }
    u32 out = 0u;
    for (u32 i = 0u; i < src_len && out + 1u < dst_len; i += 1u) {
        u8 ch = src[i];
        if (ch < 33u || ch > 126u || ch == ';') {
            break;
        }
        dst[out++] = (char) ch;
    }
    dst[out] = 0;
}

static u8 net_cookie_line_starts_set_cookie(const u8 *line, u32 len)
{
    const char name[] = "set-cookie:";
    for (u32 i = 0u; name[i] != 0; i += 1u) {
        if (i >= len || !net_char_eq_ci((char) line[i], name[i])) {
            return 0u;
        }
    }
    return 1u;
}

static void net_cookie_store_pair(const u8 *name, u32 name_len,
                                  const u8 *value, u32 value_len)
{
    if (name_len == 0u || name_len >= NET_COOKIE_NAME_MAX ||
        value_len == 0u) {
        return;
    }
    if (net_cookie_host[0] == 0) {
        net_append_capped(net_cookie_host, 0u,
                          sizeof(net_cookie_host), net_browser_host);
    }
    u8 slot = 0xFFu;
    for (u32 i = 0u; i < net_cookie_count; i += 1u) {
        u8 same = 1u;
        for (u32 n = 0u; n < name_len || net_cookie_name[i][n] != 0; n += 1u) {
            if (n >= name_len ||
                net_cookie_name[i][n] == 0 ||
                !net_char_eq_ci(net_cookie_name[i][n], (char) name[n])) {
                same = 0u;
                break;
            }
        }
        if (same) {
            slot = (u8) i;
            break;
        }
    }
    if (slot == 0xFFu) {
        if (net_cookie_count >= NET_COOKIE_MAX) {
            return;
        }
        slot = net_cookie_count++;
    }
    net_cookie_copy_ascii(net_cookie_name[slot],
                          NET_COOKIE_NAME_MAX, name, name_len);
    net_cookie_copy_ascii(net_cookie_value[slot],
                          NET_COOKIE_VALUE_MAX, value, value_len);
}

static void net_cookie_capture_headers(const u8 *headers, u32 header_len)
{
    if (net_tls_fetch_kind != 0u) {
        return;
    }
    u32 line = 0u;
    while (line < header_len) {
        u32 end = line;
        while (end < header_len && headers[end] != '\r' && headers[end] != '\n') {
            end += 1u;
        }
        if (net_cookie_line_starts_set_cookie(headers + line, end - line)) {
            u32 pos = line + sizeof("set-cookie:") - 1u;
            while (pos < end && (headers[pos] == ' ' || headers[pos] == '\t')) {
                pos += 1u;
            }
            u32 name_start = pos;
            while (pos < end && headers[pos] != '=' &&
                   headers[pos] != ';' && headers[pos] != ' ') {
                pos += 1u;
            }
            if (pos < end && headers[pos] == '=') {
                u32 name_len = pos - name_start;
                pos += 1u;
                u32 value_start = pos;
                while (pos < end && headers[pos] != ';' &&
                       headers[pos] != '\r' && headers[pos] != '\n') {
                    pos += 1u;
                }
                net_cookie_store_pair(headers + name_start, name_len,
                                      headers + value_start,
                                      pos - value_start);
            }
        }
        while (end < header_len && (headers[end] == '\r' || headers[end] == '\n')) {
            end += 1u;
        }
        line = end;
    }
}

static u32 net_cookie_append_request_header(char *dst, u32 pos, u32 dst_len)
{
    if (net_cookie_count == 0u ||
        !net_text_is(net_cookie_host, net_browser_host)) {
        return pos;
    }
    pos = net_append_capped(dst, pos, dst_len, "\r\nCookie: ");
    for (u32 i = 0u; i < net_cookie_count; i += 1u) {
        if (i != 0u) {
            pos = net_append_capped(dst, pos, dst_len, "; ");
        }
        pos = net_append_capped(dst, pos, dst_len, net_cookie_name[i]);
        pos = net_append_capped(dst, pos, dst_len, "=");
        pos = net_append_capped(dst, pos, dst_len, net_cookie_value[i]);
    }
    return pos;
}

static u32 net_browser_append_origin(char *dst, u32 dst_len)
{
    u32 pos = net_append_capped(dst, 0u, dst_len, "https://");
    return net_append_capped(dst, pos, dst_len, net_browser_host);
}

static u8 net_browser_normalize_resource(const char *value, char *dst, u32 dst_len)
{
    if (dst_len == 0u) {
        return 0u;
    }
    dst[0] = 0;
    if (value[0] == 0 || value[0] == '#') {
        return 0u;
    }
    if (net_text_starts_with_ci(value, "javascript:") ||
        net_text_starts_with_ci(value, "mailto:") ||
        net_text_starts_with_ci(value, "data:")) {
        return 0u;
    }
    if (net_text_starts_with_ci(value, "https://") ||
        net_text_starts_with_ci(value, "http://")) {
        net_append_capped(dst, 0u, dst_len, value);
        return 1u;
    }
    if (value[0] == '/' && value[1] == '/') {
        u32 pos = net_append_capped(dst, 0u, dst_len, "https:");
        net_append_capped(dst, pos, dst_len, value);
        return 1u;
    }
    if (value[0] == '/') {
        u32 pos = net_browser_append_origin(dst, dst_len);
        net_append_capped(dst, pos, dst_len, value);
        return 1u;
    }
    if (value[0] == '.' && value[1] == '/') {
        u32 pos = net_browser_append_origin(dst, dst_len);
        pos = net_append_capped(dst, pos, dst_len, "/");
        net_append_capped(dst, pos, dst_len, value + 2u);
        return 1u;
    }
    for (u32 i = 0u; value[i] != 0; i += 1u) {
        if (value[i] == ':') {
            return 0u;
        }
    }
    u32 pos = net_browser_append_origin(dst, dst_len);
    pos = net_append_capped(dst, pos, dst_len, "/");
    net_append_capped(dst, pos, dst_len, value);
    return 1u;
}

static u8 net_browser_parse_url(const char *url, char *host, u32 host_len,
                                char *path, u32 path_len,
                                char *full, u32 full_len)
{
    u32 i = 0u;
    while (url[i] == ' ' || url[i] == '\t') {
        i += 1u;
    }
    const char *src = url + i;
    if (net_text_starts_with_ci(src, "http://")) {
        return 0u;
    }
    if (net_text_starts_with_ci(src, "https://")) {
        src += 8u;
    }
    if (src[0] == 0) {
        return 0u;
    }

    u32 h = 0u;
    while (src[h] != 0 && src[h] != '/' && src[h] != '?' &&
           src[h] != ' ' && src[h] != '\t') {
        if (!net_dns_host_char(src[h]) || src[h] == '.') {
            if (src[h] == '.' && (h == 0u || src[h + 1u] == 0 ||
                                  src[h + 1u] == '/' || src[h + 1u] == '?')) {
                return 0u;
            }
            if (src[h] != '.') {
                return 0u;
            }
        }
        if (h + 1u >= host_len) {
            return 0u;
        }
        char ch = src[h];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char) (ch + ('a' - 'A'));
        }
        host[h] = ch;
        h += 1u;
    }
    if (h == 0u) {
        return 0u;
    }
    host[h] = 0;

    u32 p = 0u;
    if (src[h] == 0) {
        if (path_len < 2u) {
            return 0u;
        }
        path[p++] = '/';
    } else if (src[h] == '?') {
        if (path_len < 3u) {
            return 0u;
        }
        path[p++] = '/';
        path[p++] = '?';
        h += 1u;
    } else if (src[h] == '/') {
        path[p++] = '/';
        h += 1u;
    } else {
        return 0u;
    }
    while (src[h] != 0 && src[h] != ' ' && src[h] != '\t') {
        if ((u8) src[h] < 33u || (u8) src[h] > 126u ||
            p + 1u >= path_len) {
            return 0u;
        }
        path[p++] = src[h++];
    }
    path[p] = 0;
    while (src[h] == ' ' || src[h] == '\t') {
        h += 1u;
    }
    if (src[h] != 0) {
        return 0u;
    }

    u32 out = net_append_capped(full, 0u, full_len, "https://");
    out = net_append_capped(full, out, full_len, host);
    net_append_capped(full, out, full_len, path);
    return 1u;
}

static void net_browser_reset_primary_fetch(void)
{
    net_dns_source_port += 0x10u;
    if (net_dns_source_port < NET_DNS_SOURCE_PORT ||
        net_dns_source_port > NET_DNS_SOURCE_PORT + 512u) {
        net_dns_source_port = NET_DNS_SOURCE_PORT + 0x10u;
    }
    net_dns_txid += 1u;
    if (net_dns_txid == 0u) {
        net_dns_txid = NET_DNS_TXID + 1u;
    }
    net_dns_query_sent = 0u;
    net_dns_reply_seen = 0u;
    net_dns_query_tick = 0u;
    net_dns_retry_count = 0u;
    net_dns_rx_diag_count = 0u;
    mem_zero(net_dns_a_record, sizeof(net_dns_a_record));
    mem_zero((u8 *) net_dns_a_records, sizeof(net_dns_a_records));
    net_dns_a_count = 0u;
    net_dns_a_index = 0u;
    net_tcp_syn_sent = 0u;
    net_tcp_connected = 0u;
    net_http_get_sent = 0u;
    net_http_response_seen = 0u;
    net_tcp_next_seq = 0u;
    net_tcp_ack = 0u;
    net_tls_syn_sent = 0u;
    net_tls_connected = 0u;
    net_tls_syn_retry_count = 0u;
    net_tls_syn_tick = 0u;
    net_tls_fin_pending = 0u;
    net_tls_fin_seq = 0u;
    net_tls_client_hello_sent = 0u;
    net_tls_server_hello_seen = 0u;
    net_tls_fetch_kind = 0u;
    if (net_tls_primary_source_port_next < NET_TLS_SOURCE_PORT ||
        net_tls_primary_source_port_next > NET_TLS_SOURCE_PORT + 128u) {
        net_tls_primary_source_port_next = NET_TLS_SOURCE_PORT;
    }
    net_tls_source_port = net_tls_primary_source_port_next;
    net_tls_primary_source_port_next += 1u;
    if (net_tls_primary_source_port_next > NET_TLS_SOURCE_PORT + 128u) {
        net_tls_primary_source_port_next = NET_TLS_SOURCE_PORT;
    }
    net_tls_next_seq = 0u;
    net_tls_ack = 0u;
    net_tls_rx_len = 0u;
    net_tls_clear_out_of_order();
    net_tls_ooo_log_count = 0u;
    net_tls_server_hs_seq = 0u;
    net_tls_client_hs_seq = 0u;
    net_tls_server_app_seq = 0u;
    net_tls_client_app_seq = 0u;
    net_tls_handshake_keys_ready = 0u;
    net_tls_server_finished_seen = 0u;
    net_tls_client_finished_sent = 0u;
    net_tls_primary_done = 0u;
    net_tls_primary_close_sent = 0u;
    net_tls_primary_done_tick = 0u;
    net_https_get_sent = 0u;
    net_https_get_tick = 0u;
    net_https_response_seen = 0u;
    net_https_headers_done = 0u;
    net_https_chunked = 0u;
    net_https_body_complete = 0u;
    net_browser_finalize_sent = 0u;
    net_https_body_last_tick = 0u;
    net_https_headers_tick = 0u;
    net_https_header_len = 0u;
    net_https_header_log_sent = 0u;
    net_https_body_raw_log_count = 0u;
    net_https_body_raw_total = 0u;
    net_https_body_decoded_log_count = 0u;
    net_https_body_decoded_total = 0u;
    net_https_body_decoded_total32 = 0u;
    net_http_chunk_state = 0u;
    net_http_chunk_seen_digit = 0u;
    net_http_chunk_skip_ext = 0u;
    net_http_chunk_remaining = 0u;
    net_https_status[0] = '-';
    net_https_status[1] = '-';
    net_https_status[2] = '-';
    net_https_status[3] = 0;
}

static void net_browser_set_error_page(const char *title, const char *detail,
                                       const char *url)
{
    if (net_user_fetch_active) {
        (void) title;
        (void) detail;
        (void) url;
        net_user_fetch_finish(0u);
        return;
    }
    net_tls_abort_current_for_navigation();
    net_browser_fetch_enabled = 0u;
    net_browser_resource_fetch_pending = 0u;
    net_browser_resource_fetch_started = 0u;
    net_browser_resource_fetch_done = 1u;
    net_browser_text_reset();
    net_browser_text_len = 0u;
    net_browser_text_len = net_append_capped(net_browser_text,
        net_browser_text_len, sizeof(net_browser_text), title);
    net_browser_text_len = net_append_capped(net_browser_text,
        net_browser_text_len, sizeof(net_browser_text), "\n\n");
    net_browser_text_len = net_append_capped(net_browser_text,
        net_browser_text_len, sizeof(net_browser_text), detail);
    net_browser_text_len = net_append_capped(net_browser_text,
        net_browser_text_len, sizeof(net_browser_text), "\n");
    net_browser_text_len = net_append_capped(net_browser_text,
        net_browser_text_len, sizeof(net_browser_text), url);
    net_browser_status[0] = 'E';
    net_browser_status[1] = 'R';
    net_browser_status[2] = 'R';
    net_browser_status[3] = 0;
    net_browser_line[0] = 'U';
    net_browser_line[1] = 'R';
    net_browser_line[2] = 'L';
    net_browser_line[3] = ' ';
    net_browser_line[4] = 'N';
    net_browser_line[5] = 'O';
    net_browser_line[6] = 'T';
    net_browser_line[7] = ' ';
    net_browser_line[8] = 'L';
    net_browser_line[9] = 'O';
    net_browser_line[10] = 'A';
    net_browser_line[11] = 'D';
    net_browser_line[12] = 'E';
    net_browser_line[13] = 'D';
    net_browser_line[14] = 0;
    net_browser_text_ready = 1u;
    if (net_browser_history_error_count < 65535u) {
        net_browser_history_error_count += 1u;
    }
    net_browser_set_state("ERROR");
    dirty = 1;
}

static void net_browser_open_url_internal(const char *url, u8 add_history,
                                          u8 restore_scroll)
{
    char *host = net_browser_open_host_scratch;
    char *path = net_browser_open_path_scratch;
    char *full = net_browser_open_full_scratch;
    u32 saved_scroll = net_browser_scroll;
    if (!net_browser_parse_url(url, host, NET_BROWSER_HOST_MAX,
                               path, NET_BROWSER_PATH_MAX,
                               full, NET_BROWSER_RESOURCE_URL_MAX)) {
        net_browser_set_error_page("HTTPS URL ERROR",
            "Only real https://host/path URLs are supported by this tiny client.",
            url);
        net_set_last("BROWSER URL BAD");
        serial_print("LeonOS net browser URL rejected ");
        serial_print(url);
        serial_print("\r\n");
        return;
    }
    net_browser_history_save_scroll();
    if (!net_user_fetch_active) {
        net_user_fetch_ignore_tail = 0u;
    }
    net_tls_abort_current_for_navigation();
    net_tls_primary_source_port_next = NET_TLS_SOURCE_PORT;
    if (net_cookie_host[0] != 0 &&
        !net_text_is(net_cookie_host, host)) {
        net_cookie_clear();
    }
    net_append_capped(net_browser_host, 0u, sizeof(net_browser_host), host);
    net_append_capped(net_browser_path, 0u, sizeof(net_browser_path), path);
    net_append_capped(net_browser_current_url, 0u,
                      sizeof(net_browser_current_url), full);
    if (add_history) {
        net_browser_history_record(net_browser_current_url);
    }
    net_browser_header_only_retry_count = 0u;
    net_browser_text_reset();
    if (restore_scroll) {
        net_browser_scroll = saved_scroll;
        if (net_browser_history_count != 0u &&
            net_browser_history_index < NET_BROWSER_HISTORY_MAX) {
            net_browser_scroll = net_browser_history_scroll[net_browser_history_index];
        }
    }
    net_browser_fetch_enabled = 1u;
    net_browser_reset_primary_fetch();
    net_browser_status[0] = 'L';
    net_browser_status[1] = 'D';
    net_browser_status[2] = 'G';
    net_browser_status[3] = 0;
    net_browser_set_state("LOADING");
    net_set_last("BROWSER URL OPEN");
    serial_print("LeonOS net browser URL open ");
    serial_print(net_browser_current_url);
    serial_print("\r\n");
    dirty = 1;
}

static void net_browser_open_url(const char *url)
{
    net_browser_open_url_internal(url, 1u, 0u);
}

static void net_browser_retry_current_url_after_https_stall(const char *reason)
{
    u8 use_saved_ip = 0u;
    u8 saved_count = net_dns_a_count;
    u8 saved_index = net_dns_a_index;
    u8 saved_records[NET_DNS_A_MAX][4];
    if (saved_count > 1u) {
        mem_copy((u8 *) saved_records, (const u8 *) net_dns_a_records,
                 sizeof(saved_records));
        saved_index += 1u;
        if (saved_index >= saved_count) {
            saved_index = 0u;
        }
        use_saved_ip = 1u;
    }
    net_tls_abort_primary_for_retry();
    net_browser_header_only_retry_count += 1u;
    if (net_user_fetch_active && net_user_fetch_len == 0u) {
        net_user_fetch_clear_response_for_retry();
    }
    serial_print("LeonOS net browser ");
    serial_print(reason);
    serial_print(" retry ");
    serial_print_dec(net_browser_header_only_retry_count);
    serial_write(' ');
    serial_print(net_browser_current_url);
    serial_print("\r\n");
    net_browser_text_reset();
    net_browser_fetch_enabled = 1u;
    net_browser_reset_primary_fetch();
    if (use_saved_ip) {
        mem_copy((u8 *) net_dns_a_records, (const u8 *) saved_records,
                 sizeof(net_dns_a_records));
        net_dns_a_count = saved_count;
        net_dns_a_index = saved_index;
        mem_copy(net_dns_a_record, net_dns_a_records[net_dns_a_index], 4u);
        net_dns_reply_seen = 1u;
        net_dns_query_sent = 1u;
        serial_print("LeonOS net DNS A retry ");
        serial_print(net_browser_host);
        serial_print(" ");
        serial_print_ipv4(net_dns_a_record);
        serial_print("\r\n");
    }
    net_set_last("BROWSER HEADER RETRY");
    serial_print("LeonOS net browser URL open ");
    serial_print(net_browser_current_url);
    serial_print("\r\n");
    dirty = 1;
}

static void net_browser_reload_current_url(void)
{
    if (net_browser_history_reload_count < 65535u) {
        net_browser_history_reload_count += 1u;
    }
    serial_print(msg_net_browser_history);
    serial_print("reload ");
    serial_print(net_browser_current_url);
    serial_print("\r\n");
    if (net_browser_open_local_history_url(net_browser_current_url)) {
        return;
    }
    net_browser_open_url_internal(net_browser_current_url, 0u, 1u);
}

static void net_browser_open_default_url(void)
{
    net_browser_open_url(net_browser_default_url);
}

static u32 net_cstr_len(const char *text, u32 max_len)
{
    u32 len = 0u;
    while (len < max_len && text[len] != 0) {
        len += 1u;
    }
    return len;
}

static void net_browser_form_open_current(void)
{
    if (net_browser_form_model_count >= NET_BROWSER_FORM_MAX) {
        net_browser_active_form = 0xFFu;
        return;
    }
    u8 slot = (u8) net_browser_form_model_count++;
    if (net_browser_current_form_action[0] != 0) {
        if (!net_browser_normalize_resource(net_browser_current_form_action,
                                            net_browser_form_action_url[slot],
                                            NET_BROWSER_RESOURCE_URL_MAX)) {
            net_append_capped(net_browser_form_action_url[slot], 0u,
                              NET_BROWSER_RESOURCE_URL_MAX,
                              net_browser_current_url);
        }
    } else {
        net_append_capped(net_browser_form_action_url[slot], 0u,
                          NET_BROWSER_RESOURCE_URL_MAX,
                          net_browser_current_url);
    }
    net_browser_form_get[slot] =
        net_browser_current_form_method[0] == 0 ||
        net_text_is(net_browser_current_form_method, "get");
    net_browser_active_form = slot;
}

static void net_browser_form_close_current(void)
{
    net_browser_active_form = 0xFFu;
}

static u8 net_browser_control_kind_from_current(void)
{
    if (net_text_is(net_browser_control_type, "hidden")) {
        return NET_BROWSER_CONTROL_KIND_HIDDEN;
    }
    if (net_text_is(net_browser_control_type, "submit") ||
        net_text_is(net_browser_control_type, "image")) {
        return NET_BROWSER_CONTROL_KIND_SUBMIT;
    }
    if (net_text_is(net_browser_control_type, "button") ||
        net_text_is(net_browser_control_type, "reset")) {
        return NET_BROWSER_CONTROL_KIND_BUTTON;
    }
    if (net_tag_is("button")) {
        if (net_text_is(net_browser_control_type, "button") ||
            net_text_is(net_browser_control_type, "reset")) {
            return NET_BROWSER_CONTROL_KIND_BUTTON;
        }
        return NET_BROWSER_CONTROL_KIND_SUBMIT;
    }
    if (net_tag_is("input")) {
        if (net_browser_control_type[0] == 0 ||
            net_text_is(net_browser_control_type, "text") ||
            net_text_is(net_browser_control_type, "search") ||
            net_text_is(net_browser_control_type, "url") ||
            net_text_is(net_browser_control_type, "email") ||
            net_text_is(net_browser_control_type, "password")) {
            return NET_BROWSER_CONTROL_KIND_TEXT;
        }
        return NET_BROWSER_CONTROL_KIND_OTHER;
    }
    if (net_tag_is("textarea")) {
        return NET_BROWSER_CONTROL_KIND_TEXT;
    }
    if (net_tag_is("select")) {
        return NET_BROWSER_CONTROL_KIND_OTHER;
    }
    return NET_BROWSER_CONTROL_KIND_OTHER;
}

static u8 net_browser_register_current_control(u8 kind)
{
    if (net_browser_control_model_count >= NET_BROWSER_CONTROL_MAX) {
        return 0xFFu;
    }
    u8 slot = (u8) net_browser_control_model_count++;
    net_browser_control_form[slot] = net_browser_active_form;
    net_browser_control_kind[slot] = kind;
    net_browser_control_focusable[slot] =
        kind != NET_BROWSER_CONTROL_KIND_HIDDEN &&
        kind != NET_BROWSER_CONTROL_KIND_OTHER;
    net_browser_control_editable[slot] =
        kind == NET_BROWSER_CONTROL_KIND_TEXT;
    net_append_capped(net_browser_control_name[slot], 0u,
                      NET_BROWSER_CONTROL_NAME_MAX,
                      net_browser_current_control_name);
    net_append_capped(net_browser_control_value_store[slot], 0u,
                      NET_BROWSER_CONTROL_VALUE_MAX,
                      net_browser_current_control_actual);
    if (net_browser_control_value[0] != 0) {
        net_append_capped(net_browser_control_label[slot], 0u,
                          NET_BROWSER_CONTROL_VALUE_MAX,
                          net_browser_control_value);
    } else if (net_browser_current_control_name[0] != 0) {
        net_append_capped(net_browser_control_label[slot], 0u,
                          NET_BROWSER_CONTROL_VALUE_MAX,
                          net_browser_current_control_name);
    } else if (kind == NET_BROWSER_CONTROL_KIND_SUBMIT ||
               kind == NET_BROWSER_CONTROL_KIND_BUTTON) {
        net_append_capped(net_browser_control_label[slot], 0u,
                          NET_BROWSER_CONTROL_VALUE_MAX,
                          "Submit");
    } else {
        net_append_capped(net_browser_control_label[slot], 0u,
                          NET_BROWSER_CONTROL_VALUE_MAX,
                          "field");
    }
    if ((kind == NET_BROWSER_CONTROL_KIND_SUBMIT ||
         kind == NET_BROWSER_CONTROL_KIND_BUTTON) &&
        net_browser_control_value_store[slot][0] == 0) {
        net_append_capped(net_browser_control_value_store[slot], 0u,
                          NET_BROWSER_CONTROL_VALUE_MAX,
                          net_browser_control_label[slot]);
    }
    if (net_browser_control_focusable[slot] &&
        net_browser_control_focusable_count < 65535u) {
        net_browser_control_focusable_count += 1u;
    }
    if (net_browser_control_editable[slot] &&
        net_browser_control_editable_count < 65535u) {
        net_browser_control_editable_count += 1u;
    }
    if (kind == NET_BROWSER_CONTROL_KIND_HIDDEN &&
        net_browser_control_hidden_count < 65535u) {
        net_browser_control_hidden_count += 1u;
    }
    if ((kind == NET_BROWSER_CONTROL_KIND_SUBMIT ||
         kind == NET_BROWSER_CONTROL_KIND_BUTTON) &&
        net_browser_control_submit_count < 65535u) {
        net_browser_control_submit_count += 1u;
    }
    return slot;
}

static void net_browser_control_write_render_text(u8 slot, u8 control)
{
    if (slot >= NET_BROWSER_RENDER_MAX || control >= NET_BROWSER_CONTROL_MAX) {
        return;
    }
    char *out = net_browser_render_text[slot];
    u32 pos = 0u;
    if (net_browser_control_kind[control] == NET_BROWSER_CONTROL_KIND_SUBMIT ||
        net_browser_control_kind[control] == NET_BROWSER_CONTROL_KIND_BUTTON) {
        pos = net_append_capped(out, pos, NET_BROWSER_RENDER_TEXT_MAX, "BUTTON ");
        pos = net_append_capped(out, pos, NET_BROWSER_RENDER_TEXT_MAX,
                                net_browser_control_label[control]);
        return;
    }
    pos = net_append_capped(out, pos, NET_BROWSER_RENDER_TEXT_MAX, "INPUT ");
    pos = net_append_capped(out, pos, NET_BROWSER_RENDER_TEXT_MAX,
                            net_browser_control_label[control]);
    pos = net_append_capped(out, pos, NET_BROWSER_RENDER_TEXT_MAX, ": ");
    const char *value = net_browser_control_value_store[control];
    u32 len = net_cstr_len(value, NET_BROWSER_CONTROL_VALUE_MAX - 1u);
    for (u32 i = 0u; i <= len && pos + 1u < NET_BROWSER_RENDER_TEXT_MAX; i += 1u) {
        if (net_browser_focused_control == control &&
            i == net_browser_form_caret) {
            out[pos++] = '|';
        }
        if (i < len) {
            out[pos++] = value[i];
        }
    }
    out[pos] = 0;
}

static void net_browser_update_control_render(u8 control)
{
    for (u32 i = 0u; i < net_browser_render_count; i += 1u) {
        if (net_browser_render_control[i] == control) {
            net_browser_control_write_render_text((u8) i, control);
        }
    }
    dirty = 1;
}

static void net_browser_focus_control(u8 control)
{
    if (control >= net_browser_control_model_count ||
        !net_browser_control_focusable[control]) {
        return;
    }
    u8 old = net_browser_focused_control;
    net_browser_focused_control = control;
    net_browser_form_caret = (u8) net_cstr_len(
        net_browser_control_value_store[control],
        NET_BROWSER_CONTROL_VALUE_MAX - 1u);
    if (net_browser_form_focus_count < 65535u) {
        net_browser_form_focus_count += 1u;
    }
    if (old != 0xFFu) {
        net_browser_update_control_render(old);
    }
    net_browser_update_control_render(control);
    serial_print("LeonOS net browser form focus ");
    serial_print_dec(control);
    serial_print(" name ");
    serial_print(net_browser_control_name[control]);
    serial_print(" value ");
    serial_print(net_browser_control_value_store[control]);
    serial_print("\r\n");
}

static u8 net_browser_form_focus_next(void)
{
    if (net_browser_control_model_count == 0u) {
        return 0u;
    }
    if (net_browser_focused_control == 0xFFu) {
        for (u32 i = 0u; i < net_browser_control_model_count; i += 1u) {
            if (net_browser_control_editable[i] &&
                net_text_is(net_browser_control_name[i], "q")) {
                net_browser_focus_control((u8) i);
                return 1u;
            }
        }
    }
    u8 start = net_browser_focused_control == 0xFFu
        ? 0u
        : (u8) (net_browser_focused_control + 1u);
    for (u32 pass = 0u; pass < 2u; pass += 1u) {
        for (u32 i = start; i < net_browser_control_model_count; i += 1u) {
            if (net_browser_control_focusable[i] &&
                (pass != 0u || net_browser_control_editable[i])) {
                net_browser_focus_control((u8) i);
                return 1u;
            }
        }
        start = 0u;
    }
    return 0u;
}

static void net_browser_append_hex_byte(char *dst, u32 *pos, u32 dst_len, u8 ch)
{
    if (*pos + 3u >= dst_len) {
        return;
    }
    dst[(*pos)++] = '%';
    dst[(*pos)++] = hex_digit((u8) (ch >> 4));
    dst[(*pos)++] = hex_digit(ch);
    dst[*pos] = 0;
}

static void net_browser_append_query_encoded(char *dst, u32 *pos,
                                             u32 dst_len, const char *src)
{
    for (u32 i = 0u; src[i] != 0 && *pos + 1u < dst_len; i += 1u) {
        u8 ch = (u8) src[i];
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            dst[(*pos)++] = (char) ch;
            dst[*pos] = 0;
        } else if (ch == ' ') {
            dst[(*pos)++] = '+';
            dst[*pos] = 0;
        } else {
            net_browser_append_hex_byte(dst, pos, dst_len, ch);
        }
    }
}

static u8 net_browser_url_has_query(const char *url)
{
    for (u32 i = 0u; url[i] != 0; i += 1u) {
        if (url[i] == '?') {
            return 1u;
        }
    }
    return 0u;
}

static u8 net_browser_form_append_raw_pair(char *url, u32 *pos, u32 url_len,
                                           u8 *first, const char *name,
                                           const char *value)
{
    if (name[0] == 0) {
        return 0u;
    }
    if (*first) {
        if (*pos + 1u >= url_len) {
            return 0u;
        }
        url[(*pos)++] = net_browser_url_has_query(url) ? '&' : '?';
        url[*pos] = 0;
        *first = 0u;
    } else {
        if (*pos + 1u >= url_len) {
            return 0u;
        }
        url[(*pos)++] = '&';
        url[*pos] = 0;
    }
    net_browser_append_query_encoded(url, pos, url_len, name);
    if (*pos + 1u >= url_len) {
        return 1u;
    }
    url[(*pos)++] = '=';
    url[*pos] = 0;
    net_browser_append_query_encoded(url, pos, url_len, value);
    return 1u;
}

static u8 net_browser_form_append_pair(char *url, u32 *pos, u32 url_len,
                                       u8 *first, u8 control)
{
    if (control >= net_browser_control_model_count) {
        return 0u;
    }
    return net_browser_form_append_raw_pair(
        url, pos, url_len, first,
        net_browser_control_name[control],
        net_browser_control_value_store[control]);
}

static u8 net_browser_form_is_google_search(u8 form)
{
    return form != 0xFFu &&
           form < NET_BROWSER_FORM_MAX &&
           net_text_is(net_browser_host, "www.google.com") &&
           net_text_contains_ci(net_browser_form_action_url[form], "/search");
}

static u8 net_browser_form_should_submit_hidden(u8 form, u8 control)
{
    if (control >= net_browser_control_model_count ||
        net_browser_control_kind[control] != NET_BROWSER_CONTROL_KIND_HIDDEN) {
        return 1u;
    }
    if (net_browser_form_is_google_search(form)) {
        return net_text_is(net_browser_control_name[control], "igu") ||
               net_text_is(net_browser_control_name[control], "hl") ||
               net_text_is(net_browser_control_name[control], "gbv");
    }
    return 1u;
}

static u8 net_browser_form_submit_control(u8 control)
{
    if (control >= net_browser_control_model_count) {
        return 0u;
    }
    u8 form = net_browser_control_form[control];
    if (form >= net_browser_form_model_count || form >= NET_BROWSER_FORM_MAX) {
        form = 0xFFu;
    }
    if (form != 0xFFu && !net_browser_form_get[form]) {
        serial_print("LeonOS net browser form submit unsupported method\r\n");
        return 1u;
    }
    char url[NET_BROWSER_RESOURCE_URL_MAX];
    if (form != 0xFFu && net_browser_form_action_url[form][0] != 0) {
        net_append_capped(url, 0u, sizeof(url), net_browser_form_action_url[form]);
    } else {
        net_append_capped(url, 0u, sizeof(url), net_browser_current_url);
    }
    u32 pos = net_cstr_len(url, sizeof(url) - 1u);
    u8 first = 1u;
    if (net_browser_control_editable[control]) {
        net_browser_form_append_pair(url, &pos, sizeof(url), &first, control);
    }
    for (u32 i = 0u; i < net_browser_control_model_count; i += 1u) {
        if (i == control || net_browser_control_form[i] != form) {
            continue;
        }
        if (net_browser_control_kind[i] == NET_BROWSER_CONTROL_KIND_HIDDEN ||
            net_browser_control_editable[i]) {
            if (!net_browser_form_should_submit_hidden(form, (u8) i)) {
                continue;
            }
            net_browser_form_append_pair(url, &pos, sizeof(url), &first, (u8) i);
        }
    }
    if (net_browser_form_is_google_search(form)) {
        net_browser_form_append_raw_pair(url, &pos, sizeof(url),
                                         &first, "gbv", "1");
    }
    if ((net_browser_control_kind[control] == NET_BROWSER_CONTROL_KIND_SUBMIT ||
         net_browser_control_kind[control] == NET_BROWSER_CONTROL_KIND_BUTTON) &&
        net_browser_control_name[control][0] != 0) {
        net_browser_form_append_pair(url, &pos, sizeof(url), &first, control);
    }
    if (net_browser_form_submit_event_count < 65535u) {
        net_browser_form_submit_event_count += 1u;
    }
    net_append_capped(net_browser_last_form_submit_url, 0u,
                      sizeof(net_browser_last_form_submit_url), url);
    serial_print("LeonOS net browser form submit ");
    serial_print_dec(control);
    serial_print(" url ");
    serial_print(url);
    serial_print("\r\n");
    net_browser_open_url(url);
    return 1u;
}

static u8 net_browser_form_insert_char(char ch)
{
    u8 control = net_browser_focused_control;
    if (control >= net_browser_control_model_count ||
        !net_browser_control_editable[control] ||
        (u8) ch < 32u || (u8) ch > 126u) {
        return 0u;
    }
    char *value = net_browser_control_value_store[control];
    u32 len = net_cstr_len(value, NET_BROWSER_CONTROL_VALUE_MAX - 1u);
    if (len >= NET_BROWSER_CONTROL_VALUE_MAX - 1u) {
        return 1u;
    }
    if (net_browser_form_caret > len) {
        net_browser_form_caret = (u8) len;
    }
    for (u32 i = len + 1u; i > net_browser_form_caret; i -= 1u) {
        value[i] = value[i - 1u];
    }
    value[net_browser_form_caret] = ch;
    net_browser_form_caret += 1u;
    if (net_browser_form_edit_count < 65535u) {
        net_browser_form_edit_count += 1u;
    }
    net_browser_update_control_render(control);
    serial_print("LeonOS net browser form edit ");
    serial_print_dec(control);
    serial_print(" value ");
    serial_print(value);
    serial_print("\r\n");
    return 1u;
}

static u8 net_browser_form_backspace(void)
{
    u8 control = net_browser_focused_control;
    if (control >= net_browser_control_model_count ||
        !net_browser_control_editable[control] ||
        net_browser_form_caret == 0u) {
        return 1u;
    }
    char *value = net_browser_control_value_store[control];
    u32 len = net_cstr_len(value, NET_BROWSER_CONTROL_VALUE_MAX - 1u);
    if (net_browser_form_caret > len) {
        net_browser_form_caret = (u8) len;
    }
    for (u32 i = net_browser_form_caret - 1u; i < len; i += 1u) {
        value[i] = value[i + 1u];
    }
    net_browser_form_caret -= 1u;
    if (net_browser_form_edit_count < 65535u) {
        net_browser_form_edit_count += 1u;
    }
    net_browser_update_control_render(control);
    serial_print("LeonOS net browser form edit ");
    serial_print_dec(control);
    serial_print(" value ");
    serial_print(value);
    serial_print("\r\n");
    return 1u;
}

static u8 net_browser_form_key(u8 scancode, char ch)
{
    if (net_browser_focused_control == 0xFFu) {
        return 0u;
    }
    if (scancode == 0x01u) {
        u8 old = net_browser_focused_control;
        net_browser_focused_control = 0xFFu;
        net_browser_form_caret = 0u;
        net_browser_update_control_render(old);
        return 1u;
    }
    if (scancode == 0x1Cu) {
        return net_browser_form_submit_control(net_browser_focused_control);
    }
    if (scancode == 0x0Eu) {
        return net_browser_form_backspace();
    }
    if (scancode == 0x4Bu) {
        if (net_browser_form_caret != 0u) {
            net_browser_form_caret -= 1u;
            net_browser_update_control_render(net_browser_focused_control);
        }
        return 1u;
    }
    if (scancode == 0x4Du) {
        u8 control = net_browser_focused_control;
        u32 len = net_cstr_len(net_browser_control_value_store[control],
                               NET_BROWSER_CONTROL_VALUE_MAX - 1u);
        if (net_browser_form_caret < len) {
            net_browser_form_caret += 1u;
            net_browser_update_control_render(control);
        }
        return 1u;
    }
    if (ch != 0) {
        return net_browser_form_insert_char(ch);
    }
    return 1u;
}

static void net_browser_store_current_link_target(void)
{
    if (net_browser_current_link_index != 0xFFu ||
        net_browser_link_target_count >= NET_BROWSER_LINK_MAX) {
        return;
    }
    char normalized[NET_BROWSER_RESOURCE_URL_MAX];
    if (!net_browser_normalize_resource(net_browser_html_attr_value,
                                        normalized, sizeof(normalized))) {
        return;
    }
    u16 slot = net_browser_link_target_count++;
    net_browser_current_link_index = (u8) slot;
    net_append_capped(net_browser_link_url[slot], 0u,
                      NET_BROWSER_RESOURCE_URL_MAX, normalized);
}

static void net_browser_queue_resource(char type)
{
    char normalized[NET_BROWSER_RESOURCE_URL_MAX];
    if (!net_browser_normalize_resource(net_browser_html_attr_value,
                                        normalized, sizeof(normalized))) {
        return;
    }
    net_browser_resource_total += 1u;
    if (net_browser_resource_count >= NET_BROWSER_RESOURCE_MAX) {
        return;
    }
    u16 slot = net_browser_resource_count++;
    net_browser_resource_type[slot] = type;
    net_append_capped(net_browser_resource_url[slot], 0u,
                      NET_BROWSER_RESOURCE_URL_MAX, normalized);
    net_browser_resource_image[slot] = type == 'I'
        ? net_browser_image_find_by_url(normalized)
        : 0xFFu;
}

static void net_browser_queue_resource_value(char type, const char *value)
{
    char normalized[NET_BROWSER_RESOURCE_URL_MAX];
    if (!net_browser_normalize_resource(value, normalized, sizeof(normalized))) {
        return;
    }
    net_browser_resource_total += 1u;
    if (net_browser_resource_count >= NET_BROWSER_RESOURCE_MAX) {
        return;
    }
    u16 slot = net_browser_resource_count++;
    net_browser_resource_type[slot] = type;
    net_append_capped(net_browser_resource_url[slot], 0u,
                      NET_BROWSER_RESOURCE_URL_MAX, normalized);
    net_browser_resource_image[slot] = type == 'I'
        ? net_browser_image_find_by_url(normalized)
        : 0xFFu;
}

static void net_browser_queue_resource_first_url(char type)
{
    char first[96];
    u32 out = 0u;
    u32 i = 0u;
    while (net_browser_html_attr_value[i] == ' ' ||
           net_browser_html_attr_value[i] == '\t') {
        i += 1u;
    }
    for (; net_browser_html_attr_value[i] != 0 &&
           net_browser_html_attr_value[i] != ',' &&
           net_browser_html_attr_value[i] != ' ' &&
           net_browser_html_attr_value[i] != '\t' &&
           out + 1u < sizeof(first); i += 1u) {
        first[out++] = net_browser_html_attr_value[i];
    }
    first[out] = 0;
    if (out != 0u) {
        net_browser_queue_resource_value(type, first);
    }
}

static void net_browser_render_emit_image(void)
{
    if (!net_tag_is("img")) {
        return;
    }
    net_browser_note_placeholder();
    if (net_browser_current_img_src[0] != 0) {
        char normalized[NET_BROWSER_RESOURCE_URL_MAX];
        if (net_browser_normalize_resource(net_browser_current_img_src,
                                           normalized, sizeof(normalized))) {
            u8 image = net_browser_image_add_pre_normalized(
                normalized, net_browser_current_img_alt);
            net_browser_image_bind_resource_url(image, normalized);
            net_browser_render_add_image_block(image);
            return;
        }
    }
    if (net_browser_current_img_alt[0] != 0) {
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_IMAGE,
                                           NET_BROWSER_RENDER_FLAG_BOX,
                                           "IMAGE unsupported: ",
                                           net_browser_current_img_alt);
    } else {
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_IMAGE,
                                           NET_BROWSER_RENDER_FLAG_BOX,
                                           "IMAGE unsupported: ", "missing src");
    }
}

static void net_browser_render_emit_meta(void)
{
    if (!net_tag_is("meta")) {
        return;
    }
    if (net_browser_control_type_len == 0u &&
        net_browser_control_value_len == 0u) {
        return;
    }
    char line[96];
    u32 pos = 0u;
    if (net_browser_control_type_len != 0u) {
        pos = net_append_capped(line, pos, sizeof(line),
                                net_browser_control_type);
        if (net_browser_control_value_len != 0u) {
            pos = net_append_capped(line, pos, sizeof(line), ": ");
        }
    }
    if (net_browser_control_value_len != 0u) {
        net_append_capped(line, pos, sizeof(line), net_browser_control_value);
    }
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_META,
                                       0u, "META ", line);
}

static void net_browser_render_emit_named_control(const char *prefix,
                                                  const char *fallback)
{
    u8 kind = net_browser_control_kind_from_current();
    u8 control = net_browser_register_current_control(kind);
    u8 flags = NET_BROWSER_RENDER_FLAG_INPUT | NET_BROWSER_RENDER_FLAG_BOX;
    if (kind == NET_BROWSER_CONTROL_KIND_SUBMIT ||
        kind == NET_BROWSER_CONTROL_KIND_BUTTON) {
        flags = NET_BROWSER_RENDER_FLAG_BUTTON |
                NET_BROWSER_RENDER_FLAG_BOX |
                NET_BROWSER_RENDER_FLAG_CENTER;
    }
    if (net_browser_control_value_len == 0u) {
        net_browser_control_value_set_from("", fallback);
    }
    if (control < NET_BROWSER_CONTROL_MAX) {
        u8 slot = net_browser_render_add_control_block(NET_BROWSER_RENDER_KIND_TEXT,
                                                       flags,
                                                       control);
        if (slot != 0xFFu) {
            net_browser_css_apply_tag_to_slot(slot);
            net_browser_control_write_render_text(slot, control);
        }
    } else {
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_TEXT,
                                           flags, prefix,
                                           net_browser_control_value);
    }
}

static void net_browser_render_emit_embed(const char *prefix,
                                          const char *fallback)
{
    net_browser_note_placeholder();
    const char *value = fallback;
    if (net_browser_control_value_len != 0u) {
        value = net_browser_control_value;
    }
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_EMBED,
                                       NET_BROWSER_RENDER_FLAG_BOX,
                                       prefix, value);
}

static void net_browser_render_emit_dialog(void)
{
    net_browser_note_placeholder();
    const char *value = "dialog";
    if (net_browser_control_value_len != 0u) {
        value = net_browser_control_value;
    }
    net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_DIALOG,
                                       NET_BROWSER_RENDER_FLAG_BOX,
                                       "DIALOG ", value);
}

static void net_browser_attr_commit(void)
{
    if (net_browser_html_attr_name_len == 0u) {
        net_browser_attr_reset_current();
        return;
    }
    net_browser_html_attr_name[net_browser_html_attr_name_len] = 0;
    net_browser_html_attr_value[net_browser_html_attr_value_len] = 0;

    if (net_browser_html_tag_len == 0u || net_browser_html_tag[0] == '/') {
        net_browser_attr_reset_current();
        return;
    }

    net_browser_attr_count += 1u;
    if (net_attr_starts_with("aria-")) {
        net_browser_aria_attr_count += 1u;
    }
    if (net_attr_starts_with("data-")) {
        net_browser_data_attr_count += 1u;
    }
    if (net_attr_starts_with("on")) {
        net_browser_event_attr_count += 1u;
        net_browser_js_attr_count += 1u;
    } else if (net_attr_starts_with("js")) {
        net_browser_js_attr_count += 1u;
    }
    if (net_text_starts_with_ci(net_browser_html_attr_value, "javascript:")) {
        net_browser_js_url_count += 1u;
    }
    if (net_text_is(net_browser_html_attr_name, "href")) {
        net_browser_href_count += 1u;
        if (net_browser_first_href[0] == 0) {
            net_copy_attr_value(net_browser_first_href, sizeof(net_browser_first_href));
        }
        if (net_tag_is("a")) {
            net_browser_store_current_link_target();
        }
        net_browser_queue_resource('H');
    } else if (net_text_is(net_browser_html_attr_name, "src")) {
        net_browser_src_count += 1u;
        if (net_browser_first_src[0] == 0) {
            net_copy_attr_value(net_browser_first_src, sizeof(net_browser_first_src));
        }
        net_browser_queue_resource(net_tag_is("script") ? 'J' :
                                   (net_tag_is("img") || net_tag_is("image")) ? 'I' : 'S');
        if (net_tag_is("img")) {
            net_copy_attr_value(net_browser_current_img_src,
                                sizeof(net_browser_current_img_src));
        }
    } else if (net_text_is(net_browser_html_attr_name, "srcset") ||
               net_text_is(net_browser_html_attr_name, "imagesrcset")) {
        net_browser_srcset_count += 1u;
        if (net_browser_first_src[0] == 0) {
            net_browser_queue_resource_first_url('I');
        } else {
            net_browser_queue_resource_first_url('I');
        }
        if (net_tag_is("img") && net_browser_current_img_src[0] == 0) {
            char first[96];
            u32 out = 0u;
            for (u32 i = 0u; net_browser_html_attr_value[i] != 0 &&
                 net_browser_html_attr_value[i] != ',' &&
                 net_browser_html_attr_value[i] != ' ' &&
                 out + 1u < sizeof(first); i += 1u) {
                first[out++] = net_browser_html_attr_value[i];
            }
            first[out] = 0;
            net_append_capped(net_browser_current_img_src, 0u,
                              sizeof(net_browser_current_img_src), first);
        }
    } else if (net_text_is(net_browser_html_attr_name, "poster") ||
               (net_tag_is("object") &&
                net_text_is(net_browser_html_attr_name, "data"))) {
        net_browser_queue_resource('I');
    } else if (net_text_is(net_browser_html_attr_name, "action")) {
        net_browser_action_count += 1u;
        if (net_browser_first_action[0] == 0) {
            net_copy_attr_value(net_browser_first_action, sizeof(net_browser_first_action));
        }
        if (net_tag_is("form")) {
            net_copy_attr_value(net_browser_current_form_action,
                                sizeof(net_browser_current_form_action));
        }
        net_browser_queue_resource('A');
    } else if (net_tag_is("form") &&
               net_text_is(net_browser_html_attr_name, "method")) {
        net_copy_attr_value_lower(net_browser_current_form_method,
                                  sizeof(net_browser_current_form_method));
    } else if (net_text_is(net_browser_html_attr_name, "rel")) {
        if (net_browser_first_rel[0] == 0) {
            net_copy_attr_value(net_browser_first_rel, sizeof(net_browser_first_rel));
        }
    } else if (net_text_is(net_browser_html_attr_name, "class")) {
        net_copy_attr_value(net_browser_current_class,
                            sizeof(net_browser_current_class));
    } else if (net_text_is(net_browser_html_attr_name, "id")) {
        net_copy_attr_value(net_browser_current_id,
                            sizeof(net_browser_current_id));
    } else if (net_tag_is("input") &&
               net_text_is(net_browser_html_attr_name, "type")) {
        net_browser_control_type_set();
        if (net_browser_first_input_type[0] == 0) {
            net_copy_attr_value(net_browser_first_input_type,
                                sizeof(net_browser_first_input_type));
        }
        if (net_text_is(net_browser_control_type, "hidden")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_HIDDEN;
        } else if (net_text_is(net_browser_control_type, "submit") ||
                   net_text_is(net_browser_control_type, "button") ||
                   net_text_is(net_browser_control_type, "reset") ||
                   net_text_is(net_browser_control_type, "image")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BUTTON |
                                            NET_BROWSER_RENDER_FLAG_BOX |
                                            NET_BROWSER_RENDER_FLAG_CENTER;
        }
    } else if (net_text_is(net_browser_html_attr_name, "role")) {
        net_browser_control_type_set();
        if (net_text_is(net_browser_control_type, "button") ||
            net_text_is(net_browser_control_type, "tab") ||
            net_text_is(net_browser_control_type, "menuitem")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BUTTON |
                                            NET_BROWSER_RENDER_FLAG_BOX |
                                            NET_BROWSER_RENDER_FLAG_CENTER;
        } else if (net_text_is(net_browser_control_type, "textbox") ||
                   net_text_is(net_browser_control_type, "searchbox")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_INPUT |
                                            NET_BROWSER_RENDER_FLAG_BOX;
        } else if (net_text_is(net_browser_control_type, "dialog")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BOX;
        } else if (net_text_is(net_browser_control_type, "img")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BOX;
        }
    } else if (net_text_is(net_browser_html_attr_name, "hidden")) {
        net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_HIDDEN;
    } else if (net_text_is(net_browser_html_attr_name, "align")) {
        if (net_text_contains_ci(net_browser_html_attr_value, "center")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_CENTER;
        }
    } else if (net_text_is(net_browser_html_attr_name, "style")) {
        net_browser_css_parse_inline_style(net_browser_html_attr_value);
    } else if (net_tag_is("meta") &&
               (net_text_is(net_browser_html_attr_name, "name") ||
                net_text_is(net_browser_html_attr_name, "property") ||
                net_text_is(net_browser_html_attr_name, "http-equiv") ||
                net_text_is(net_browser_html_attr_name, "charset"))) {
        net_browser_control_type_set();
        if (net_text_is(net_browser_html_attr_name, "charset")) {
            net_browser_control_value_set("");
        }
    } else if (net_tag_is("meta") &&
               net_text_is(net_browser_html_attr_name, "content")) {
        net_browser_control_value_set("");
    } else if ((net_tag_is("input") || net_tag_is("button") ||
                net_tag_is("textarea") || net_tag_is("select") ||
                net_tag_is("option")) &&
               net_text_is(net_browser_html_attr_name, "name")) {
        net_copy_attr_value(net_browser_current_control_name,
                            sizeof(net_browser_current_control_name));
        if (net_browser_control_value_len == 0u) {
            net_browser_control_value_set("");
        }
    } else if ((net_tag_is("input") || net_tag_is("button") ||
                net_tag_is("textarea") || net_tag_is("select") ||
                net_tag_is("option")) &&
               net_text_is(net_browser_html_attr_name, "value")) {
        net_copy_attr_value(net_browser_current_control_actual,
                            sizeof(net_browser_current_control_actual));
        if (net_tag_is("button") ||
            net_text_is(net_browser_control_type, "submit") ||
            net_text_is(net_browser_control_type, "button") ||
            net_text_is(net_browser_control_type, "reset") ||
            net_text_is(net_browser_control_type, "image")) {
            net_browser_control_value_set("");
        }
    } else if ((net_tag_is("input") || net_tag_is("button") ||
                net_tag_is("textarea") || net_tag_is("select") ||
                net_tag_is("option") || net_tag_has_dash() ||
                net_tag_is("svg") || net_tag_is_media_name()) &&
               (net_text_is(net_browser_html_attr_name, "placeholder") ||
                net_text_is(net_browser_html_attr_name, "aria-label") ||
                net_text_is(net_browser_html_attr_name, "title") ||
                net_text_is(net_browser_html_attr_name, "label"))) {
        net_browser_control_value_set("");
    } else if (net_tag_is("img") &&
               (net_text_is(net_browser_html_attr_name, "alt") ||
                net_text_is(net_browser_html_attr_name, "title") ||
                net_text_is(net_browser_html_attr_name, "aria-label"))) {
        net_copy_attr_value(net_browser_current_img_alt,
                            sizeof(net_browser_current_img_alt));
    }

    net_browser_attr_reset_current();
}

static void net_browser_attr_start(u8 lower)
{
    net_browser_attr_reset_current();
    if (!net_attr_name_char(lower)) {
        return;
    }
    net_browser_html_attr_name[net_browser_html_attr_name_len++] = (char) lower;
    net_browser_html_attr_state = 1u;
}

static void net_browser_attr_append_value(u8 ch)
{
    if (net_browser_html_attr_value_len >=
        sizeof(net_browser_html_attr_value) - 1u) {
        return;
    }
    if (ch == '\r' || ch == '\n' || ch == '\t') {
        ch = ' ';
    }
    if (ch >= 32u && ch <= 126u) {
        net_browser_html_attr_value[net_browser_html_attr_value_len++] =
            (char) ch;
    }
}

static void net_browser_attr_feed(u8 ch)
{
    u8 lower = net_ascii_lower(ch);
    if (net_browser_html_attr_state == 0u) {
        if (net_html_space(ch) || ch == '/') {
            return;
        }
        net_browser_attr_start(lower);
        return;
    }
    if (net_browser_html_attr_state == 1u) {
        if (net_attr_name_char(lower)) {
            if (net_browser_html_attr_name_len <
                sizeof(net_browser_html_attr_name) - 1u) {
                net_browser_html_attr_name[net_browser_html_attr_name_len++] =
                    (char) lower;
            }
            return;
        }
        if (ch == '=') {
            net_browser_html_attr_state = 3u;
            return;
        }
        if (net_html_space(ch)) {
            net_browser_html_attr_state = 2u;
            return;
        }
        net_browser_attr_commit();
        net_browser_attr_start(lower);
        return;
    }
    if (net_browser_html_attr_state == 2u) {
        if (net_html_space(ch)) {
            return;
        }
        if (ch == '=') {
            net_browser_html_attr_state = 3u;
            return;
        }
        net_browser_attr_commit();
        net_browser_attr_start(lower);
        return;
    }
    if (net_browser_html_attr_state == 3u) {
        if (net_html_space(ch)) {
            return;
        }
        if (ch == '"' || ch == '\'') {
            net_browser_html_attr_quote = ch;
            net_browser_html_attr_state = 4u;
            return;
        }
        net_browser_html_attr_state = 5u;
        net_browser_attr_append_value(ch);
        return;
    }
    if (net_browser_html_attr_state == 4u) {
        if (ch == net_browser_html_attr_quote) {
            net_browser_attr_commit();
        } else {
            net_browser_attr_append_value(ch);
        }
        return;
    }
    if (net_browser_html_attr_state == 5u) {
        if (net_html_space(ch)) {
            net_browser_attr_commit();
        } else {
            net_browser_attr_append_value(ch);
        }
    }
}

static void net_browser_count_tag(void)
{
    if (net_browser_html_tag_len == 0u || net_browser_html_tag[0] == '/') {
        return;
    }
    net_browser_tag_count += 1u;
    if (net_tag_is("a")) {
        net_browser_anchor_count += 1u;
    } else if (net_tag_is("link")) {
        net_browser_link_count += 1u;
    } else if (net_tag_is("script")) {
        net_browser_script_count += 1u;
    } else if (net_tag_is("style")) {
        net_browser_style_count += 1u;
    } else if (net_tag_is("img")) {
        net_browser_image_count += 1u;
    } else if (net_tag_is("form")) {
        net_browser_form_count += 1u;
    } else if (net_tag_is("input")) {
        net_browser_input_count += 1u;
    } else if (net_tag_is("button")) {
        net_browser_button_count += 1u;
    } else if (net_tag_is("select")) {
        net_browser_select_count += 1u;
    } else if (net_tag_is("textarea")) {
        net_browser_textarea_count += 1u;
    } else if (net_tag_is("table")) {
        net_browser_table_count += 1u;
    } else if (net_tag_is("td") || net_tag_is("th")) {
        net_browser_table_cell_count += 1u;
    } else if (net_tag_is("meta")) {
        net_browser_meta_count += 1u;
    } else if (net_tag_is_heading_open()) {
        net_browser_heading_count += 1u;
    } else if (net_tag_is("svg")) {
        net_browser_svg_count += 1u;
    } else if (net_tag_is_svg_shape()) {
        net_browser_svg_shape_count += 1u;
    } else if (net_tag_is("picture") || net_tag_is("source")) {
        net_browser_picture_count += 1u;
    } else if (net_tag_is_media_name()) {
        net_browser_media_count += 1u;
    } else if (net_tag_has_dash()) {
        net_browser_custom_count += 1u;
    } else if (net_tag_is("dialog")) {
        net_browser_dialog_count += 1u;
    } else if (net_tag_is("template")) {
        net_browser_template_count += 1u;
    } else if (net_tag_is("noscript")) {
        net_browser_noscript_count += 1u;
    }
}

static void net_browser_finish_tag(void)
{
    net_browser_html_tag[net_browser_html_tag_len] = 0;
    if (net_browser_html_ignore_tag) {
        net_browser_attr_reset_current();
        net_browser_html_ignore_tag = 0u;
        return;
    }
    net_browser_attr_commit();
    net_browser_count_tag();
    if (net_browser_html_tag[0] == '/') {
        if (net_tag_is("/form")) {
            net_browser_form_close_current();
        }
        net_browser_dom_close_current_tag();
        net_browser_render_pop_tag_flags();
    } else {
        if (net_tag_is("center")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_CENTER;
        } else if (net_tag_is("b") || net_tag_is("strong") ||
                   net_tag_is("em") || net_tag_is("th") ||
                   net_tag_is("dt") || net_tag_is("summary")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BOLD;
        } else if (net_tag_is("mark") || net_tag_is("fieldset") ||
                   net_tag_is("details")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BOX;
        } else if (net_tag_is("pre") || net_tag_is("code") ||
                   net_tag_is("kbd") || net_tag_is("samp") ||
                   net_tag_is("var")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_MONO;
            if (net_tag_is("pre")) {
                net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BOX;
            }
        } else if (net_tag_is("blockquote") || net_tag_is("dialog")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BOX;
        } else if (net_tag_is("button")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_BUTTON |
                                            NET_BROWSER_RENDER_FLAG_BOX |
                                            NET_BROWSER_RENDER_FLAG_CENTER;
        } else if (net_tag_is("input") || net_tag_is("textarea") ||
                   net_tag_is("select") || net_tag_is("output") ||
                   net_tag_is("meter") || net_tag_is("progress")) {
            net_browser_render_tag_flags |= NET_BROWSER_RENDER_FLAG_INPUT |
                                            NET_BROWSER_RENDER_FLAG_BOX;
        }
        net_browser_render_tag_flags |= net_browser_css_apply_styles_for_current_tag();
        if (net_tag_is("form")) {
            net_browser_form_open_current();
        }
        net_browser_dom_open_current_tag();
        if (net_tag_is("input")) {
            net_browser_render_break();
            net_browser_render_emit_input_control();
        } else if (net_tag_is("button")) {
            net_browser_render_break();
            net_browser_render_emit_named_control("BUTTON ", "Submit");
            net_browser_render_push_tag_flags();
        } else if (net_tag_is("img")) {
            net_browser_render_break();
            net_browser_render_emit_image();
        } else if (net_tag_is("meta")) {
            net_browser_render_emit_meta();
        } else if (net_tag_is("dialog")) {
            net_browser_render_break();
            if (net_browser_control_value_len != 0u) {
                net_browser_render_emit_dialog();
            }
            net_browser_render_push_tag_flags();
        } else if (net_tag_is("svg")) {
            net_browser_render_break();
            net_browser_render_emit_embed("SVG ", "vector image not rendered");
            net_browser_render_push_tag_flags();
        } else if (net_tag_is("picture")) {
            net_browser_render_break();
            net_browser_render_emit_embed("PICTURE ", "responsive image set");
            net_browser_render_push_tag_flags();
        } else if (net_tag_is_media_name()) {
            net_browser_render_break();
            if (net_tag_is("canvas")) {
                net_browser_render_emit_embed("CANVAS ", "scripted bitmap");
            } else if (net_tag_is("iframe")) {
                net_browser_render_emit_embed("IFRAME ", "embedded page");
            } else if (net_tag_is("audio")) {
                net_browser_render_emit_embed("AUDIO ", "media");
            } else if (net_tag_is("video")) {
                net_browser_render_emit_embed("VIDEO ", "media");
            } else if (net_tag_is("object") || net_tag_is("embed")) {
                net_browser_render_emit_embed("EMBED ", "object");
            }
            net_browser_render_push_tag_flags();
        } else if (net_tag_is("template")) {
            net_browser_render_break();
            net_browser_render_emit_embed("TEMPLATE ", "inert HTML fragment");
        } else if (net_tag_has_dash()) {
            net_browser_render_break();
            net_browser_render_emit_embed("CUSTOM ", net_browser_html_tag);
            net_browser_render_push_tag_flags();
        } else if (net_tag_is("select")) {
            net_browser_render_break();
            net_browser_render_emit_named_control("SELECT ", "select");
            net_browser_render_push_tag_flags();
        } else if (net_tag_is("textarea")) {
            net_browser_render_break();
            if (net_browser_control_value_len != 0u) {
                net_browser_render_emit_named_control("TEXTAREA ", "textarea");
            }
            net_browser_render_push_tag_flags();
        } else {
            net_browser_render_push_tag_flags();
        }
    }
    if (net_tag_is_open("title")) {
        net_browser_render_break();
        net_browser_render_in_title = 1u;
    } else if (net_tag_is("/title")) {
        net_browser_render_break();
        net_browser_render_in_title = 0u;
    } else if (net_tag_is_open("a")) {
        net_browser_render_break();
        net_browser_render_in_anchor = 1u;
    } else if (net_tag_is("/a")) {
        net_browser_render_break();
        net_browser_render_in_anchor = 0u;
        net_browser_current_link_index = 0xFFu;
    } else if (net_tag_is_list_container() && net_browser_html_tag[0] != '/') {
        if (net_browser_render_list_depth < 255u) {
            net_browser_render_list_depth += 1u;
        }
        net_browser_render_break();
    } else if (net_tag_is_list_container() && net_browser_html_tag[0] == '/') {
        if (net_browser_render_list_depth != 0u) {
            net_browser_render_list_depth -= 1u;
        }
        net_browser_render_break();
    } else if (net_tag_is_open("li")) {
        net_browser_render_break();
        net_browser_render_in_list = 1u;
    } else if (net_tag_is("/li")) {
        net_browser_render_break();
        net_browser_render_in_list = 0u;
    } else if (net_tag_is_heading_open()) {
        net_browser_render_break();
        net_browser_render_heading = 1u;
    } else if (net_tag_is_heading_close()) {
        net_browser_render_break();
        net_browser_render_heading = 0u;
    } else if (net_tag_is_open("blockquote") || net_tag_is_open("q")) {
        net_browser_render_break();
        net_browser_render_in_quote = 1u;
    } else if (net_tag_is("/blockquote") || net_tag_is("/q")) {
        net_browser_render_break();
        net_browser_render_in_quote = 0u;
    } else if (net_tag_is_open("pre")) {
        net_browser_render_break();
        net_browser_render_in_pre = 1u;
    } else if (net_tag_is("/pre")) {
        net_browser_render_break();
        net_browser_render_in_pre = 0u;
    } else if (net_tag_is_open("code") || net_tag_is_open("kbd") ||
               net_tag_is_open("samp") || net_tag_is_open("var")) {
        net_browser_render_in_code = 1u;
    } else if (net_tag_is("/code") || net_tag_is("/kbd") ||
               net_tag_is("/samp") || net_tag_is("/var")) {
        net_browser_render_in_code = 0u;
    } else if (net_tag_is_open("table")) {
        net_browser_render_break();
        net_browser_render_table_row_cell = 0u;
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_TABLE,
                                           NET_BROWSER_RENDER_FLAG_BOX,
                                           "TABLE", "");
    } else if (net_tag_is("/table")) {
        net_browser_render_break();
        net_browser_render_table_row_cell = 0u;
    } else if (net_tag_is_open("tr")) {
        net_browser_render_break();
        net_browser_render_table_row_cell = 0u;
    } else if (net_tag_is("/tr")) {
        net_browser_render_break();
        net_browser_render_table_row_cell = 0u;
    } else if (net_tag_is_table_cell() && net_browser_html_tag[0] != '/') {
        net_browser_render_in_table_cell = 1u;
        net_browser_render_table_row_cell += 1u;
        if (net_browser_render_table_row_cell > 1u) {
            net_browser_append_char('|');
            net_browser_append_char(' ');
        }
    } else if (net_tag_is_table_cell() && net_browser_html_tag[0] == '/') {
        net_browser_append_char(' ');
        net_browser_render_in_table_cell = 0u;
    } else if (net_tag_is_table_section()) {
        net_browser_render_break();
    } else if (net_tag_name_is("dt") && net_browser_html_tag[0] != '/') {
        net_browser_render_break();
    } else if (net_tag_name_is("dd") && net_browser_html_tag[0] != '/') {
        net_browser_render_break();
        net_browser_render_in_list = 1u;
    } else if (net_tag_name_is("dd") && net_browser_html_tag[0] == '/') {
        net_browser_render_break();
        net_browser_render_in_list = 0u;
    } else if (net_tag_is_open("option")) {
        net_browser_render_break();
        net_browser_append_char('-');
        net_browser_append_char(' ');
    } else if (net_tag_is("hr")) {
        net_browser_render_break();
        net_browser_render_add_block_flags(NET_BROWSER_RENDER_KIND_TEXT,
                                           NET_BROWSER_RENDER_FLAG_BOX,
                                           "--------------------------------",
                                           "");
    } else if (net_tag_starts_with("br") || net_tag_is_block_name()) {
        net_browser_render_break();
    }
    if (net_tag_is_open("style")) {
        net_browser_css_in_style = 1u;
        net_browser_html_skip = 1u;
    } else if (net_tag_is_open("script")) {
        net_browser_js_in_script = 1u;
        net_browser_html_skip = 1u;
    } else if (net_tag_is_open("template")) {
        net_browser_html_skip = 1u;
    } else if (net_tag_is("/style")) {
        net_browser_css_finish_token();
        net_browser_css_in_style = 0u;
        net_browser_html_skip = 0u;
        net_browser_append_char('\n');
    } else if (net_tag_is("/script")) {
        net_browser_js_finish_token();
        net_browser_js_in_script = 0u;
        net_browser_html_skip = 0u;
        net_browser_append_char('\n');
    } else if (net_tag_is("/template")) {
        net_browser_html_skip = 0u;
        net_browser_append_char('\n');
    } else if (!net_browser_html_skip &&
               (net_tag_starts_with("br") || net_tag_starts_with("p") || net_tag_is("/p") ||
                net_tag_starts_with("div") || net_tag_is("/div") ||
                net_tag_is_block_name() ||
                net_tag_starts_with("li") || net_tag_is("/li") ||
                net_tag_starts_with("tr") || net_tag_is("/tr") ||
                net_tag_is_table_section() ||
                net_tag_is_table_cell() ||
                net_tag_is_heading_open() || net_tag_is_heading_close() ||
                net_tag_starts_with("title") || net_tag_is("/title"))) {
        net_browser_append_char('\n');
    }
    net_browser_attr_reset_current();
}

static void net_browser_emit_entity(void)
{
    net_browser_html_entity_buf[net_browser_html_entity_len] = 0;
    if (net_browser_html_entity_len != 0u &&
        net_browser_html_entity_buf[0] == '#') {
        u32 value = 0u;
        u32 i = 1u;
        u8 base = 10u;
        u8 valid = 1u;
        if (net_browser_html_entity_buf[i] == 'x') {
            base = 16u;
            i += 1u;
        }
        for (; i < net_browser_html_entity_len; i += 1u) {
            u8 ch = (u8) net_browser_html_entity_buf[i];
            u8 digit = 0xFFu;
            if (ch >= '0' && ch <= '9') {
                digit = (u8) (ch - '0');
            } else if (base == 16u && ch >= 'a' && ch <= 'f') {
                digit = (u8) (10u + ch - 'a');
            }
            if (digit >= base) {
                valid = 0u;
                break;
            }
            value = value * base + digit;
        }
        if (valid && value >= 32u && value <= 126u) {
            net_browser_append_char((char) value);
        } else if (valid && value == 160u) {
            net_browser_append_char(' ');
        } else {
            net_browser_append_char(' ');
        }
    } else if (net_browser_html_entity_len == 2u &&
        net_browser_html_entity_buf[0] == 'l' &&
        net_browser_html_entity_buf[1] == 't') {
        net_browser_append_char('<');
    } else if (net_browser_html_entity_len == 2u &&
               net_browser_html_entity_buf[0] == 'g' &&
               net_browser_html_entity_buf[1] == 't') {
        net_browser_append_char('>');
    } else if (net_browser_html_entity_len == 3u &&
               net_browser_html_entity_buf[0] == 'a' &&
               net_browser_html_entity_buf[1] == 'm' &&
               net_browser_html_entity_buf[2] == 'p') {
        net_browser_append_char('&');
    } else if (net_browser_html_entity_len == 4u &&
               net_browser_html_entity_buf[0] == 'n' &&
               net_browser_html_entity_buf[1] == 'b' &&
               net_browser_html_entity_buf[2] == 's' &&
               net_browser_html_entity_buf[3] == 'p') {
        net_browser_append_char(' ');
    } else if (net_browser_html_entity_len == 4u &&
               net_browser_html_entity_buf[0] == 'q' &&
               net_browser_html_entity_buf[1] == 'u' &&
               net_browser_html_entity_buf[2] == 'o' &&
               net_browser_html_entity_buf[3] == 't') {
        net_browser_append_char('"');
    } else if (net_browser_html_entity_len == 4u &&
               net_browser_html_entity_buf[0] == 'a' &&
               net_browser_html_entity_buf[1] == 'p' &&
               net_browser_html_entity_buf[2] == 'o' &&
               net_browser_html_entity_buf[3] == 's') {
        net_browser_append_char('\'');
    } else if ((net_browser_html_entity_len == 5u &&
                net_browser_html_entity_buf[0] == 'm' &&
                net_browser_html_entity_buf[1] == 'd' &&
                net_browser_html_entity_buf[2] == 'a' &&
                net_browser_html_entity_buf[3] == 's' &&
                net_browser_html_entity_buf[4] == 'h') ||
               (net_browser_html_entity_len == 5u &&
                net_browser_html_entity_buf[0] == 'n' &&
                net_browser_html_entity_buf[1] == 'd' &&
                net_browser_html_entity_buf[2] == 'a' &&
                net_browser_html_entity_buf[3] == 's' &&
                net_browser_html_entity_buf[4] == 'h')) {
        net_browser_append_char('-');
    } else if (net_browser_html_entity_len == 6u &&
               net_browser_html_entity_buf[0] == 'h' &&
               net_browser_html_entity_buf[1] == 'e' &&
               net_browser_html_entity_buf[2] == 'l' &&
               net_browser_html_entity_buf[3] == 'l' &&
               net_browser_html_entity_buf[4] == 'i' &&
               net_browser_html_entity_buf[5] == 'p') {
        net_browser_append_char('.');
        net_browser_append_char('.');
        net_browser_append_char('.');
    } else {
        net_browser_append_char(' ');
    }
}

static void net_browser_html_feed(const u8 *html, u32 len)
{
    for (u32 i = 0u; i < len; i += 1u) {
        u8 ch = html[i];
        if (net_browser_html_entity) {
            if (ch == ';') {
                net_browser_emit_entity();
                net_browser_html_entity = 0u;
                net_browser_html_entity_len = 0u;
            } else if (net_browser_html_entity_len < sizeof(net_browser_html_entity_buf) - 1u) {
                net_browser_html_entity_buf[net_browser_html_entity_len++] =
                    (char) net_ascii_lower(ch);
            } else {
                net_browser_append_char(' ');
                net_browser_html_entity = 0u;
                net_browser_html_entity_len = 0u;
            }
            continue;
        }
        if (net_browser_html_in_tag) {
            if (ch == '>') {
                net_browser_finish_tag();
                net_browser_html_in_tag = 0u;
                net_browser_html_ignore_tag = 0u;
            } else if (net_browser_html_ignore_tag) {
                continue;
            } else if (!net_browser_html_tag_done) {
                u8 lower = net_ascii_lower(ch);
                if (net_browser_html_tag_len < sizeof(net_browser_html_tag) - 1u &&
                    ((lower >= 'a' && lower <= 'z') ||
                     (lower >= '0' && lower <= '9') ||
                     (lower == '-' && net_browser_html_tag_len != 0u) ||
                     (lower == '/' && net_browser_html_tag_len == 0u))) {
                    net_browser_html_tag[net_browser_html_tag_len++] = (char) lower;
                } else if (net_browser_html_tag_len == 0u &&
                           (ch == '!' || ch == '?')) {
                    net_browser_html_ignore_tag = 1u;
                    net_browser_html_tag_done = 1u;
                } else if (net_browser_html_tag_len != 0u) {
                    net_browser_html_tag_done = 1u;
                    net_browser_attr_feed(ch);
                }
            } else {
                net_browser_attr_feed(ch);
            }
            continue;
        }
        if (ch == '<') {
            net_browser_html_in_tag = 1u;
            net_browser_html_tag_len = 0u;
            net_browser_html_tag_done = 0u;
            net_browser_html_ignore_tag = 0u;
            net_browser_render_tag_flags = 0u;
            net_browser_css_clear_tag_style();
            net_browser_css_clear_inline_style();
            net_browser_control_type_len = 0u;
            net_browser_control_type[0] = 0;
            net_browser_control_value_len = 0u;
            net_browser_control_value[0] = 0;
            net_browser_current_class[0] = 0;
            net_browser_current_id[0] = 0;
            net_browser_current_img_src[0] = 0;
            net_browser_current_img_alt[0] = 0;
            net_browser_current_control_name[0] = 0;
            net_browser_current_control_actual[0] = 0;
            net_browser_current_form_action[0] = 0;
            net_browser_current_form_method[0] = 0;
            net_browser_attr_reset_current();
            continue;
        }
        if (net_browser_html_skip) {
            if (net_browser_css_in_style) {
                net_browser_css_feed(ch);
            } else if (net_browser_js_in_script) {
                net_browser_js_feed(ch);
            }
            continue;
        }
        if (ch == '&') {
            net_browser_html_entity = 1u;
            net_browser_html_entity_len = 0u;
            continue;
        }
        if (ch == '\n') {
            net_browser_append_char('\n');
        } else if (ch == '\r' || ch == '\t' || ch == ' ') {
            net_browser_append_char(' ');
        } else {
            net_browser_append_char((char) ch);
        }
    }
}

static void net_browser_open_layout_selftest(void)
{
    static const char html[] =
        "<!doctype html><html><head><title>DOM Layout Selftest</title>"
        "<style>.card{border:1px solid #ccd;padding:4px}.wide{font-weight:bold}</style>"
        "<script>document.write('script generated text')</script></head><body>"
        "<h1>DOM Layout Selftest</h1>"
        "<p>This inline paragraph has <a href=\"/next\">a real link</a> and "
        "<strong>bold inline text</strong> with enough words to wrap across multiple "
        "layout lines inside the browser viewport.</p>"
        "<p>A long plain paragraph continues with many ordinary words so the layout engine "
        "must wrap one supported inline text run across more than one visual line while "
        "preserving the same block flow and without inventing fake browser behavior.</p>"
        "<div class=\"card\"><p>Block card with padding and border.</p>"
        "<form action=\"/search\"><label>Search <input type=\"search\" name=\"q\" "
        "placeholder=\"query\"></label><button>Go</button></form></div>"
        "<table><caption>Routes</caption><tr><th>Name</th><th>Status</th></tr>"
        "<tr><td>Google</td><td>Loaded</td></tr><tr><td>Layout</td><td>Boxed</td></tr></table>"
        "<img src=\"/logo.png\" alt=\"fixture image\">"
        "<canvas title=\"demo canvas\"></canvas><custom-widget title=\"custom component\"></custom-widget>"
        "<svg title=\"vector logo\"><path d=\"M0 0L10 10\"></path></svg>"
        "</body></html>";

    net_browser_history_save_scroll();
    net_append_capped(net_browser_current_url, 0u,
                      sizeof(net_browser_current_url),
                      "leonos://dom-layout-selftest");
    net_append_capped(net_browser_host, 0u, sizeof(net_browser_host),
                      "layout.local");
    net_append_capped(net_browser_path, 0u, sizeof(net_browser_path),
                      "/dom-layout-selftest");
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
                      "DOM LAYOUT SELFTEST");
    net_append_capped(net_browser_location, 0u, sizeof(net_browser_location),
                      net_browser_current_url);
    serial_print("LeonOS net browser DOM layout selftest open\r\n");
    net_browser_html_feed((const u8 *) html, sizeof(html) - 1u);
    net_browser_text_ready = 1u;
    net_https_body_decoded_total32 = sizeof(html) - 1u;
    net_browser_maybe_print_structure();
    net_browser_finalize_primary_response("fixture");
    dirty = 1;
}

static void net_browser_open_css_selftest(void)
{
    static const char html[] =
        "<!doctype html><html><head><title>CSS Subset Selftest</title>"
        "<style>"
        "body{color:#1f2937;background:#ffffff}"
        "p{color:#1a5fb4;margin:8px;padding:4px;border:1px solid #8aa;background:#eef6ff}"
        ".card{background-color:#dff7e8;width:420px;height:72px;padding:10px;border:3px solid #2f8}"
        "#accent{color:rgb(190,40,60);font-weight:700;font-size:22px}"
        ".gone{display:none}"
        ".cascade{color:#336699;background:#dde}"
        ".cascade{color:#cc5500;background:#fff1d6}"
        "</style></head><body>"
        "<h1>CSS Subset Selftest</h1>"
        "<p>Tag selector styled with color, margin, padding, border, and background.</p>"
        "<div class=\"card\">Class selector sets width, height, padding, border, and background.</div>"
        "<p id=\"accent\">ID selector sets red text, bold weight, and larger font.</p>"
        "<p class=\"gone\">CSS HIDDEN TEXT SHOULD NOT RENDER</p>"
        "<p class=\"cascade\">Cascade selector uses the later orange rule.</p>"
        "<p style=\"color:green;background:#f0fff4;width:360px;height:48px\">Inline style overrides and paints this row.</p>"
        "</body></html>";

    net_browser_history_save_scroll();
    net_append_capped(net_browser_current_url, 0u,
                      sizeof(net_browser_current_url),
                      "leonos://css-subset-selftest");
    net_append_capped(net_browser_host, 0u, sizeof(net_browser_host),
                      "css.local");
    net_append_capped(net_browser_path, 0u, sizeof(net_browser_path),
                      "/css-subset-selftest");
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
                      "CSS SUBSET SELFTEST");
    net_append_capped(net_browser_location, 0u, sizeof(net_browser_location),
                      net_browser_current_url);
    serial_print("LeonOS net browser CSS selftest open\r\n");
    net_browser_html_feed((const u8 *) html, sizeof(html) - 1u);
    net_browser_text_ready = 1u;
    net_https_body_decoded_total32 = sizeof(html) - 1u;
    net_browser_maybe_print_structure();
    net_browser_finalize_primary_response("fixture");
    dirty = 1;
}

static void net_browser_open_unsupported_selftest(void)
{
    static const char html[] =
        "<!doctype html><html><head><title>Unsupported Feature Selftest</title>"
        "<script>function boot(){var next=window.location;document.write('dynamic script text');}</script>"
        "</head><body>"
        "<h1>Unsupported Feature Selftest</h1>"
        "<noscript>JavaScript is required for this dynamic page.</noscript>"
        "<p>This static paragraph must remain visible in strict no-JS mode.</p>"
        "<button onclick=\"boot()\">Run dynamic action</button>"
        "<a href=\"javascript:boot()\">JavaScript link</a>"
        "<canvas title=\"live chart\"></canvas>"
        "<iframe title=\"remote frame\" src=\"https://example.com/embed\"></iframe>"
        "<custom-widget title=\"custom app shell\"></custom-widget>"
        "<template><p>client template</p></template>"
        "<picture><source srcset=\"/big.webp 2x\"><img src=\"/fallback.svg\" alt=\"vector fallback\"></picture>"
        "<svg title=\"vector\"><path d=\"M0 0L10 10\"></path></svg>"
        "</body></html>";

    net_browser_history_save_scroll();
    net_append_capped(net_browser_current_url, 0u,
                      sizeof(net_browser_current_url),
                      "leonos://unsupported-selftest");
    net_append_capped(net_browser_host, 0u, sizeof(net_browser_host),
                      "unsupported.local");
    net_append_capped(net_browser_path, 0u, sizeof(net_browser_path),
                      "/unsupported-selftest");
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
                      "UNSUPPORTED SELFTEST");
    net_append_capped(net_browser_location, 0u, sizeof(net_browser_location),
                      net_browser_current_url);
    serial_print("LeonOS net browser unsupported selftest open\r\n");
    net_browser_html_feed((const u8 *) html, sizeof(html) - 1u);
    net_browser_text_ready = 1u;
    net_https_body_decoded_total32 = sizeof(html) - 1u;
    net_browser_maybe_print_structure();
    net_browser_finalize_primary_response("fixture");
    dirty = 1;
}

static void net_browser_open_port_status_page(void)
{
    static const char html[] =
        "<!doctype html><html><head><title>LeonOS Browser</title>"
        "<style>"
        "body{font-family:sans-serif;margin:16px;background:#f8fafc;color:#111}"
        "h1{color:#1a73e8}div.card{background:#fff;border:1px solid #ccd;padding:12px;margin:12px 0}"
        "p.note{color:#444}"
        "</style></head><body>"
        "<h1>LeonOS Browser</h1>"
        "<div class=\"card\"><p>This is the real LeonOS HTTPS browser: TLS, HTML, "
        "layout, forms, images, navigation, and an honest strict NOJS mode.</p>"
        "<p class=\"note\">Programs exposes one Browser app. It opens inside the "
        "LeonOS desktop shell so the taskbar, mouse, and window controls remain "
        "available while the page loads.</p></div>"
        "<div class=\"card\"><p>Launch <strong>Browser</strong> from Programs or "
        "press <strong>F11</strong> to open this shell-managed browser window.</p>"
        "<p>Try a real page: https://www.example.com/</p></div>"
        "</body></html>";

    net_browser_history_save_scroll();
    net_append_capped(net_browser_current_url, 0u,
                      sizeof(net_browser_current_url), "leonos://browser");
    net_append_capped(net_browser_host, 0u, sizeof(net_browser_host), "leonos.local");
    net_append_capped(net_browser_path, 0u, sizeof(net_browser_path), "/browser");
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
                      "LEONOS BROWSER");
    net_append_capped(net_browser_location, 0u, sizeof(net_browser_location),
                      net_browser_current_url);
    serial_print("LeonOS net browser port status open\r\n");
    net_browser_html_feed((const u8 *) html, sizeof(html) - 1u);
    net_browser_text_ready = 1u;
    net_https_body_decoded_total32 = sizeof(html) - 1u;
    net_browser_maybe_print_structure();
    net_browser_finalize_primary_response("fixture");
    dirty = 1;
}

static u8 net_browser_open_local_history_url(const char *url)
{
    if (net_text_is(url, "leonos://browser")) {
        net_browser_history_suppress = 1u;
        net_browser_open_port_status_page();
        net_browser_history_suppress = 0u;
        return 1u;
    }
    if (net_text_is(url, "leonos://dom-layout-selftest")) {
        net_browser_history_suppress = 1u;
        net_browser_open_layout_selftest();
        net_browser_history_suppress = 0u;
        return 1u;
    }
    if (net_text_is(url, "leonos://image-selftest")) {
        net_browser_history_suppress = 1u;
        net_browser_open_image_selftest();
        net_browser_history_suppress = 0u;
        return 1u;
    }
    if (net_text_is(url, "leonos://css-subset-selftest")) {
        net_browser_history_suppress = 1u;
        net_browser_open_css_selftest();
        net_browser_history_suppress = 0u;
        return 1u;
    }
    if (net_text_is(url, "leonos://unsupported-selftest")) {
        net_browser_history_suppress = 1u;
        net_browser_open_unsupported_selftest();
        net_browser_history_suppress = 0u;
        return 1u;
    }
    return 0u;
}

static void net_browser_open_history_current(const char *event)
{
    if (net_browser_history_count == 0u ||
        net_browser_history_index >= net_browser_history_count) {
        return;
    }
    const char *url = net_browser_history[net_browser_history_index];
    u32 restore_scroll = net_browser_history_scroll[net_browser_history_index];
    serial_print(msg_net_browser_history);
    serial_print(event);
    serial_write(' ');
    serial_print_dec(net_browser_history_index);
    serial_write('/');
    serial_print_dec(net_browser_history_count);
    serial_print(" scroll ");
    serial_print_dec(restore_scroll);
    serial_write(' ');
    serial_print(url);
    serial_print("\r\n");
    if (net_browser_open_local_history_url(url)) {
        net_browser_scroll = restore_scroll;
    } else {
        net_browser_open_url_internal(url, 0u, 1u);
    }
    net_browser_scroll = restore_scroll;
    dirty = 1;
}

static void net_browser_go_back(void)
{
    if (!net_browser_can_back()) {
        serial_print(msg_net_browser_history);
        serial_print("back unavailable\r\n");
        return;
    }
    net_browser_history_save_scroll();
    net_browser_history_index -= 1u;
    if (net_browser_history_back_count < 65535u) {
        net_browser_history_back_count += 1u;
    }
    net_browser_open_history_current("back");
}

static void net_browser_go_forward(void)
{
    if (!net_browser_can_forward()) {
        serial_print(msg_net_browser_history);
        serial_print("forward unavailable\r\n");
        return;
    }
    net_browser_history_save_scroll();
    net_browser_history_index += 1u;
    if (net_browser_history_forward_count < 65535u) {
        net_browser_history_forward_count += 1u;
    }
    net_browser_open_history_current("forward");
}

static void net_browser_stop_loading(void)
{
    if (!net_browser_fetch_enabled &&
        !net_browser_resource_fetch_pending &&
        !net_browser_resource_fetch_started) {
        serial_print(msg_net_browser_history);
        serial_print("stop idle\r\n");
        return;
    }
    net_tls_abort_current_for_navigation();
    net_browser_fetch_enabled = 0u;
    net_browser_resource_fetch_pending = 0u;
    net_browser_resource_fetch_started = 0u;
    net_browser_resource_fetch_done = 1u;
    net_browser_set_resource_phase("STOP");
    net_https_body_complete = 1u;
    net_browser_finalize_sent = 1u;
    if (!net_browser_text_ready && net_browser_text_len == 0u) {
        net_browser_text_len = net_append_capped(net_browser_text, 0u,
            sizeof(net_browser_text), "LOAD STOPPED\n\n");
        net_browser_text_len = net_append_capped(net_browser_text,
            net_browser_text_len, sizeof(net_browser_text), net_browser_current_url);
        net_browser_text_ready = 1u;
    } else if (!net_browser_text_ready) {
        net_browser_text_ready = 1u;
    }
    net_browser_status[0] = 'S';
    net_browser_status[1] = 'T';
    net_browser_status[2] = 'P';
    net_browser_status[3] = 0;
    net_browser_line[0] = 'L';
    net_browser_line[1] = 'O';
    net_browser_line[2] = 'A';
    net_browser_line[3] = 'D';
    net_browser_line[4] = ' ';
    net_browser_line[5] = 'S';
    net_browser_line[6] = 'T';
    net_browser_line[7] = 'O';
    net_browser_line[8] = 'P';
    net_browser_line[9] = 'P';
    net_browser_line[10] = 'E';
    net_browser_line[11] = 'D';
    net_browser_line[12] = 0;
    if (net_browser_history_stop_count < 65535u) {
        net_browser_history_stop_count += 1u;
    }
    net_browser_set_state("STOPPED");
    serial_print(msg_net_browser_history);
    serial_print("stop ");
    serial_print(net_browser_current_url);
    serial_print("\r\n");
    dirty = 1;
}

static u32 net_http_body_offset(const u8 *plain, u32 plain_len, u8 *found)
{
    *found = 0u;
    for (u32 i = 0u; i + 3u < plain_len; i += 1u) {
        if (plain[i] == '\r' && plain[i + 1u] == '\n' &&
            plain[i + 2u] == '\r' && plain[i + 3u] == '\n') {
            *found = 1u;
            return i + 4u;
        }
    }
    return plain_len;
}

static u8 net_http_match_ci(const u8 *data, u32 pos, u32 len, const char *needle)
{
    u32 i = 0u;
    while (needle[i] != 0) {
        if (pos + i >= len ||
            net_ascii_lower(data[pos + i]) != (u8) needle[i]) {
            return 0u;
        }
        i += 1u;
    }
    return 1u;
}

static u8 net_http_header_has_token(const u8 *headers, u32 header_len,
                                    const char *name, const char *token)
{
    u32 name_len = 0u;
    while (name[name_len] != 0) {
        name_len += 1u;
    }
    u32 token_len = 0u;
    while (token[token_len] != 0) {
        token_len += 1u;
    }
    if (name_len == 0u || token_len == 0u) {
        return 0u;
    }

    u32 line = 0u;
    while (line < header_len) {
        u32 end = line;
        while (end < header_len && headers[end] != '\r' && headers[end] != '\n') {
            end += 1u;
        }
        u32 colon = line;
        while (colon < end && headers[colon] != ':') {
            colon += 1u;
        }
        if (colon < end && colon - line == name_len &&
            net_http_match_ci(headers, line, header_len, name)) {
            for (u32 scan = colon + 1u; scan + token_len <= end; scan += 1u) {
                if (net_http_match_ci(headers, scan, header_len, token)) {
                    return 1u;
                }
            }
        }
        line = end;
        while (line < header_len && (headers[line] == '\r' || headers[line] == '\n')) {
            line += 1u;
        }
    }
    return 0u;
}

static u8 net_http_header_copy_value(const u8 *headers, u32 header_len,
                                     const char *name, char *dst, u32 dst_len)
{
    u32 name_len = 0u;
    while (name[name_len] != 0) {
        name_len += 1u;
    }
    if (name_len == 0u || dst_len == 0u) {
        return 0u;
    }
    dst[0] = 0;

    u32 line = 0u;
    while (line < header_len) {
        u32 end = line;
        while (end < header_len && headers[end] != '\r' && headers[end] != '\n') {
            end += 1u;
        }
        u32 colon = line;
        while (colon < end && headers[colon] != ':') {
            colon += 1u;
        }
        if (colon < end && colon - line == name_len &&
            net_http_match_ci(headers, line, header_len, name)) {
            u32 scan = colon + 1u;
            while (scan < end &&
                   (headers[scan] == ' ' || headers[scan] == '\t')) {
                scan += 1u;
            }
            u32 out = 0u;
            while (scan < end && out + 1u < dst_len) {
                u8 ch = headers[scan++];
                if (ch < 32u || ch > 126u) {
                    break;
                }
                dst[out++] = (char) ch;
            }
            dst[out] = 0;
            return out != 0u;
        }
        line = end;
        while (line < header_len && (headers[line] == '\r' || headers[line] == '\n')) {
            line += 1u;
        }
    }
    return 0u;
}

static u8 net_http_header_copy_u32(const u8 *headers, u32 header_len,
                                   const char *name, u32 *value)
{
    char text[16];
    u32 out = 0u;

    *value = 0u;
    if (!net_http_header_copy_value(headers, header_len, name,
                                    text, sizeof(text))) {
        return 0u;
    }
    for (u32 i = 0u; text[i] != 0; i += 1u) {
        if (text[i] < '0' || text[i] > '9') {
            break;
        }
        if (out > 429496729u) {
            return 0u;
        }
        out = out * 10u + (u32) (text[i] - '0');
    }
    if (out == 0u) {
        return 0u;
    }
    *value = out;
    return 1u;
}

static u8 net_http_hex_value(u8 ch, u8 *value)
{
    if (ch >= '0' && ch <= '9') {
        *value = (u8) (ch - '0');
        return 1u;
    }
    ch = net_ascii_lower(ch);
    if (ch >= 'a' && ch <= 'f') {
        *value = (u8) (10u + ch - 'a');
        return 1u;
    }
    return 0u;
}

static void net_browser_resource_note_body(const u8 *data, u32 len)
{
    net_browser_resource_fetch_body_seen = 1u;
    if (net_browser_resource_fetch_bytes + len > 65535u) {
        net_browser_resource_fetch_bytes = 65535u;
    } else {
        net_browser_resource_fetch_bytes += (u16) len;
    }
    net_browser_image_note_resource_body(data, len);
}

static void net_http_chunked_feed_html(const u8 *data, u32 len)
{
    for (u32 i = 0u; i < len; i += 1u) {
        if (net_tls_fetch_kind == 0u && net_https_body_complete) {
            return;
        }
        u8 ch = data[i];
        if (net_http_chunk_state == 0u) {
            u8 value = 0u;
            if (net_http_chunk_skip_ext) {
                if (ch == '\r') {
                    net_http_chunk_state = 1u;
                } else if (ch == '\n') {
                    if (net_http_chunk_seen_digit && net_http_chunk_remaining != 0u) {
                        net_http_chunk_state = 2u;
                    } else {
                        net_http_chunk_state = 5u;
                        net_https_body_complete = 1u;
                        net_browser_finalize_primary_response("chunked");
                    }
                    net_http_chunk_seen_digit = 0u;
                    net_http_chunk_skip_ext = 0u;
                }
            } else if (net_http_hex_value(ch, &value)) {
                net_http_chunk_seen_digit = 1u;
                if (net_http_chunk_remaining <= 0x0FFFFFFFu) {
                    net_http_chunk_remaining = (net_http_chunk_remaining << 4) | value;
                }
            } else if (ch == ';') {
                net_http_chunk_skip_ext = 1u;
            } else if (ch == '\r') {
                net_http_chunk_state = 1u;
            } else if (ch == '\n') {
                if (net_http_chunk_seen_digit && net_http_chunk_remaining != 0u) {
                    net_http_chunk_state = 2u;
                } else {
                    net_http_chunk_state = 5u;
                    net_https_body_complete = 1u;
                    net_browser_finalize_primary_response("chunked");
                }
                net_http_chunk_seen_digit = 0u;
            }
        } else if (net_http_chunk_state == 1u) {
            if (ch == '\n') {
                if (net_http_chunk_seen_digit && net_http_chunk_remaining != 0u) {
                    net_http_chunk_state = 2u;
                } else {
                    net_http_chunk_state = 5u;
                    net_https_body_complete = 1u;
                    net_browser_finalize_primary_response("chunked");
                }
                net_http_chunk_seen_digit = 0u;
                net_http_chunk_skip_ext = 0u;
            }
        } else if (net_http_chunk_state == 2u) {
            u32 take = len - i;
            if (take > net_http_chunk_remaining) {
                take = net_http_chunk_remaining;
            }
            if (take != 0u) {
                net_https_log_primary_decoded_bytes(take);
                if (net_browser_direct_image_active) {
                    net_browser_image_note_direct_body(data + i, take);
                } else {
                    net_browser_html_feed(data + i, take);
                }
                net_browser_note_decoded_body(take);
                if (net_https_body_complete) {
                    return;
                }
                i += take - 1u;
                net_http_chunk_remaining -= take;
            }
            if (net_http_chunk_remaining == 0u) {
                net_http_chunk_state = 3u;
            }
        } else if (net_http_chunk_state == 3u) {
            if (ch == '\n') {
                net_http_chunk_state = 0u;
                net_http_chunk_remaining = 0u;
                net_http_chunk_seen_digit = 0u;
                net_http_chunk_skip_ext = 0u;
            } else if (ch == '\r') {
                net_http_chunk_state = 4u;
            }
        } else if (net_http_chunk_state == 4u) {
            if (ch == '\n') {
                net_http_chunk_state = 0u;
                net_http_chunk_remaining = 0u;
                net_http_chunk_seen_digit = 0u;
                net_http_chunk_skip_ext = 0u;
            }
        } else if (net_http_chunk_state == 5u) {
            return;
        }
    }
}

static void net_http_chunked_feed_user(const u8 *data, u32 len)
{
    for (u32 i = 0u; i < len; i += 1u) {
        if (net_https_body_complete) {
            return;
        }
        u8 ch = data[i];
        if (net_http_chunk_state == 0u) {
            u8 value = 0u;
            if (net_http_chunk_skip_ext) {
                if (ch == '\r') {
                    net_http_chunk_state = 1u;
                } else if (ch == '\n') {
                    if (net_http_chunk_seen_digit &&
                        net_http_chunk_remaining != 0u) {
                        net_http_chunk_state = 2u;
                    } else {
                        net_http_chunk_state = 5u;
                        net_https_body_complete = 1u;
                        net_browser_finalize_primary_response("chunked");
                    }
                    net_http_chunk_seen_digit = 0u;
                    net_http_chunk_skip_ext = 0u;
                }
            } else if (net_http_hex_value(ch, &value)) {
                net_http_chunk_seen_digit = 1u;
                if (net_http_chunk_remaining <= 0x0FFFFFFFu) {
                    net_http_chunk_remaining =
                        (net_http_chunk_remaining << 4) | value;
                }
            } else if (ch == ';') {
                net_http_chunk_skip_ext = 1u;
            } else if (ch == '\r') {
                net_http_chunk_state = 1u;
            } else if (ch == '\n') {
                if (net_http_chunk_seen_digit &&
                    net_http_chunk_remaining != 0u) {
                    net_http_chunk_state = 2u;
                } else {
                    net_http_chunk_state = 5u;
                    net_https_body_complete = 1u;
                    net_browser_finalize_primary_response("chunked");
                }
                net_http_chunk_seen_digit = 0u;
            }
        } else if (net_http_chunk_state == 1u) {
            if (ch == '\n') {
                if (net_http_chunk_seen_digit &&
                    net_http_chunk_remaining != 0u) {
                    net_http_chunk_state = 2u;
                } else {
                    net_http_chunk_state = 5u;
                    net_https_body_complete = 1u;
                    net_browser_finalize_primary_response("chunked");
                }
                net_http_chunk_seen_digit = 0u;
                net_http_chunk_skip_ext = 0u;
            }
        } else if (net_http_chunk_state == 2u) {
            u32 take = len - i;
            if (take > net_http_chunk_remaining) {
                take = net_http_chunk_remaining;
            }
            if (take != 0u) {
                net_user_fetch_note_body(data + i, take);
                net_browser_note_decoded_body(take);
                if (net_https_body_complete) {
                    return;
                }
                i += take - 1u;
                net_http_chunk_remaining -= take;
            }
            if (net_http_chunk_remaining == 0u) {
                net_http_chunk_state = 3u;
            }
        } else if (net_http_chunk_state == 3u) {
            if (ch == '\n') {
                net_http_chunk_state = 0u;
                net_http_chunk_remaining = 0u;
                net_http_chunk_seen_digit = 0u;
                net_http_chunk_skip_ext = 0u;
            } else if (ch == '\r') {
                net_http_chunk_state = 4u;
            }
        } else if (net_http_chunk_state == 4u) {
            if (ch == '\n') {
                net_http_chunk_state = 0u;
                net_http_chunk_remaining = 0u;
                net_http_chunk_seen_digit = 0u;
                net_http_chunk_skip_ext = 0u;
            }
        } else if (net_http_chunk_state == 5u) {
            return;
        }
    }
}

static void net_browser_feed_body(const u8 *data, u32 len)
{
    if (len == 0u) {
        return;
    }
	if (net_user_fetch_active) {
		if (net_https_chunked) {
			net_http_chunked_feed_user(data, len);
		} else {
			net_user_fetch_note_body(data, len);
			net_browser_note_decoded_body(len);
		}
		return;
	}
	if (net_user_fetch_ignore_tail) {
		return;
	}
    if (net_tls_fetch_kind == 0u && net_https_body_raw_log_count < 8u) {
        net_https_body_raw_log_count += 1u;
        if (net_https_body_raw_total + len > 65535u) {
            net_https_body_raw_total = 65535u;
        } else {
            net_https_body_raw_total += (u16) len;
        }
        serial_print("LeonOS net HTTPS body raw bytes ");
        serial_print_dec(len);
        serial_print(" total ");
        serial_print_dec(net_https_body_raw_total);
        serial_print("\r\n");
    }
    if (net_tls_fetch_kind != 0u) {
        net_browser_resource_note_body(data, len);
        return;
    }
    if (net_browser_direct_image_active) {
        net_https_log_primary_decoded_bytes(len);
        net_browser_image_note_direct_body(data, len);
        net_browser_note_decoded_body(len);
        return;
    }
    if (net_https_chunked) {
        net_http_chunked_feed_html(data, len);
    } else {
        net_https_log_primary_decoded_bytes(len);
        net_browser_html_feed(data, len);
        net_browser_note_decoded_body(len);
    }
}

static void net_tls_parse_https_plain(const u8 *plain, u32 plain_len)
{
    if (plain_len == 0u) {
        return;
    }
    if (!net_https_response_seen &&
        (plain_len < 12u ||
         plain[0] != 'H' || plain[1] != 'T' || plain[2] != 'T' ||
         plain[3] != 'P' || plain[4] != '/')) {
        return;
    }
    if (!net_https_response_seen) {
        net_https_response_seen = 1u;
        if (net_tls_fetch_kind == 0u) {
            net_browser_text_reset();
            net_browser_status[0] = '2';
            net_browser_status[1] = '?';
            net_browser_status[2] = '?';
            net_browser_status[3] = 0;
        }
        for (u32 i = 0u; i < 3u && 9u + i < plain_len; i += 1u) {
            net_https_status[i] = (char) plain[9u + i];
            if (net_tls_fetch_kind == 0u) {
                net_browser_status[i] = (char) plain[9u + i];
            } else {
                net_browser_resource_fetch_status[i] = (char) plain[9u + i];
            }
        }
        net_https_status[3] = 0;
        if (net_tls_fetch_kind == 0u) {
            u32 out = 0u;
            for (u32 i = 0u; i < plain_len && out < sizeof(net_browser_line) - 1u; i += 1u) {
                char ch = (char) plain[i];
                if (ch == '\r' || ch == '\n') {
                    break;
                }
                if ((u8) ch < 32u) {
                    ch = '.';
                }
                net_browser_line[out++] = ch;
            }
            net_browser_line[out] = 0;
            net_set_last("HTTPS RESPONSE RX");
            serial_print(msg_net_https_status_prefix);
            serial_print(net_https_status);
            serial_print("\r\n");
        } else {
            net_set_last("RESOURCE RESPONSE RX");
            serial_print(msg_net_browser_resource_status_prefix);
            serial_print(net_browser_resource_fetch_status);
            serial_print("\r\n");
        }
    }
    if (!net_https_headers_done) {
        u8 body_found = 0u;
        u32 append_len = plain_len;
        if (append_len > NET_HTTPS_HEADER_MAX - net_https_header_len) {
            append_len = NET_HTTPS_HEADER_MAX - net_https_header_len;
        }
        if (append_len != 0u) {
            mem_copy(net_https_header_buf + net_https_header_len, plain, append_len);
            net_https_header_len += append_len;
        }
        u32 body = net_http_body_offset(net_https_header_buf,
                                        net_https_header_len,
                                        &body_found);
        if (body_found) {
            net_https_headers_done = 1u;
            if (net_tls_fetch_kind == 0u) {
                net_https_headers_tick = system_ticks;
            }
            net_https_chunked = net_http_header_has_token(net_https_header_buf,
                                                          body,
                                                          "transfer-encoding",
                                                          "chunked");
            u8 response_gzip = net_http_header_has_token(net_https_header_buf,
                                                         body,
                                                         "content-encoding",
                                                         "gzip");
            if (net_user_fetch_active && net_tls_fetch_kind == 0u) {
                net_user_fetch_flags |= USER_NET_FETCH_FLAG_HEADERS;
                if (net_https_chunked) {
                    net_user_fetch_flags |= USER_NET_FETCH_FLAG_CHUNKED;
                }
                if (response_gzip) {
                    net_user_fetch_flags |= USER_NET_FETCH_FLAG_GZIP;
                }
                if (net_https_status[0] >= '0' && net_https_status[0] <= '9' &&
                    net_https_status[1] >= '0' && net_https_status[1] <= '9' &&
                    net_https_status[2] >= '0' && net_https_status[2] <= '9') {
                    net_user_fetch_status_code =
                        ((u32) (net_https_status[0] - '0') * 100u) +
                        ((u32) (net_https_status[1] - '0') * 10u) +
                        (u32) (net_https_status[2] - '0');
                }
                if (!net_http_header_copy_value(net_https_header_buf, body,
                                                "content-type",
                                                net_user_fetch_content_type,
                                                USER_NET_FETCH_CONTENT_TYPE_MAX)) {
                    net_user_fetch_content_type[0] = 0;
                }
                if (!net_http_header_copy_value(net_https_header_buf, body,
                                                "location",
                                                net_user_fetch_location,
                                                USER_NET_FETCH_LOCATION_MAX)) {
                    net_user_fetch_location[0] = 0;
                }
                net_user_fetch_content_length_seen = 0u;
                if (!net_https_chunked && !response_gzip &&
                    net_http_header_copy_u32(net_https_header_buf, body,
                                             "content-length",
                                             &net_user_fetch_content_length)) {
                    net_user_fetch_content_length_seen = 1u;
                } else {
                    net_user_fetch_content_length = 0u;
                }
                serial_print("LeonOS user net fetch meta status ");
                serial_print_dec(net_user_fetch_status_code);
                serial_print(" type ");
                serial_print(net_user_fetch_content_type[0] != 0 ?
                             net_user_fetch_content_type : "unknown");
                if (net_user_fetch_content_length_seen) {
                    serial_print(" length ");
                    serial_print_dec(net_user_fetch_content_length);
                }
                serial_print("\r\n");
            }
            net_cookie_capture_headers(net_https_header_buf, body);
            if (net_tls_fetch_kind == 0u && !net_https_header_log_sent) {
                net_https_header_log_sent = 1u;
                serial_print("LeonOS net HTTPS headers bytes ");
                serial_print_dec(body);
                serial_print(" chunked ");
                serial_print(net_https_chunked ? "yes" : "no");
                serial_print(" gzip ");
                serial_print(response_gzip ? "yes" : "no");
                serial_print(" body_first ");
                serial_print_dec(body < net_https_header_len ?
                                 net_https_header_len - body : 0u);
                serial_print("\r\n");
            }
            if (net_tls_fetch_kind == 0u &&
                !net_user_fetch_active &&
                net_browser_status[0] == '2' &&
                !net_browser_direct_image_active) {
                u8 image_format = NET_BROWSER_IMAGE_FORMAT_UNKNOWN;
                if (net_http_header_has_token(net_https_header_buf, body,
                                              "content-type", "image/png")) {
                    image_format = NET_BROWSER_IMAGE_FORMAT_PNG;
                } else if (net_http_header_has_token(net_https_header_buf, body,
                                                     "content-type", "image/jpeg")) {
                    image_format = NET_BROWSER_IMAGE_FORMAT_JPEG;
                } else {
                    image_format = net_browser_image_format_from_url(
                        net_browser_current_url);
                }
                if (image_format == NET_BROWSER_IMAGE_FORMAT_PNG ||
                    image_format == NET_BROWSER_IMAGE_FORMAT_JPEG) {
                    net_browser_image_start_direct_document(image_format);
                }
            }
            if (body < net_https_header_len) {
                net_browser_feed_body(net_https_header_buf + body,
                                      net_https_header_len - body);
            }
            if (append_len < plain_len) {
                net_browser_feed_body(plain + append_len, plain_len - append_len);
            }
        }
    } else {
        net_browser_feed_body(plain, plain_len);
    }
}

static void net_tls_handle_handshake_plain(const u8 *plain, u32 plain_len)
{
    u32 off = 0u;
    while (off + 4u <= plain_len) {
        u32 msg_len = ((u32) plain[off + 1u] << 16) |
                      ((u32) plain[off + 2u] << 8) |
                      plain[off + 3u];
        if (off + 4u + msg_len > plain_len) {
            return;
        }
        u8 msg_type = plain[off];
        if (msg_type == 0x14u && msg_len == 32u &&
            !net_tls_server_finished_seen) {
            u8 transcript_hash[32];
            u8 expected[32];
            net_tls_transcript_hash(transcript_hash);
            hmac_sha256(net_tls_server_finished_key, 32u, transcript_hash, 32u, expected);
            if (!mem_equal(expected, plain + off + 4u, 32u)) {
                net_set_last("TLS FINISHED BAD");
                serial_print_line(msg_net_tls_finished_bad);
                return;
            }
            sha256_update(&net_tls_transcript, plain + off, 4u + msg_len);
            net_tls_server_finished_seen = 1u;
            net_set_last("TLS SERVER FINISHED");
            net_tls_derive_application_keys();
            net_tls_send_encrypted_finished();
            net_tls_send_https_get();
        } else {
            sha256_update(&net_tls_transcript, plain + off, 4u + msg_len);
        }
        off += 4u + msg_len;
    }
}

static void net_tls_process_records(void)
{
    while (net_tls_rx_len >= 5u) {
        u32 rec_len = read_be16(net_tls_rx_buf + 3u);
        if (rec_len + 5u > net_tls_rx_len) {
            return;
        }
        u8 rec_type = net_tls_rx_buf[0];
        u8 *rec = net_tls_rx_buf;
        if (rec_type == 0x16u && rec_len >= 4u && !net_tls_handshake_keys_ready) {
            u8 server_pub[32];
            if (net_tls_parse_server_hello(rec + 5u, rec_len, server_pub)) {
                if (!net_tls_server_hello_seen &&
                    rec[5u] == 0x02u) {
                    net_tls_server_hello_seen = 1u;
                    net_set_last("TLS SERVERHELLO RX");
                    if (net_tls_fetch_kind == 0u) {
                        net_browser_status[0] = 'T';
                        net_browser_status[1] = 'L';
                        net_browser_status[2] = 'S';
                        net_browser_status[3] = 0;
                        net_browser_line[0] = 'H';
                        net_browser_line[1] = 'T';
                        net_browser_line[2] = 'T';
                        net_browser_line[3] = 'P';
                        net_browser_line[4] = 'S';
                        net_browser_line[5] = ' ';
                        net_browser_line[6] = 'S';
                        net_browser_line[7] = 'E';
                        net_browser_line[8] = 'R';
                        net_browser_line[9] = 'V';
                        net_browser_line[10] = 'E';
                        net_browser_line[11] = 'R';
                        net_browser_line[12] = 'H';
                        net_browser_line[13] = 'E';
                        net_browser_line[14] = 'L';
                        net_browser_line[15] = 'L';
                        net_browser_line[16] = 'O';
                        net_browser_line[17] = 0;
                    }
                    serial_print_line(msg_net_tls_server_hello);
                }
                sha256_update(&net_tls_transcript, rec + 5u, rec_len);
                net_tls_derive_handshake_keys(server_pub);
            } else if (rec_len >= 8u && rec[5u] == 0x02u) {
                serial_print("LeonOS net TLS ServerHello rejected len ");
                serial_print_dec(rec_len);
                serial_print(" body ");
                serial_print_dec(((u32) rec[6u] << 16) |
                                 ((u32) rec[7u] << 8) |
                                 (u32) rec[8u]);
                serial_print("\r\n");
            }
        } else if (rec_type == 0x17u && rec_len <= NET_TLS_PLAIN_MAX + 16u) {
            u8 *plain = net_tls_plain_buf;
            u8 inner_type = 0u;
            u32 plain_len = rec_len - 16u;
            mem_copy(plain, rec + 5u, rec_len);
            if (!net_tls_server_finished_seen) {
                if (net_tls_handshake_keys_ready &&
                    chacha20_poly1305_decrypt(net_tls_server_hs_key,
                        net_tls_server_hs_iv, net_tls_server_hs_seq, rec,
                        plain, rec_len)) {
                    net_tls_server_hs_seq += 1u;
                    u32 content_len = net_tls_inner_content_len(plain, plain_len,
                                                                &inner_type);
                    if (inner_type == 0x16u) {
                        net_tls_handle_handshake_plain(plain, content_len);
                    }
                } else {
                    net_set_last("TLS DECRYPT BAD");
                    serial_print_line(msg_net_tls_decrypt_bad);
                }
            } else {
                if (chacha20_poly1305_decrypt(net_tls_server_app_key,
                        net_tls_server_app_iv, net_tls_server_app_seq, rec,
                        plain, rec_len)) {
                    net_tls_server_app_seq += 1u;
                    u32 content_len = net_tls_inner_content_len(plain, plain_len,
                                                                &inner_type);
                    if (inner_type == 0x17u) {
                        net_tls_parse_https_plain(plain, content_len);
                    }
                } else {
                    net_set_last("TLS APP BAD");
                    serial_print_line(msg_net_tls_decrypt_bad);
                }
            }
        }

        u32 consumed = rec_len + 5u;
        net_tls_rx_len -= consumed;
        if (net_tls_rx_len != 0u) {
            mem_copy(net_tls_rx_buf, net_tls_rx_buf + consumed, net_tls_rx_len);
        }
    }
}

static void net_send_icmp_echo_reply(const u8 dst_mac[6], const u8 dst_ip[4],
                                     const u8 *icmp_in, u32 icmp_len)
{
    if (icmp_len > NET_MTU - 20u) {
        return;
    }
    u8 payload[NET_MTU];
    u8 *ip = payload;
    u8 *icmp = payload + 20u;
    mem_zero(payload, 20u + icmp_len);
    net_write_ipv4_header(ip, (u16) (20u + icmp_len), IP_PROTO_ICMP, dst_ip);
    mem_copy(icmp, icmp_in, icmp_len);
    icmp[0] = 0u;
    icmp[1] = 0u;
    write_be16(icmp + 2u, 0);
    write_be16(icmp + 2u, net_checksum(icmp, icmp_len));
    if (net_send_frame(dst_mac, ETH_TYPE_IPV4, payload, 20u + icmp_len)) {
        net_icmp_tx += 1u;
        net_set_last("ICMP REPLY SENT");
        serial_print("LeonOS net ICMP echo reply sent\r\n");
    }
}

static u32 dns_skip_name(const u8 *dns, u32 len, u32 offset)
{
    while (offset < len) {
        u8 label = dns[offset];
        if (label == 0u) {
            return offset + 1u;
        }
        if ((label & 0xC0u) == 0xC0u) {
            return (offset + 1u < len) ? offset + 2u : len + 1u;
        }
        if ((label & 0xC0u) != 0u) {
            return len + 1u;
        }
        offset += 1u + label;
    }
    return len + 1u;
}

static void net_handle_dns_payload(const u8 *dns, u32 len)
{
    if (len < 12u || (dns[2] & 0x80u) == 0u) {
        return;
    }
    if (read_be16(dns + 0u) != net_dns_txid) {
        if (net_dns_rx_diag_count < 4u) {
            net_dns_rx_diag_count += 1u;
            serial_print("LeonOS net DNS rx ignored txid ");
            serial_print_hex(read_be16(dns + 0u));
            serial_print(" want ");
            serial_print_hex(net_dns_txid);
            serial_print("\r\n");
        }
        return;
    }

    u32 qdcount = read_be16(dns + 4u);
    u32 ancount = read_be16(dns + 6u);
    u32 offset = 12u;
    for (u32 q = 0; q < qdcount; q += 1u) {
        offset = dns_skip_name(dns, len, offset);
        if (offset + 4u > len) {
            return;
        }
        offset += 4u;
    }

    for (u32 answer = 0; answer < ancount; answer += 1u) {
        offset = dns_skip_name(dns, len, offset);
        if (offset + 10u > len) {
            return;
        }
        u16 type = read_be16(dns + offset);
        u16 class_code = read_be16(dns + offset + 2u);
        u16 rdlen = read_be16(dns + offset + 8u);
        offset += 10u;
        if (offset + rdlen > len) {
            return;
        }
        if (type == 1u && class_code == 1u && rdlen == 4u) {
            if (net_dns_a_count < NET_DNS_A_MAX) {
                mem_copy(net_dns_a_records[net_dns_a_count], dns + offset, 4u);
                net_dns_a_count += 1u;
            }
            if (!net_dns_reply_seen) {
                mem_copy(net_dns_a_record, dns + offset, 4u);
                net_dns_a_index = 0u;
                net_dns_reply_seen = 1u;
                net_set_last("DNS A RX");
                serial_print(msg_net_dns_reply_prefix);
                serial_print(net_browser_host);
                serial_print(" ");
                serial_print_ipv4(net_dns_a_record);
                serial_write('\r');
                serial_write('\n');
            } else if (net_dns_a_count <= NET_DNS_A_MAX) {
                serial_print("LeonOS net DNS A alt ");
                serial_print(net_browser_host);
                serial_print(" ");
                serial_print_ipv4(dns + offset);
                serial_write('\r');
                serial_write('\n');
            }
        }
        offset += rdlen;
    }
}

static void net_tls_accept_tcp_data(const u8 *data, u32 data_len)
{
    if (net_tls_rx_len + data_len <= NET_TLS_RX_BUF_MAX) {
        mem_copy(net_tls_rx_buf + net_tls_rx_len, data, data_len);
        net_tls_rx_len += data_len;
        net_tls_process_records();
    } else {
        net_tls_rx_len = 0u;
        net_tls_clear_out_of_order();
        net_set_last("TLS RX FULL");
    }
}

static void net_tls_store_out_of_order(u32 seq, const u8 *data, u32 data_len)
{
    if (seq <= net_tls_ack || data_len > NET_MTU) {
        return;
    }
    u8 slot = 0xFFu;
    u8 far_slot = 0u;
    u32 far_seq = 0u;
    for (u32 i = 0u; i < NET_TLS_OOO_MAX; i += 1u) {
        if (net_tls_ooo_valid[i]) {
            if (net_tls_ooo_seq[i] == seq) {
                if (net_tls_ooo_len[i] >= data_len) {
                    return;
                }
                slot = (u8) i;
                break;
            }
            if (net_tls_ooo_seq[i] >= far_seq) {
                far_seq = net_tls_ooo_seq[i];
                far_slot = (u8) i;
            }
        } else if (slot == 0xFFu) {
            slot = (u8) i;
        }
    }
    if (slot == 0xFFu) {
        if (seq >= far_seq) {
            return;
        }
        slot = far_slot;
    }
    mem_copy(net_tls_ooo_buf[slot], data, data_len);
    net_tls_ooo_seq[slot] = seq;
    net_tls_ooo_len[slot] = data_len;
    net_tls_ooo_valid[slot] = 1u;
    net_tls_ooo_idle_wait_count = 0u;
    net_tls_ooo_reack_tick = 0u;
    net_tls_ooo_reack_count = 0u;
    if (net_tls_ooo_log_count < 32u) {
        net_tls_ooo_log_count += 1u;
        serial_print("LeonOS net TCP 443 out-of-order seq ");
        serial_print_hex(seq);
        serial_print(" want ");
        serial_print_hex(net_tls_ack);
        serial_print(" len ");
        serial_print_dec(data_len);
        serial_print("\r\n");
    }
}

static void net_tls_finish_pending_fin_if_ready(void)
{
    if (!net_tls_fin_pending ||
        net_tls_ack != net_tls_fin_seq ||
        net_tls_out_of_order_pending()) {
        return;
    }
    net_tls_fin_pending = 0u;
    net_tls_ack += 1u;
    net_send_tls_ack();
    if (net_tls_fetch_kind == 0u) {
        net_tls_primary_done = 1u;
        net_tls_primary_done_tick = system_ticks;
        if (!net_https_body_complete) {
            net_https_body_complete = 1u;
        }
        net_browser_finalize_primary_response("fin");
    } else {
        net_browser_finish_resource_fetch();
    }
}

static void net_tls_drain_out_of_order(void)
{
    while (1) {
        u8 slot = 0xFFu;
        u32 slot_seq = 0xFFFFFFFFu;
        for (u32 i = 0u; i < NET_TLS_OOO_MAX; i += 1u) {
            if (!net_tls_ooo_valid[i]) {
                continue;
            }
            u32 seq = net_tls_ooo_seq[i];
            u32 len = net_tls_ooo_len[i];
            u32 end = seq + len;
            if (end < seq || end <= net_tls_ack) {
                net_tls_ooo_valid[i] = 0u;
                net_tls_ooo_len[i] = 0u;
                continue;
            }
            if (seq <= net_tls_ack && seq < slot_seq) {
                slot = (u8) i;
                slot_seq = seq;
            }
        }
        if (slot == 0xFFu) {
            break;
        }
        u32 overlap = net_tls_ack - net_tls_ooo_seq[slot];
        u32 data_len = net_tls_ooo_len[slot] - overlap;
        const u8 *data = net_tls_ooo_buf[slot] + overlap;
        net_tls_ooo_valid[slot] = 0u;
        net_tls_ooo_len[slot] = 0u;
        net_tls_ooo_idle_wait_count = 0u;
        net_tls_ack += data_len;
        net_send_tls_ack();
        net_tls_accept_tcp_data(data, data_len);
    }
    net_tls_finish_pending_fin_if_ready();
}

static void net_handle_tls_tcp(const u8 *src_ip, const u8 *dst_ip,
                               const u8 *payload, u32 len)
{
    if (len < 20u) {
        return;
    }
    u32 header_len = (u32) (payload[12] >> 4) * 4u;
    if (header_len < 20u || header_len > len) {
        return;
    }
    if (net_transport_checksum(src_ip, dst_ip, IP_PROTO_TCP, payload, len) != 0u) {
        return;
    }

    net_tcp_rx += 1u;
    u32 seq = read_be32(payload + 4u);
    u32 ack = read_be32(payload + 8u);
    u8 flags = payload[13];
    u32 data_len = len - header_len;
    const u8 *data = payload + header_len;
    u32 segment_seq = seq;
    u32 segment_data_len = data_len;

    if ((flags & TCP_FLAG_RST) != 0u) {
        net_set_last("TLS TCP RST");
        return;
    }

    if (!net_tls_connected &&
        (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
        ack == net_tls_next_seq) {
        net_tls_connected = 1u;
        net_tls_ack = seq + 1u;
        if (net_browser_resource_fetch_started) {
            net_browser_set_resource_phase("OPEN");
        }
        net_set_last("TLS TCP CONNECTED");
        serial_print_line(msg_net_tls_connected);
        net_send_tls_ack();
        return;
    }

    if (!net_tls_connected) {
        return;
    }

    if (data_len != 0u) {
        if (seq < net_tls_ack) {
            u32 overlap = net_tls_ack - seq;
            if (overlap >= data_len) {
                net_send_tls_ack();
                return;
            }
            data += overlap;
            data_len -= overlap;
            seq = net_tls_ack;
        }
        if (seq != net_tls_ack) {
            net_tls_store_out_of_order(seq, data, data_len);
            if ((flags & TCP_FLAG_FIN) != 0u) {
                net_tls_fin_pending = 1u;
                net_tls_fin_seq = segment_seq + segment_data_len;
            }
            for (u8 dup = 0u; dup < 4u; dup += 1u) {
                net_send_tls_ack();
            }
            return;
        }
        net_tls_ack += data_len;
        net_send_tls_ack();
        net_tls_accept_tcp_data(data, data_len);
        net_tls_drain_out_of_order();
    }

    if ((flags & TCP_FLAG_FIN) != 0u) {
        u32 fin_seq = segment_seq + segment_data_len;
        if (fin_seq == net_tls_ack && !net_tls_out_of_order_pending()) {
            net_tls_ack += 1u;
            net_send_tls_ack();
            if (net_tls_fetch_kind == 0u) {
                net_tls_primary_done = 1u;
                net_tls_primary_done_tick = system_ticks;
                if (!net_https_body_complete) {
                    net_https_body_complete = 1u;
                }
                net_browser_finalize_primary_response("fin");
            } else {
                net_browser_finish_resource_fetch();
            }
        } else {
            net_tls_fin_pending = 1u;
            net_tls_fin_seq = fin_seq;
            net_send_tls_ack();
        }
    }
}

static void net_handle_tcp(const u8 *src_ip, const u8 *dst_ip,
                           const u8 *payload, u32 len)
{
    if (len < 20u) {
        return;
    }
    u16 src_port = read_be16(payload + 0u);
    u16 dst_port = read_be16(payload + 2u);
    u16 tls_port = net_tls_source_port != 0u ? net_tls_source_port
                                             : NET_TLS_SOURCE_PORT;
    if (src_port == 443u &&
        dst_port == tls_port &&
        mem_equal(src_ip, net_dns_a_record, 4u)) {
        net_handle_tls_tcp(src_ip, dst_ip, payload, len);
        return;
    }
    if (src_port != 80u ||
        dst_port != NET_TCP_SOURCE_PORT ||
        !mem_equal(src_ip, net_dns_a_record, 4u)) {
        return;
    }
    u32 header_len = (u32) (payload[12] >> 4) * 4u;
    if (header_len < 20u || header_len > len) {
        return;
    }
    if (net_transport_checksum(src_ip, dst_ip, IP_PROTO_TCP, payload, len) != 0u) {
        return;
    }

    net_tcp_rx += 1u;
    u32 seq = read_be32(payload + 4u);
    u32 ack = read_be32(payload + 8u);
    u8 flags = payload[13];
    u32 data_len = len - header_len;
    const u8 *data = payload + header_len;

    if ((flags & TCP_FLAG_RST) != 0u) {
        net_set_last("TCP RST RX");
        return;
    }

    if (!net_tcp_connected &&
        (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
        ack == net_tcp_next_seq) {
        net_tcp_connected = 1u;
        net_tcp_ack = seq + 1u;
        if (net_browser_resource_fetch_started) {
            net_browser_set_resource_phase("OPEN");
        }
        net_set_last("TCP CONNECTED");
        serial_print_line(msg_net_tcp_connected);
        net_send_tcp_ack();
        return;
    }

    if (!net_tcp_connected) {
        return;
    }

    if (data_len != 0u) {
        if (seq == net_tcp_ack) {
            net_tcp_ack += data_len;
        }
        net_send_tcp_ack();
        if (net_browser_resource_fetch_started) {
            if (net_browser_resource_fetch_bytes + data_len > 65535u) {
                net_browser_resource_fetch_bytes = 65535u;
            } else {
                net_browser_resource_fetch_bytes += (u16) data_len;
            }
        }
        if (!net_http_response_seen) {
            net_http_response_seen = 1u;
            if (data_len >= 12u &&
                data[0] == 'H' && data[1] == 'T' &&
                data[2] == 'T' && data[3] == 'P') {
                if (net_browser_resource_fetch_started) {
                    net_browser_resource_fetch_status[0] = (char) data[9];
                    net_browser_resource_fetch_status[1] = (char) data[10];
                    net_browser_resource_fetch_status[2] = (char) data[11];
                    net_browser_resource_fetch_status[3] = 0;
                } else {
                    net_browser_status[0] = (char) data[9];
                    net_browser_status[1] = (char) data[10];
                    net_browser_status[2] = (char) data[11];
                    net_browser_status[3] = 0;
                }
            } else {
                if (net_browser_resource_fetch_started) {
                    net_browser_resource_fetch_status[0] = '?';
                    net_browser_resource_fetch_status[1] = '?';
                    net_browser_resource_fetch_status[2] = '?';
                    net_browser_resource_fetch_status[3] = 0;
                } else {
                    net_browser_status[0] = '?';
                    net_browser_status[1] = '?';
                    net_browser_status[2] = '?';
                    net_browser_status[3] = 0;
                }
            }
            if (net_browser_resource_fetch_started) {
                net_set_last("RESOURCE HTTP RX");
                serial_print(msg_net_browser_resource_status_prefix);
                serial_print(net_browser_resource_fetch_status);
                serial_print("\r\n");
                net_browser_finish_resource_fetch();
            } else {
                net_set_last("HTTP RESPONSE RX");
                net_browser_location[0] = 0;
                u32 copy_len = 0u;
                while (copy_len < sizeof(net_browser_line) - 1u &&
                       copy_len < data_len &&
                       data[copy_len] != '\r' &&
                       data[copy_len] != '\n') {
                    net_browser_line[copy_len] = (char) data[copy_len];
                    copy_len += 1u;
                }
                net_browser_line[copy_len] = 0;
                for (u32 i = 0u; i + 10u < data_len; i += 1u) {
                    if ((data[i] == 'L' || data[i] == 'l') &&
                        (data[i + 1u] == 'o' || data[i + 1u] == 'O') &&
                        (data[i + 2u] == 'c' || data[i + 2u] == 'C') &&
                        (data[i + 3u] == 'a' || data[i + 3u] == 'A') &&
                        (data[i + 4u] == 't' || data[i + 4u] == 'T') &&
                        (data[i + 5u] == 'i' || data[i + 5u] == 'I') &&
                        (data[i + 6u] == 'o' || data[i + 6u] == 'O') &&
                        (data[i + 7u] == 'n' || data[i + 7u] == 'N') &&
                        data[i + 8u] == ':' &&
                        data[i + 9u] == ' ') {
                        u32 j = i + 10u;
                        u32 out = 0u;
                        while (j < data_len &&
                               out < sizeof(net_browser_location) - 1u &&
                               data[j] != '\r' &&
                               data[j] != '\n') {
                            net_browser_location[out++] = (char) data[j++];
                        }
                        net_browser_location[out] = 0;
                        break;
                    }
                }
                serial_print(msg_net_http_status_prefix);
                if (net_browser_status[0] != '?') {
                    serial_print(net_browser_status);
                } else {
                    serial_print("bytes=");
                    serial_print_dec(data_len);
                }
                serial_write('\r');
                serial_write('\n');
            }
        }
    }

    if ((flags & TCP_FLAG_FIN) != 0u) {
        u32 fin_ack = seq + data_len + 1u;
        if (fin_ack > net_tcp_ack) {
            net_tcp_ack = fin_ack;
        }
        net_send_tcp_ack();
        if (net_browser_resource_fetch_started) {
            net_browser_finish_resource_fetch();
        }
    }
}

static void net_handle_udp(const u8 *src_ip, const u8 *payload, u32 len)
{
    if (len < 8u) {
        return;
    }
    u16 src_port = read_be16(payload + 0u);
    u16 dst_port = read_be16(payload + 2u);
    u16 udp_len = read_be16(payload + 4u);
    if (udp_len < 8u || udp_len > len) {
        return;
    }
    net_udp_rx += 1u;
    if (src_port == 53u && mem_equal(src_ip, net_dns_ip, 4u)) {
        if (dst_port == net_dns_source_port) {
            net_handle_dns_payload(payload + 8u, (u32) udp_len - 8u);
        } else if (net_dns_rx_diag_count < 4u) {
            net_dns_rx_diag_count += 1u;
            serial_print("LeonOS net DNS rx ignored port ");
            serial_print_dec(dst_port);
            serial_print(" want ");
            serial_print_dec(net_dns_source_port);
            serial_print("\r\n");
        }
    }
}

static void net_handle_arp(const u8 *frame, const u8 *payload, u32 len)
{
    if (len < 28u ||
        read_be16(payload + 0u) != 1u ||
        read_be16(payload + 2u) != ETH_TYPE_IPV4 ||
        payload[4] != 6u ||
        payload[5] != 4u) {
        return;
    }
    net_arp_frames += 1u;
    const u8 *sender_mac = payload + 8u;
    const u8 *sender_ip = payload + 14u;
    const u8 *target_ip = payload + 24u;
    u16 op = read_be16(payload + 6u);

    if (op == 2u && mem_equal(sender_ip, net_gateway_ip, 4u)) {
        mem_copy(net_gateway_mac, sender_mac, 6u);
        if (!net_gateway_mac_valid) {
            net_gateway_mac_valid = 1u;
            net_set_last("ARP GATEWAY OK");
            serial_print(msg_net_arp_resolved_prefix);
            serial_print_mac(net_gateway_mac);
            serial_write('\r');
            serial_write('\n');
        }
    } else if (op == 2u && mem_equal(sender_ip, net_dns_ip, 4u)) {
        mem_copy(net_dns_mac, sender_mac, 6u);
        if (!net_dns_mac_valid) {
            net_dns_mac_valid = 1u;
            net_set_last("ARP DNS OK");
            serial_print(msg_net_dns_resolved_prefix);
            serial_print_mac(net_dns_mac);
            serial_write('\r');
            serial_write('\n');
        }
    } else if (op == 1u && mem_equal(target_ip, net_ip, 4u)) {
        net_send_arp_reply(frame + 6u, sender_ip);
    }
}

static void net_handle_ipv4(const u8 *frame, const u8 *payload, u32 len)
{
    if (len < 20u || (payload[0] >> 4) != 4u) {
        return;
    }
    u32 ihl = (payload[0] & 0x0Fu) * 4u;
    if (ihl < 20u || len < ihl) {
        return;
    }
    u16 total_len = read_be16(payload + 2u);
    if (total_len < ihl || total_len > len) {
        return;
    }
    if (net_checksum(payload, ihl) != 0u) {
        return;
    }
    const u8 *src_ip = payload + 12u;
    const u8 *dst_ip = payload + 16u;
    if (!mem_equal(dst_ip, net_ip, 4u)) {
        return;
    }
    net_ipv4_frames += 1u;
    if (payload[9] == IP_PROTO_ICMP) {
        const u8 *icmp = payload + ihl;
        u32 icmp_len = total_len - ihl;
        if (icmp_len < 8u || net_checksum(icmp, icmp_len) != 0u) {
            return;
        }
        net_icmp_rx += 1u;
        if (icmp[0] == 8u) {
            net_send_icmp_echo_reply(frame + 6u, src_ip, icmp, icmp_len);
        } else if (icmp[0] == 0u &&
                   mem_equal(src_ip, net_gateway_ip, 4u) &&
                   !net_icmp_reply_seen) {
            net_icmp_reply_seen = 1u;
            net_set_last("ICMP REPLY RX");
            serial_print_line(msg_net_icmp_reply);
        }
    } else if (payload[9] == IP_PROTO_UDP) {
        net_handle_udp(src_ip, payload + ihl, total_len - ihl);
    } else if (payload[9] == IP_PROTO_TCP) {
        net_handle_tcp(src_ip, dst_ip, payload + ihl, total_len - ihl);
    }
}

static void net_handle_frame(const u8 *frame, u32 len)
{
    if (len < 14u) {
        return;
    }
    net_rx_frames += 1u;
    u16 ethertype = read_be16(frame + 12u);
    if (ethertype == ETH_TYPE_ARP) {
        net_handle_arp(frame, frame + 14u, len - 14u);
    } else if (ethertype == ETH_TYPE_IPV4) {
        net_handle_ipv4(frame, frame + 14u, len - 14u);
    }
}

static u8 rtl8139_tx(const u8 *frame, u32 len)
{
    if (!net_ready || !rtl8139_running || len > NET_FRAME_MAX) {
        return 0u;
    }
    u8 slot = rtl8139_tx_slot;
    u8 found = 0u;
    for (u8 tries = 0u; tries < 4u; tries += 1u) {
        u8 candidate = (u8) ((rtl8139_tx_slot + tries) & 3u);
        u32 tsd = inl((u16) (rtl8139_io_base + RTL_REG_TSD0 + candidate * 4u));
        if (!rtl8139_tx_used[candidate] ||
            (tsd & (1u << 13)) != 0u ||
            (tsd & (1u << 15)) != 0u) {
            slot = candidate;
            found = 1u;
            break;
        }
    }
    if (!found) {
        return 0u;
    }
    u8 *buf = rtl8139_tx_buffers[slot];
    u32 send_len = len < 60u ? 60u : len;
    if (!buf || send_len > RTL8139_TX_BUF_SIZE) {
        return 0u;
    }
    mem_zero(buf, send_len);
    for (u32 i = 0; i < len; i += 1u) {
        buf[i] = frame[i];
    }
    __asm__ volatile ("" : : : "memory");
    outl((u16) (rtl8139_io_base + RTL_REG_TSAD0 + slot * 4u), (u32) buf);
    outl((u16) (rtl8139_io_base + RTL_REG_TSD0 + slot * 4u), send_len);
    rtl8139_tx_used[slot] = 1u;
    rtl8139_tx_slot = (u8) ((slot + 1u) & 3u);
    net_tx_frames += 1u;
    return 1u;
}

static u8 rtl8139_rx_read8(u16 offset)
{
    return rtl8139_rx_buffer[offset % RTL8139_RX_BUF_SIZE];
}

static u16 rtl8139_rx_read16(u16 offset)
{
    return (u16) rtl8139_rx_read8(offset) |
           ((u16) rtl8139_rx_read8((u16) (offset + 1u)) << 8);
}

static void rtl8139_poll_rx(u32 budget)
{
    if (!net_ready || !rtl8139_running || !rtl8139_rx_enabled) {
        return;
    }
    while (budget > 0u &&
           (inb((u16) (rtl8139_io_base + RTL_REG_CR)) & 0x01u) == 0u) {
        u16 offset = rtl8139_rx_cur;
        u16 status = rtl8139_rx_read16(offset);
        u16 length = rtl8139_rx_read16((u16) (offset + 2u));
        u32 frame_len = 0u;
        if ((status & 0x0001u) != 0u && length >= 4u && length <= NET_FRAME_MAX + 4u) {
            frame_len = (u32) length - 4u;
            for (u32 i = 0; i < frame_len; i += 1u) {
                net_rx_frame[i] = rtl8139_rx_read8((u16) (offset + 4u + i));
            }
        }
        u32 next = ((u32) rtl8139_rx_cur + (u32) length + 4u + 3u) & ~3u;
        rtl8139_rx_cur = (u16) (next % RTL8139_RX_BUF_SIZE);
        outw((u16) (rtl8139_io_base + RTL_REG_CAPR), (u16) (rtl8139_rx_cur - 16u));
        outw((u16) (rtl8139_io_base + RTL_REG_ISR), 0x0005u);
        budget -= 1u;
        if (frame_len != 0u) {
            net_handle_frame(net_rx_frame, frame_len);
        }
    }
}

static u8 rtl8139_init_from_pci(u8 bus, u8 slot, u8 func)
{
    u32 bar0 = pci_read32(bus, slot, func, 0x10u);
    if ((bar0 & 1u) == 0u) {
        serial_print("LeonOS net RTL8139 BAR0 not I/O\r\n");
        return 0u;
    }
    if ((bar0 & 0xFFFCu) == 0u) {
        pci_write32(bus, slot, func, 0x10u, RTL8139_FALLBACK_IO | 1u);
        bar0 = pci_read32(bus, slot, func, 0x10u);
    }
    if ((bar0 & 1u) == 0u || (bar0 & 0xFFFCu) == 0u) {
        serial_print("LeonOS net RTL8139 BAR0 unassigned\r\n");
        return 0u;
    }
    rtl8139_io_base = (u16) (bar0 & 0xFFFCu);
    rtl8139_irq = (u8) (pci_read32(bus, slot, func, 0x3Cu) & 0xFFu);
    u32 command = pci_read32(bus, slot, func, 0x04u);
    pci_write32(bus, slot, func, 0x04u, command | 0x00000005u);

    serial_print(msg_net_pci_prefix);
    serial_print_hex(rtl8139_io_base);
    serial_print(msg_net_irq_prefix);
    serial_print_dec(rtl8139_irq);
    serial_write('\r');
    serial_write('\n');

    outb((u16) (rtl8139_io_base + RTL_REG_CONFIG1), 0x00u);
    outb((u16) (rtl8139_io_base + RTL_REG_CR), 0x10u);
    for (u32 tries = 0; tries < 100000u; tries += 1u) {
        if ((inb((u16) (rtl8139_io_base + RTL_REG_CR)) & 0x10u) == 0u) {
            break;
        }
    }

    for (u32 i = 0; i < 6u; i += 1u) {
        net_mac[i] = inb((u16) (rtl8139_io_base + RTL_REG_IDR0 + i));
    }

    rtl8139_rx_buffer = (u8 *) pmm_alloc_contiguous_pages(RTL8139_RX_ALLOC_SIZE / PAGE_SIZE);
    mem_zero(rtl8139_rx_buffer, RTL8139_RX_ALLOC_SIZE);
    serial_print(msg_net_rx_prefix);
    serial_print_hex((u32) rtl8139_rx_buffer);
    serial_write('\r');
    serial_write('\n');
    rtl8139_tx_region = (u8 *) pmm_alloc_contiguous_pages(
        (RTL8139_TX_BUF_SIZE * 4u) / PAGE_SIZE);
    mem_zero(rtl8139_tx_region, RTL8139_TX_BUF_SIZE * 4u);
    serial_print(msg_net_tx_prefix);
    serial_print_hex((u32) rtl8139_tx_region);
    serial_write('\r');
    serial_write('\n');
    for (u32 i = 0; i < 4u; i += 1u) {
        rtl8139_tx_buffers[i] = rtl8139_tx_region + i * RTL8139_TX_BUF_SIZE;
        rtl8139_tx_used[i] = 0u;
        outl((u16) (rtl8139_io_base + RTL_REG_TSAD0 + i * 4u),
             (u32) rtl8139_tx_buffers[i]);
    }

    rtl8139_rx_cur = 0;
    rtl8139_tx_slot = 0;
    outl((u16) (rtl8139_io_base + RTL_REG_RBSTART), (u32) rtl8139_rx_buffer);
    outw((u16) (rtl8139_io_base + RTL_REG_IMR), 0x0000u);
    outl((u16) (rtl8139_io_base + RTL_REG_RCR), 0x00001F8Fu);
    outl((u16) (rtl8139_io_base + RTL_REG_TCR), 0x00000700u);

    net_present = 1u;
    net_ready = 1u;
    net_set_last("RTL8139 READY");
    serial_print(msg_net_mac_prefix);
    serial_print_mac(net_mac);
    serial_write('\r');
    serial_write('\n');
    return 1u;
}

static void rtl8139_start(void)
{
    if (!net_ready || rtl8139_running) {
        return;
    }
    /* Keep networking polled and interrupt-free for now. This gives the
     * browser-prep stack a deterministic RX/TX base in QEMU. */
    outw((u16) (rtl8139_io_base + RTL_REG_ISR), 0xFFFFu);
    outw((u16) (rtl8139_io_base + RTL_REG_CAPR), 0xFFF0u);
    outb((u16) (rtl8139_io_base + RTL_REG_CR), 0x0Cu);
    rtl8139_running = 1u;
    rtl8139_rx_enabled = 1u;
    net_set_last("RTL8139 LINK READY");
    serial_print_line(msg_net_link_ready);
}

static void net_init(void)
{
    u8 bus = 0;
    u8 slot = 0;
    u8 func = 0;
    net_set_last("NO NIC");
    if (!pci_find_rtl8139(&bus, &slot, &func)) {
        serial_print("LeonOS net no supported NIC\r\n");
        return;
    }
    if (!rtl8139_init_from_pci(bus, slot, func)) {
        net_set_last("NIC INIT FAILED");
        return;
    }
}

static void task_net(struct KernelTask *task)
{
    (void) task;
    if (!net_ready) {
        return;
    }
    if (!rtl8139_running) {
        rtl8139_start();
        return;
    }
    rtl8139_poll_rx(256u);
    if (net_browser_fetch_enabled &&
        net_tls_connected &&
        net_tls_out_of_order_pending() &&
        (net_tls_ooo_reack_tick == 0u ||
         system_ticks - net_tls_ooo_reack_tick >= 10u)) {
        net_tls_ooo_reack_tick = system_ticks;
        if (net_tls_ooo_reack_count < 128u) {
            net_tls_ooo_reack_count += 1u;
            net_send_tls_ack();
        }
    }
    if (net_browser_resource_fetch_pending &&
        net_tls_primary_done &&
        system_ticks - net_tls_primary_done_tick >= 20u) {
        net_browser_start_resource_fetch();
    }
    if (net_browser_fetch_enabled &&
        net_tls_fetch_kind == 0u &&
        !net_browser_finalize_sent &&
        !net_https_response_seen &&
        net_https_get_sent &&
        net_https_get_tick != 0u &&
        system_ticks - net_https_get_tick >= NET_BROWSER_BODY_IDLE_TICKS) {
        if (net_browser_header_only_retry_count < NET_BROWSER_HEADER_ONLY_RETRY_MAX) {
            net_browser_retry_current_url_after_https_stall("no-response");
        } else {
            net_https_body_complete = 1u;
            net_tls_primary_done = 1u;
            net_tls_primary_done_tick = system_ticks;
            net_browser_finalize_primary_response("no-response");
        }
    }
    if (net_browser_fetch_enabled &&
        net_tls_fetch_kind == 0u &&
        !net_browser_finalize_sent &&
        !net_https_body_complete &&
        net_https_headers_done &&
        net_https_body_decoded_total32 == 0u &&
        net_https_headers_tick != 0u &&
        system_ticks - net_https_headers_tick >= NET_BROWSER_BODY_IDLE_TICKS) {
        if (net_tls_out_of_order_pending() &&
            net_tls_ooo_idle_wait_count < 16u) {
            net_tls_ooo_idle_wait_count += 1u;
            net_send_tls_ack();
            net_https_headers_tick = system_ticks;
        } else if (net_user_fetch_active &&
                   net_user_fetch_content_length_seen &&
                   net_user_fetch_len < net_user_fetch_content_length) {
            net_send_tls_ack();
            net_https_headers_tick = system_ticks;
        } else if (net_user_fetch_active &&
                   net_user_fetch_allows_empty_body()) {
            net_https_body_complete = 1u;
            net_tls_primary_done = 1u;
            net_tls_primary_done_tick = system_ticks;
            net_browser_finalize_primary_response("empty");
        } else if (net_browser_header_only_retry_count < NET_BROWSER_HEADER_ONLY_RETRY_MAX) {
            net_browser_retry_current_url_after_https_stall("header-only");
        } else {
            net_https_body_complete = 1u;
            net_tls_primary_done = 1u;
            net_tls_primary_done_tick = system_ticks;
            net_browser_finalize_primary_response("headers-only");
        }
    }
    if (net_browser_fetch_enabled &&
        net_tls_fetch_kind == 0u &&
        !net_browser_finalize_sent &&
        !net_https_body_complete &&
        net_https_body_decoded_total32 != 0u &&
        net_https_body_last_tick != 0u &&
        system_ticks - net_https_body_last_tick >= NET_BROWSER_BODY_IDLE_TICKS) {
        if (net_tls_out_of_order_pending() &&
            net_tls_ooo_idle_wait_count < 16u) {
            net_tls_ooo_idle_wait_count += 1u;
            net_send_tls_ack();
            net_https_body_last_tick = system_ticks;
        } else if (net_user_fetch_active &&
                   net_user_fetch_content_length_seen &&
                   net_user_fetch_len < net_user_fetch_content_length) {
            net_send_tls_ack();
            net_https_body_last_tick = system_ticks;
        } else {
            net_https_body_complete = 1u;
            net_tls_primary_done = 1u;
            net_tls_primary_done_tick = system_ticks;
            net_browser_finalize_primary_response("idle");
        }
    }
    /* Browser prep is deliberately tiny: ARP, ICMP, DNS, then one direct
     * TLS/HTTPS probe. It is not a socket API or a browser engine yet. */
    if (NET_ENABLE_ARP_PROBE && !net_arp_requested && system_ticks >= 12u) {
        net_send_arp_request();
    }
    if (net_gateway_mac_valid && !net_icmp_probe_sent) {
        net_send_icmp_echo_request();
    }
    if (net_icmp_reply_seen && !net_dns_arp_requested) {
        net_send_dns_arp_request();
    }
    if (!net_browser_fetch_enabled) {
        return;
    }
    if (net_dns_query_sent && !net_dns_reply_seen &&
        net_dns_retry_count < 3u &&
        system_ticks - net_dns_query_tick > 200u) {
        net_dns_query_sent = 0u;
        net_dns_retry_count += 1u;
        net_dns_source_port += 0x10u;
        if (net_dns_source_port < NET_DNS_SOURCE_PORT ||
            net_dns_source_port > NET_DNS_SOURCE_PORT + 512u) {
            net_dns_source_port = NET_DNS_SOURCE_PORT + 0x20u;
        }
        net_dns_txid += 1u;
        if (net_dns_txid == 0u) {
            net_dns_txid = NET_DNS_TXID + 2u;
        }
        serial_print("LeonOS net DNS retry ");
        serial_print(net_browser_host);
        serial_print("\r\n");
    }
    if (net_dns_mac_valid && !net_dns_query_sent) {
        net_send_dns_query();
    }
    if (net_dns_query_sent && !net_dns_reply_seen &&
        net_dns_retry_count >= 3u &&
        net_dns_query_tick != 0u &&
        system_ticks - net_dns_query_tick > 500u) {
        net_browser_set_error_page("DNS ERROR",
            "The host did not resolve after real DNS retries.",
            net_browser_current_url);
        net_set_last("BROWSER DNS ERROR");
        return;
    }
    if (net_dns_reply_seen &&
        net_tls_syn_sent &&
        !net_tls_connected &&
        net_tls_syn_retry_count < NET_TLS_SYN_RETRY_MAX &&
        net_tls_syn_tick != 0u &&
        system_ticks - net_tls_syn_tick > NET_TLS_SYN_RETRY_TICKS) {
        net_tls_syn_retry_count += 1u;
        serial_print("LeonOS net TCP 443 SYN retry ");
        serial_print_dec(net_tls_syn_retry_count);
        serial_print("\r\n");
        net_send_tls_syn();
    }
    if (net_dns_reply_seen &&
        net_tls_syn_sent &&
        !net_tls_connected &&
        net_tls_syn_retry_count >= NET_TLS_SYN_RETRY_MAX &&
        net_tls_syn_tick != 0u &&
        system_ticks - net_tls_syn_tick > NET_TLS_SYN_RETRY_TICKS &&
        net_dns_a_count > 1u) {
        net_dns_a_index += 1u;
        if (net_dns_a_index >= net_dns_a_count) {
            net_dns_a_index = 0u;
        }
        mem_copy(net_dns_a_record, net_dns_a_records[net_dns_a_index], 4u);
        net_tls_syn_sent = 0u;
        net_tls_syn_retry_count = 0u;
        net_tls_syn_tick = 0u;
        serial_print("LeonOS net TCP 443 SYN alternate ");
        serial_print_ipv4(net_dns_a_record);
        serial_print("\r\n");
    }
    if (net_dns_reply_seen && !net_tls_syn_sent) {
        net_send_tls_syn();
    }
    net_browser_timeout_resource_fetch();
    if (net_tls_connected && !net_tls_client_hello_sent) {
        net_send_tls_client_hello();
    }
    rtl8139_poll_rx(256u);
}

static void net_user_fetch_drive(void)
{
    struct KernelTask task;
    if (!net_ready || !net_user_fetch_active) {
        return;
    }
    if (!rtl8139_running) {
        rtl8139_start();
    }
    rtl8139_poll_rx(512u);
    task.name = "user-fetch";
    task.entry = task_net;
    task.run_count = 0u;
    task.data0 = 0u;
    task.data1 = 0u;
    task.active = 1u;
    task_net(&task);
}

static u8 user_copy_https_url(u32 url_ptr, char *url_copy, u32 url_copy_max)
{
    if (!user_range_valid(url_ptr, 1u) || url_copy_max == 0u) {
        return 0u;
    }
    const char *url = (const char *) url_ptr;
    if (!user_url_starts_https(url)) {
        return 0u;
    }
    u32 url_max = user_range_max(url_ptr);
    u32 url_len = 0u;
    while (url_len < url_max && url[url_len] != 0) {
        url_len += 1u;
    }
    if (url_len < 12u || url_len >= url_copy_max) {
        return 0u;
    }
    for (u32 i = 0u; i < url_len; i += 1u) {
        url_copy[i] = url[i];
    }
    url_copy[url_len] = 0;
    return 1u;
}

static u8 user_copy_method_token(u32 method_ptr, char *dst, u32 dst_len)
{
    if (dst_len < 4u) {
        return 0u;
    }
    if (method_ptr == 0u) {
        dst[0] = 'G';
        dst[1] = 'E';
        dst[2] = 'T';
        dst[3] = 0;
        return 1u;
    }
    if (!user_range_valid(method_ptr, 1u)) {
        return 0u;
    }
    const char *src = (const char *) method_ptr;
    u32 max = user_range_max(method_ptr);
    u32 i = 0u;
    while (i + 1u < dst_len && i < max && src[i] != 0) {
        char ch = src[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char) (ch - ('a' - 'A'));
        }
        if (ch < 'A' || ch > 'Z') {
            return 0u;
        }
        dst[i++] = ch;
    }
    if (i == 0u) {
        dst[0] = 'G';
        dst[1] = 'E';
        dst[2] = 'T';
        dst[3] = 0;
        return 1u;
    }
    if (i >= max || (i + 1u >= dst_len && src[i] != 0)) {
        return 0u;
    }
    dst[i] = 0;
    return 1u;
}

static u8 user_copy_header_value(u32 value_ptr, char *dst, u32 dst_len)
{
    if (dst_len == 0u) {
        return 0u;
    }
    dst[0] = 0;
    if (value_ptr == 0u) {
        return 1u;
    }
    if (!user_range_valid(value_ptr, 1u)) {
        return 0u;
    }
    const char *src = (const char *) value_ptr;
    u32 max = user_range_max(value_ptr);
    u32 i = 0u;
    while (i + 1u < dst_len && i < max && src[i] != 0) {
        char ch = src[i];
        if (ch == '\r' || ch == '\n') {
            return 0u;
        }
        if ((u8) ch < 32u || (u8) ch > 126u) {
            ch = ' ';
        }
        dst[i++] = ch;
    }
    dst[i] = 0;
    return 1u;
}

static u32 user_syscall_net_fetch_wait_copy(u32 buf_ptr, u32 max_len)
{
    u32 deadline = system_ticks + USER_NET_FETCH_TICK_LIMIT;
    while (!net_user_fetch_done && system_ticks < deadline) {
        net_user_fetch_drive();
    }

    if (!net_user_fetch_done) {
        net_tls_abort_current_for_navigation();
        net_user_fetch_finish(0u);
    }

    if (!net_user_fetch_ok || net_user_fetch_len == 0u) {
        return 0u;
    }

    u32 copy = net_user_fetch_len;
    if (copy > max_len) {
        copy = max_len;
    }
    if (!user_range_valid(buf_ptr, copy)) {
        serial_print("LeonOS user net fetch copy bad range bytes ");
        serial_print_dec(copy);
        serial_print("\r\n");
        return 0u;
    }
    serial_print("LeonOS user net fetch copy begin bytes ");
    serial_print_dec(copy);
    serial_print("\r\n");
    u8 *dst = (u8 *) buf_ptr;
    for (u32 i = 0u; i < copy; i += 1u) {
        dst[i] = net_user_fetch_buf[i];
    }
    serial_print("LeonOS user net fetch copy return bytes ");
    serial_print_dec(copy);
    serial_print("\r\n");
    return copy;
}

static u32 net_user_stream_state(void)
{
    u32 state = 0u;
    if (!net_user_stream_open) {
        return state;
    }
    state |= USER_NET_STREAM_STATE_OPEN;
    if (net_user_fetch_active && !net_user_fetch_done) {
        state |= USER_NET_STREAM_STATE_ACTIVE;
    }
    if (net_user_fetch_done) {
        state |= USER_NET_STREAM_STATE_DONE;
        state |= net_user_fetch_ok ? USER_NET_STREAM_STATE_OK
                                   : USER_NET_STREAM_STATE_ERROR;
    }
    if (net_user_stream_read_pos < net_user_fetch_len) {
        state |= USER_NET_STREAM_STATE_HAS_DATA;
    }
    return state;
}

static void net_user_stream_timeout_if_needed(void)
{
    if (!net_user_stream_open || !net_user_fetch_active ||
        net_user_fetch_done) {
        return;
    }
    if (system_ticks - net_user_stream_start_tick > USER_NET_FETCH_TICK_LIMIT) {
        serial_print("LeonOS user net stream timeout\r\n");
        net_tls_abort_current_for_navigation();
        net_user_fetch_finish(0u);
    }
}

static u32 user_syscall_net_fetch(u32 url_ptr, u32 buf_ptr, u32 max_len)
{
    if (!user_range_valid(url_ptr, 1u) ||
        !user_range_valid(buf_ptr, 1u) ||
        max_len == 0u) {
        serial_print("LeonOS user net fetch bad args\r\n");
        return 0u;
    }
    if (net_user_stream_open) {
        serial_print("LeonOS user net fetch busy stream\r\n");
        return 0u;
    }
    if (!user_copy_https_url(url_ptr, user_url_copy, USER_BROWSER_URL_MAX)) {
        serial_print("LeonOS user net fetch bad url\r\n");
        return 0u;
    }

    serial_print("LeonOS user net fetch begin ");
    serial_print(user_url_copy);
    serial_print("\r\n");
    net_user_fetch_begin(user_url_copy);

    return user_syscall_net_fetch_wait_copy(buf_ptr, max_len);
}

static u32 user_syscall_net_fetch_ex(u32 request_ptr)
{
    if (!user_range_valid(request_ptr, USER_NET_FETCH_REQUEST_SIZE)) {
        serial_print("LeonOS user net fetch ex bad request\r\n");
        return 0u;
    }
    const u8 *request = (const u8 *) request_ptr;
    u32 url_ptr = read32(request + 0u);
    u32 buf_ptr = read32(request + 4u);
    u32 max_len = read32(request + 8u);
    u32 method_ptr = read32(request + 12u);
    u32 body_ptr = read32(request + 16u);
    u32 body_len = read32(request + 20u);
    u32 content_type_ptr = read32(request + 24u);
    u32 accept_ptr = read32(request + 28u);
    char method[USER_NET_REQUEST_METHOD_MAX];
    char content_type[USER_NET_REQUEST_HEADER_VALUE_MAX];
    char accept[USER_NET_REQUEST_HEADER_VALUE_MAX];

    if (!user_range_valid(buf_ptr, 1u) || max_len == 0u) {
        serial_print("LeonOS user net fetch ex bad buffer\r\n");
        return 0u;
    }
    if (net_user_stream_open) {
        serial_print("LeonOS user net fetch ex busy stream\r\n");
        return 0u;
    }
    if (!user_copy_https_url(url_ptr, user_url_copy, USER_BROWSER_URL_MAX)) {
        serial_print("LeonOS user net fetch ex bad url\r\n");
        return 0u;
    }
    if (!user_copy_method_token(method_ptr, method, sizeof(method)) ||
        !user_copy_header_value(content_type_ptr, content_type,
                                sizeof(content_type)) ||
        !user_copy_header_value(accept_ptr, accept, sizeof(accept))) {
        serial_print("LeonOS user net fetch ex bad request strings\r\n");
        return 0u;
    }
    if (body_len > USER_NET_REQUEST_BODY_MAX) {
        serial_print("LeonOS user net fetch ex body too large bytes ");
        serial_print_dec(body_len);
        serial_print("\r\n");
        return 0u;
    }
    if (body_len != 0u && !user_range_valid(body_ptr, body_len)) {
        serial_print("LeonOS user net fetch ex bad body\r\n");
        return 0u;
    }

    serial_print("LeonOS user net fetch ex begin ");
    serial_print(method);
    serial_write(' ');
    serial_print(user_url_copy);
    serial_print(" body ");
    serial_print_dec(body_len);
    serial_print("\r\n");

    net_user_fetch_begin_request(user_url_copy, method,
                                 body_len != 0u ? (const u8 *) body_ptr : 0,
                                 body_len, content_type, accept);
    return user_syscall_net_fetch_wait_copy(buf_ptr, max_len);
}

static u32 user_syscall_net_fetch_meta(u32 meta_ptr)
{
    if (!user_range_valid(meta_ptr, USER_NET_FETCH_META_SIZE)) {
        serial_print("LeonOS user net fetch meta bad range\r\n");
        return 0u;
    }

    u8 *meta = (u8 *) meta_ptr;
    write32(meta + 0u, net_user_fetch_len);
    write32(meta + 4u, net_user_fetch_status_code);
    write32(meta + 8u, net_user_fetch_flags);
    for (u32 i = 0u; i < USER_NET_FETCH_CONTENT_TYPE_MAX; i += 1u) {
        meta[12u + i] = 0u;
    }
    for (u32 i = 0u; i + 1u < USER_NET_FETCH_CONTENT_TYPE_MAX &&
         net_user_fetch_content_type[i] != 0; i += 1u) {
        meta[12u + i] = (u8) net_user_fetch_content_type[i];
    }
    for (u32 i = 0u; i < USER_NET_FETCH_LOCATION_MAX; i += 1u) {
        meta[76u + i] = 0u;
    }
    for (u32 i = 0u; i + 1u < USER_NET_FETCH_LOCATION_MAX &&
         net_user_fetch_location[i] != 0; i += 1u) {
        meta[76u + i] = (u8) net_user_fetch_location[i];
    }

    serial_print("LeonOS user net fetch meta copy status ");
    serial_print_dec(net_user_fetch_status_code);
    serial_print(" flags ");
    serial_print_dec(net_user_fetch_flags);
    serial_print(" type ");
    serial_print(net_user_fetch_content_type[0] != 0 ?
                 net_user_fetch_content_type : "unknown");
    serial_print("\r\n");
    return 1u;
}

static u32 user_syscall_net_stream_open(u32 url_ptr)
{
    if (net_user_stream_open || net_user_fetch_active) {
        if (net_user_stream_busy_log_count < 4u) {
            net_user_stream_busy_log_count += 1u;
            serial_print("LeonOS user net stream open busy\r\n");
        }
        return 0u;
    }
    if (!net_ready) {
        serial_print("LeonOS user net stream no net\r\n");
        return 0u;
    }
    if (!user_copy_https_url(url_ptr, user_url_copy, USER_BROWSER_URL_MAX)) {
        serial_print("LeonOS user net stream bad url\r\n");
        return 0u;
    }

    serial_print("LeonOS user net stream open ");
    serial_print(user_url_copy);
    serial_print("\r\n");
    net_user_fetch_begin(user_url_copy);
    net_user_stream_open = 1u;
    net_user_stream_read_pos = 0u;
    net_user_stream_start_tick = system_ticks;
    net_user_stream_last_len = 0u;
    net_user_stream_idle_polls = 0u;
    net_user_stream_read_log_count = 0u;
    net_user_stream_busy_log_count = 0u;
    return USER_NET_STREAM_HANDLE;
}

static u32 user_syscall_net_stream_poll(u32 handle)
{
    if (!net_user_stream_open || handle != USER_NET_STREAM_HANDLE) {
        return 0u;
    }
    if (net_user_fetch_active && !net_user_fetch_done) {
        for (u8 i = 0u; i < 32u && net_user_fetch_active &&
             !net_user_fetch_done; i += 1u) {
            net_user_fetch_drive();
        }
        net_user_stream_timeout_if_needed();
        if (net_user_fetch_active && !net_user_fetch_done &&
            net_user_fetch_len != 0u &&
            (net_user_fetch_flags & USER_NET_FETCH_FLAG_HEADERS) != 0u) {
            if (net_user_fetch_len != net_user_stream_last_len) {
                net_user_stream_last_len = net_user_fetch_len;
                net_user_stream_idle_polls = 0u;
            } else if (net_user_stream_idle_polls <
                       USER_NET_STREAM_IDLE_POLL_LIMIT) {
                net_user_stream_idle_polls += 1u;
            } else {
                if ((net_user_fetch_content_length_seen &&
                     net_user_fetch_len < net_user_fetch_content_length) ||
                    net_tls_out_of_order_pending() ||
                    net_tls_fin_pending) {
                    net_user_stream_idle_polls = USER_NET_STREAM_IDLE_POLL_LIMIT;
                    net_send_tls_ack();
                } else {
                    serial_print("LeonOS user net stream idle complete bytes ");
                    serial_print_dec(net_user_fetch_len);
                    serial_print("\r\n");
                    net_https_body_complete = 1u;
                    net_user_fetch_finish(1u);
                }
            }
        }
    }
    return net_user_stream_state();
}

static u32 user_syscall_net_stream_read(u32 handle, u32 buf_ptr, u32 max_len)
{
    if (!net_user_stream_open || handle != USER_NET_STREAM_HANDLE ||
        max_len == 0u || !user_range_valid(buf_ptr, 1u)) {
        return 0u;
    }
    if (net_user_fetch_active && !net_user_fetch_done) {
        net_user_fetch_drive();
        net_user_stream_timeout_if_needed();
    }
    if (net_user_stream_read_pos >= net_user_fetch_len) {
        return 0u;
    }
    u32 copy = net_user_fetch_len - net_user_stream_read_pos;
    if (copy > max_len) {
        copy = max_len;
    }
    if (!user_range_valid(buf_ptr, copy)) {
        serial_print("LeonOS user net stream read bad range bytes ");
        serial_print_dec(copy);
        serial_print("\r\n");
        return 0u;
    }
    u8 *dst = (u8 *) buf_ptr;
    for (u32 i = 0u; i < copy; i += 1u) {
        dst[i] = net_user_fetch_buf[net_user_stream_read_pos + i];
    }
    net_user_stream_read_pos += copy;
    if (net_user_stream_read_log_count < 6u ||
        (net_user_fetch_done &&
         net_user_stream_read_pos >= net_user_fetch_len)) {
        net_user_stream_read_log_count += 1u;
        serial_print("LeonOS user net stream read bytes ");
        serial_print_dec(copy);
        serial_print(" pos ");
        serial_print_dec(net_user_stream_read_pos);
        serial_print(" of ");
        serial_print_dec(net_user_fetch_len);
        serial_print("\r\n");
    }
    return copy;
}

static u32 user_syscall_net_stream_meta(u32 handle, u32 meta_ptr)
{
    if (!net_user_stream_open || handle != USER_NET_STREAM_HANDLE) {
        return 0u;
    }
    return user_syscall_net_fetch_meta(meta_ptr);
}

static u32 user_syscall_net_stream_close(u32 handle)
{
    if (!net_user_stream_open || handle != USER_NET_STREAM_HANDLE) {
        return 0u;
    }
    if (net_user_fetch_active && !net_user_fetch_done) {
        net_tls_abort_current_for_navigation();
    }
    net_user_fetch_active = 0u;
    net_user_fetch_done = 0u;
    net_user_fetch_ok = 0u;
    net_user_fetch_ignore_tail = 1u;
    net_user_fetch_len = 0u;
    net_user_fetch_content_length = 0u;
    net_user_fetch_content_length_seen = 0u;
    net_user_fetch_status_code = 0u;
    net_user_fetch_flags = 0u;
    net_user_fetch_content_type[0] = 0;
    net_user_fetch_location[0] = 0;
    net_browser_fetch_enabled = 0u;
    net_user_stream_open = 0u;
    net_user_stream_read_pos = 0u;
    net_user_stream_start_tick = 0u;
    net_user_stream_last_len = 0u;
    net_user_stream_idle_polls = 0u;
    net_user_stream_read_log_count = 0u;
    net_user_stream_busy_log_count = 0u;
    serial_print("LeonOS user net stream close\r\n");
    return 1u;
}

static const char shell_serial_msgs[][40] LATE_RODATA = {
    "LeonOS shell win restored",
    "LeonOS shell win maximized",
    "LeonOS shell win minimized",
    "LeonOS shell win closed",
    "LeonOS shell tab=0",
    "LeonOS shell tab=1",
    "LeonOS shell start-menu open",
    "LeonOS shell start-menu closed",
    "LeonOS shell win moved",
    "LeonOS shell ready",
    "LeonOS shell app=files",
    "LeonOS shell app=apps",
    "LeonOS shell app=about",
    "LeonOS shell app=log",
    "LeonOS shell app=net",
    "LeonOS shell app=hello",
    "LeonOS shell app=uhello",
    "LeonOS shell app=write",
    "LeonOS shell app=ugfx",
    "LeonOS shell app=ucdemo",
    "LeonOS shell app=ubrowser",
    "LeonOS shell app=unetsurf",
    "LeonOS shell app=uweb",
    "LeonOS shell app=netsurf",
    "LeonOS shell app=ustream"
};

static void shell_serial_emit(enum ShellSerialMsg msg)
{
    const char *text = shell_serial_msgs[(u32) msg];
    for (u32 i = 0; text[i] != 0; i += 1) {
        serial_write(text[i]);
    }
    serial_write('\r');
    serial_write('\n');
}

#include "shell_ui.inc.c"

static u32 user_fb_clip_limit_y(void)
{
    if (user_app_netsurf_running && shellm.taskbar_y > 0u &&
        shellm.taskbar_y < g_boot->framebuffer.height) {
        return shellm.taskbar_y;
    }
    return g_boot->framebuffer.height;
}

static void user_fb_present_overlay(void)
{
    if (!framebuffer_back_ready) {
        return;
    }

    if (user_app_netsurf_running) {
        volatile u32 *old_pixels = draw_pixels_override;
        u32 old_stride = draw_stride_override;
        draw_pixels_override = framebuffer_back;
        draw_stride_override = framebuffer_back_stride;
        shell_draw_taskbar();
        draw_pixels_override = old_pixels;
        draw_stride_override = old_stride;
    }

    framebuffer_present();
    cursor_drawn = 0u;
    draw_cursor_overlay();
}

static void handle_mouse_packet(void)
{
    mouse_buttons = (u8) (mouse_packet0 & 0x07u);
    if ((mouse_packet0 & 0xC0u) != 0) {
        mouse_packet_index = 0;
        return;
    }

    restore_cursor();

    i32 dx = (i32) ((i8) mouse_packet1);
    i32 dy = -((i32) ((i8) mouse_packet2));
    if (dx > 64) {
        dx = 64;
    } else if (dx < -64) {
        dx = -64;
    }
    if (dy > 64) {
        dy = 64;
    } else if (dy < -64) {
        dy = -64;
    }
    mouse_x += dx;
    mouse_y += dy;

    i32 max_x = (i32) g_boot->framebuffer.width - 1;
    i32 max_y = (i32) g_boot->framebuffer.height - 1;
    if (mouse_x < 0) {
        mouse_x = 0;
    } else if (mouse_x > max_x) {
        mouse_x = max_x;
    }
    if (mouse_y < 0) {
        mouse_y = 0;
    } else if (mouse_y > max_y) {
        mouse_y = max_y;
    }

    if (!(user_app_running && user_fb_overlay_active)) {
        shell_mouse_move();
        shell_mouse_click();
        shell_mouse_release();
    }
    event_push(EVENT_MOUSE, (u32) mouse_x, (u32) mouse_y);
    if (mouse_buttons != prev_mouse_buttons) {
        event_push(EVENT_MOUSE_BUTTON, (u32) mouse_x,
                   ((u32) mouse_y & 0xFFFFu) |
                   ((u32) mouse_buttons << 16) |
                   ((u32) prev_mouse_buttons << 24));
        prev_mouse_buttons = mouse_buttons;
    }
    if (!dirty) {
        draw_cursor_overlay();
    }
}

static void handle_mouse_byte(u8 data)
{
    if (mouse_packet_index == 0 && (data & 0x08u) == 0) {
        return;
    }
    if (mouse_packet_index == 0) {
        mouse_packet0 = data;
        mouse_packet_index = 1;
    } else if (mouse_packet_index == 1) {
        mouse_packet1 = data;
        mouse_packet_index = 2;
    } else {
        mouse_packet2 = data;
        mouse_packet_index = 0;
        handle_mouse_packet();
    }
}

static void handle_mouse(void)
{
    for (u8 guard = 0; guard < 16u; guard += 1) {
        u8 status = inb(0x64u);
        if ((status & 0x01u) == 0) {
            break;
        }
        u8 data = inb(0x60u);
        if ((status & 0x20u) == 0) {
            continue;
        }
        handle_mouse_byte(data);
    }
}

static u8 cursor_pixel_kind(u32 col, u32 row)
{
    u8 body = (col <= row / 2u && row < 20u);
    u8 tail = (row >= 13u && row <= 22u && col >= 6u && col <= 9u);
    u8 edge = (col == (row / 2u + 1u) && row < 20u) ||
              (row >= 13u && row <= 22u && (col == 5u || col == 10u));

    if (body || tail) {
        return 2;
    }
    if (edge) {
        return 1;
    }
    return 0;
}

static void restore_cursor(void)
{
    if (!cursor_drawn) {
        return;
    }

    for (u32 row = 0; row < CURSOR_H; row += 1) {
        for (u32 col = 0; col < CURSOR_W; col += 1) {
            write_pixel(cursor_saved_x + (i32) col,
                        cursor_saved_y + (i32) row,
                        cursor_back[row * CURSOR_W + col]);
        }
    }
    cursor_drawn = 0;
}

static void draw_cursor_overlay(void)
{
    if (!mouse_ok) {
        return;
    }

    cursor_saved_x = mouse_x;
    cursor_saved_y = mouse_y;
    for (u32 row = 0; row < CURSOR_H; row += 1) {
        for (u32 col = 0; col < CURSOR_W; col += 1) {
            i32 x = mouse_x + (i32) col;
            i32 y = mouse_y + (i32) row;
            cursor_back[row * CURSOR_W + col] = read_pixel(x, y);

            u8 kind = cursor_pixel_kind(col, row);
            if (kind == 2) {
                write_pixel(x, y, 0x00FFFFFFu);
            } else if (kind == 1) {
                write_pixel(x, y, 0x00000000u);
            }
        }
    }
    cursor_drawn = 1;
}

static void handle_keyboard(void)
{
    u8 scancode = inb(0x60u);
    u8 user_overlay_input = user_app_running && user_fb_overlay_active;
    if (scancode == 0x1Du || scancode == 0x9Du) {
        keyboard_ctrl_down = (scancode == 0x1Du);
        if (!user_overlay_input) {
            shell_keyboard(scancode);
        }
        return;
    }
    if (scancode == 0x38u || scancode == 0xB8u ||
        scancode == 0x2Au || scancode == 0xAAu ||
        scancode == 0x36u || scancode == 0xB6u) {
        if (!user_overlay_input) {
            shell_keyboard(scancode);
        }
        return;
    }
    if ((scancode & 0x80u) != 0) {
        return;
    }
    if (user_overlay_input && user_app_netsurf_running &&
        keyboard_ctrl_down && scancode == 0x26u) {
        event_push(EVENT_KEYBOARD, 0x40u, 0);
        return;
    }
    event_push(EVENT_KEYBOARD, scancode, 0);
    if (user_overlay_input) {
        return;
    }
    shell_keyboard(scancode);
    if (shell_key_consumed) {
        return;
    }
    if (scancode == 0x48u && selected_file > 0) {
        selected_file -= 1;
        dirty = 1;
    } else if (scancode == 0x50u && selected_file + 1u < file_count) {
        selected_file += 1;
        dirty = 1;
    } else if (scancode == 0x1Cu) {
        open_selected_file();
    } else if (scancode == 0x11u) {
        fat32_write_test();
    } else if (scancode == 0x1Eu) {
        leo_run_app();
    } else if (scancode == 0x16u) {
        /* 'U': request the ring-3 user app; launched from the main loop so we
         * never enter ring 3 from inside the keyboard IRQ handler. */
        pending_user_app = USER_APP_UHELLO;
    } else if (scancode == 0x22u) {
        pending_user_app = USER_APP_UGFX;
    } else if (scancode == 0x2Eu) {
        pending_user_app = USER_APP_UCDEMO;
    } else if (scancode == 0x30u) {
        pending_user_app = USER_APP_UBROWSER;
    } else if (scancode == 0x31u) {
        pending_user_app = USER_APP_UNETRUN;
    } else if (scancode == 0x2Du) {
        pending_user_app = USER_APP_UWEB;
    } else if (scancode == 0x1Fu) {
        pending_user_app = USER_APP_USTREAM;
    } else if (scancode == 0x24u) {
        pending_user_app = USER_APP_UQJS;
    } else if (scancode == 0x01u) {
        file_loaded = 0;
        dirty = 1;
    }
}

void isr_dispatch(const struct InterruptFrame *frame)
{
    if (frame->vector == 0x80u) {
        syscall_dispatch(frame);
        return;
    }

    if (frame->vector >= 32u && frame->vector <= 47u) {
        if (frame->vector == 32u) {
            system_ticks += 1;
            if (!(user_app_running && user_fb_overlay_active)) {
                event_push(EVENT_TIMER, system_ticks, 0);
            }
        } else if (frame->vector == 33u) {
            handle_keyboard();
        } else if (frame->vector == 44u) {
            handle_mouse();
        }
        pic_eoi(frame->vector);
        return;
    }

    u32 cr2 = 0;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    serial_print(msg_cpu_exception);
    serial_print_hex(frame->vector);
    serial_print(msg_cpu_error);
    serial_print_hex(frame->error_code);
    serial_print(msg_cpu_eip);
    serial_print_hex(frame->eip);
    serial_print(msg_cpu_cr2);
    serial_print_hex(cr2);
    serial_write('\r');
    serial_write('\n');
    panic("unhandled CPU exception");
}

static void enable_interrupts(void)
{
    __asm__ volatile ("sti");
}

static void disable_interrupts(void)
{
    __asm__ volatile ("cli");
}

static void wait_for_interrupt(void)
{
    __asm__ volatile ("hlt");
}

static void validate_bootinfo(const struct LeonBootInfo *boot_info)
{
    if (boot_info == 0 ||
        boot_info->magic != LEONOS_BOOTINFO_MAGIC ||
        boot_info->version != LEONOS_BOOTINFO_VERSION) {
        serial_print("LeonOS 32-bit kernel invalid BootInfo\r\n");
        panic("invalid BootInfo");
    }
    if (boot_info->framebuffer.address == 0 ||
        boot_info->framebuffer.bpp != 32u ||
        !framebuffer_resolution_supported(boot_info->framebuffer.width,
                                          boot_info->framebuffer.height)) {
        panic("invalid framebuffer BootInfo");
    }
    serial_print(msg_framebuffer_prefix);
    serial_print_dec(boot_info->framebuffer.width);
    serial_write('x');
    serial_print_dec(boot_info->framebuffer.height);
    serial_write('x');
    serial_write('3');
    serial_write('2');
    serial_write('\r');
    serial_write('\n');
    if (boot_info->fat_address == 0 ||
        boot_info->root_address == 0 ||
        boot_info->data_cache_address == 0) {
        panic("missing FAT12 preload buffers");
    }
}

static void task_events(struct KernelTask *task)
{
    if (user_app_running && user_fb_overlay_active) {
        return;
    }
    struct KernelEvent event;
    while (event_pop(&event)) {
        task->data0 += 1;
        if (event.type == EVENT_TIMER && !timer_reported && event.data0 >= 2u) {
            timer_reported = 1;
            serial_print("LeonOS timer/events OK\r\n");
        }
    }
}

static void task_desktop(struct KernelTask *task)
{
    (void) task;
    if (user_app_running && user_fb_overlay_active) {
        return;
    }
    if (!dirty) {
        return;
    }

    disable_interrupts();
    restore_cursor();
    dirty = 0;
    if (framebuffer_back_ready) {
        draw_pixels_override = framebuffer_back;
        draw_stride_override = framebuffer_back_stride;
        shell_draw_desktop();
        draw_pixels_override = 0;
        draw_stride_override = 0;
        framebuffer_present();
    } else {
        shell_draw_desktop();
    }
    draw_cursor_overlay();
    enable_interrupts();
}

static void task_monitor(struct KernelTask *task)
{
    if (!scheduler_reported && system_ticks >= 5u && kernel_task_count >= 3u) {
        u8 all_ran = 1;
        for (u32 index = 0; index < kernel_task_count; index += 1) {
            if (kernel_tasks[index].run_count == 0) {
                all_ran = 0;
            }
        }
        if (all_ran) {
            scheduler_reported = 1;
            task->data0 = kernel_task_count;
            serial_print_line(msg_scheduler_ok);
            serial_print(msg_scheduler_tasks);
            serial_print_dec(kernel_task_count);
            serial_write('\r');
            serial_write('\n');
        }
    }
}

static void scheduler_add(const char *name, void (*entry)(struct KernelTask *task))
{
    if (kernel_task_count >= MAX_KERNEL_TASKS) {
        panic("too many kernel tasks");
    }

    struct KernelTask *task = &kernel_tasks[kernel_task_count];
    task->name = name;
    task->entry = entry;
    task->run_count = 0;
    task->data0 = 0;
    task->data1 = 0;
    task->active = 1;
    kernel_task_count += 1;
}

static void scheduler_init(void)
{
    kernel_task_count = 0;
    scheduler_add("events", task_events);
    scheduler_add("desktop", task_desktop);
    scheduler_add("monitor", task_monitor);
    scheduler_add("net", task_net);
    serial_print("LeonOS kernel tasks registered\r\n");
}

static void scheduler_run_once(void)
{
    for (u32 index = 0; index < kernel_task_count; index += 1) {
        struct KernelTask *task = &kernel_tasks[index];
        if (!task->active) {
            continue;
        }
        task->run_count += 1;
        task->entry(task);
    }
}

void kmain(const struct LeonBootInfo *boot_info)
{
    serial_print("LeonOS 32-bit kernel entered\r\n");
    vga_write("LeonOS 32-bit kernel entered", 0x0F);
    g_boot = boot_info;
    idt_init();
    pic_remap_and_mask();
    pit_init_masked();
    serial_print("LeonOS 32-bit IDT/PIC/PIT OK\r\n");
    validate_bootinfo(boot_info);
    mouse_x = (i32) (boot_info->framebuffer.width / 2u);
    mouse_y = (i32) (boot_info->framebuffer.height / 2u);
    serial_print("LeonOS 32-bit BootInfo OK\r\n");
    serial_print(msg_memory_entries);
    serial_print_dec(boot_info->memory_map_count);
    serial_write('\r');
    serial_write('\n');
    pmm_init(boot_info);
    pmm_smoke_test();
    paging_init(boot_info);
    heap_smoke_test();
    serial_print("LeonOS kernel heap OK\r\n");
    gdt_init();
    mouse_init();
    framebuffer_back_init();
    net_init();

    ata_detect();
    if (fat32_mount()) {
        using_fat32 = 1;
        serial_print_line(msg_fat32_mounted);
        scan_root_fat32();
        serial_print(msg_root_count_prefix);
        serial_print_dec(file_count);
        serial_write('\r');
        serial_write('\n');
    } else {
        scan_root();
        serial_print("LeonOS 32-bit FAT12 root loaded\r\n");
    }
    shell_init();
    draw_pixels_override = framebuffer_back_ready ? framebuffer_back : 0;
    draw_stride_override = framebuffer_back_ready ? framebuffer_back_stride : 0;
    shell_draw_desktop();
    draw_pixels_override = 0;
    draw_stride_override = 0;
    if (framebuffer_back_ready) {
        framebuffer_present();
    }
    draw_cursor_overlay();
    dirty = 0;
    scheduler_init();
    serial_print_line(msg_gui_ready);
    enable_interrupts();
    shell_boot_serial_ready();

    for (;;) {
        shell_run_pending_app_action();
        if (pending_user_app) {
            u8 user_app = pending_user_app;
            pending_user_app = 0;
            serial_print_line(msg_user_queued);
            if (user_app == USER_APP_UCDEMO) {
                leo_run_user_app(leo_ucdemo_name, "UCDEMO.LEO");
            } else if (user_app == USER_APP_UBROWSER) {
                leo_run_user_app(leo_ubrowser_name, "UBROWSER.LEO");
            } else if (user_app == USER_APP_UNETRUN) {
                leo_run_user_app(leo_unetrun_name, "UNETRUN.LEO");
            } else if (user_app == USER_APP_UWEB) {
                leo_run_user_app(leo_uweb_name, "UWEB.LEO");
            } else if (user_app == USER_APP_NETSURF) {
                leo_run_user_app(leo_netsurf_name, "NETSURF.LEO");
            } else if (user_app == USER_APP_USTREAM) {
                leo_run_user_app(leo_ustream_name, "USTREAM.LEO");
            } else if (user_app == USER_APP_UQJS) {
                leo_run_user_app(leo_uqjs_name, "UQJS.LEO");
            } else if (user_app == USER_APP_UGFX) {
                leo_run_user_app(leo_ugfx_name, "UGFX.LEO");
            } else {
                leo_run_user_app(leo_user_name, "UHELLO.LEO");
            }
        }
        if (pending_user_browser_open) {
            pending_user_browser_open = 0u;
            shell_browser_launch_pending_url(pending_user_browser_url);
        }
        scheduler_run_once();
        wait_for_interrupt();
    }
}
