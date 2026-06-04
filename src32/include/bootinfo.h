#ifndef LEONOS_BOOTINFO_H
#define LEONOS_BOOTINFO_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define LEONOS_BOOTINFO_MAGIC 0x4C454F4Eu
#define LEONOS_BOOTINFO_VERSION 1u
#define LEONOS_MEMORY_MAP_MAX 32u
#define LEONOS_ROOT_ENTRIES 224u

struct LeonFramebufferInfo {
    u32 address;
    u32 pitch;
    u32 width;
    u32 height;
    u32 bpp;
};

struct LeonMemoryMapEntry {
    u64 base;
    u64 length;
    u32 type;
    u32 reserved;
};

struct LeonBootInfo {
    u32 magic;
    u32 version;
    struct LeonFramebufferInfo framebuffer;
    u32 memory_map_count;
    struct LeonMemoryMapEntry memory_map[LEONOS_MEMORY_MAP_MAX];
    u32 boot_drive;
    u32 fat_lba;
    u32 root_lba;
    u32 data_lba;
    u32 fat_address;
    u32 fat_sectors;
    u32 root_address;
    u32 root_entries;
    u32 data_cache_address;
    u32 data_cache_sectors;
};

#endif
