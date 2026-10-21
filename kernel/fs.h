// Sartax OS - dyski i system plikow SartaxFS
#pragma once
#include "kernel.h"

// ---- dyski ATA ----
typedef struct {
    int id;
    bool present, atapi, lba48;
    u16 base, ctrl;
    u8 slave;
    u64 sectors;
    char model[41];
} ata_disk_t;

void ata_init(void);
int ata_count(void);
ata_disk_t *ata_get(int i);
ata_disk_t *ata_boot_disk(void);
int ata_read(ata_disk_t *d, u64 lba, u32 count, void *buf);
int ata_write(ata_disk_t *d, u64 lba, u32 count, const void *buf);
int ata_flush(ata_disk_t *d);

// ---- uklad dysku Sartax ----
#define DISK_CFG_LBA     1
#define DISK_STAGE2_LBA  2
#define DISK_SLOT0_LBA   64
#define DISK_SLOT_SECT   16384          // 8 MB na jadro
#define DISK_SLOT1_LBA   (DISK_SLOT0_LBA + DISK_SLOT_SECT)
#define DISK_FS_LBA      32768          // 16 MB

typedef struct PACKED {
    char magic[4];                       // SXCF
    u32 version;
    u16 width, height;
    u32 active_slot;
    struct PACKED { u32 lba, sectors, bytes; } slot[2];
    u32 fs_lba;
    char slot_ver[2][16];
} boot_cfg_t;

// ---- SartaxFS ----
#define FS_BLOCK 4096
#define FS_NAME_MAX 57
enum { FT_NONE = 0, FT_FILE = 1, FT_DIR = 2 };

typedef struct {
    char name[FS_NAME_MAX + 1];
    u8 type;
    u64 size;
    u32 mtime;
    u32 inode;
} fs_entry_t;

typedef struct {
    u8 type;
    u64 size;
    u32 ctime, mtime;
} fs_stat_t;

typedef struct {
    const char *path;
    const u8 *data;
    u32 size;
} initfs_file_t;
extern const initfs_file_t g_initfs[];

int fs_format(ata_disk_t *d, u64 lba, u64 sectors, const char *label);
int fs_mount(ata_disk_t *d, u64 lba);
bool fs_mounted(void);
int fs_stat(const char *path, fs_stat_t *st);
bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
int fs_list(const char *path, fs_entry_t **out);       // zwraca liczbe wpisow (kfree(*out))
int fs_read_file(const char *path, u8 **data, u64 *size); // kfree(*data)
int fs_write_file(const char *path, const void *data, u64 size);
int fs_mkdir(const char *path);
int fs_delete(const char *path);                         // rekurencyjnie
int fs_rename(const char *from, const char *to);
int fs_copy(const char *from, const char *to);           // rekurencyjnie
u64 fs_free_bytes(void);
u64 fs_total_bytes(void);
const char *fs_error(int err);
void fs_sync(void);

enum { FS_OK = 0, FS_ENOENT = -1, FS_EEXIST = -2, FS_ENOSPC = -3, FS_EIO = -4, FS_EINVAL = -5, FS_ENOTDIR = -6,
       FS_EISDIR = -7, FS_ENOTEMPTY = -8, FS_ENAME = -9, FS_ENOFS = -10 };

// sciezki
void path_join(char *out, size_t n, const char *dir, const char *name);
void path_parent(char *out, size_t n, const char *path);
const char *path_name(const char *path);
bool path_valid_name(const char *name);

// ustawienia (/System/ustawienia.cfg)
const char *cfg_get(const char *key, const char *def);
void cfg_set(const char *key, const char *val);
int cfg_get_int(const char *key, int def);
void cfg_load(void);
void cfg_save(void);

// konfiguracja rozruchu (sektor 1)
int bootcfg_read(boot_cfg_t *c);
int bootcfg_write(const boot_cfg_t *c);
