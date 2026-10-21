// Sartax OS - SartaxFS: wlasny system plikow
//  blok 0: superblok | bitmapa blokow | tablica i-wezlow (128 B) | dane
//  i-wezel 1 = katalog glowny, katalog = tablica wpisow po 64 B
#include "fs.h"

typedef struct PACKED {
    char magic[4];
    u32 version;
    u64 total_blocks;
    u32 inode_count;
    u32 bitmap_start, bitmap_blocks;
    u32 inode_start, inode_blocks;
    u32 data_start;
    u64 free_blocks;
    u32 created;
    char label[32];
} sb_t;

#define NDIRECT 20
#define PER_BLOCK (FS_BLOCK / 4)
typedef struct PACKED {
    u16 type, flags;
    u64 size;
    u32 ctime, mtime;
    u32 direct[NDIRECT];
    u32 indirect, dindirect;
    u8 pad[20];
} inode_t;

typedef struct PACKED {
    u32 inode;
    u8 type, name_len;
    char name[58];
} dirent_t;

_Static_assert(sizeof(inode_t) == 128, "inode");
_Static_assert(sizeof(dirent_t) == 64, "dirent");

static ata_disk_t *disk;
static u64 part_lba;
static sb_t sb;
static u8 *bitmap;
static inode_t *inodes;
static u8 *bm_dirty, *in_dirty;
static bool mounted;
static u32 alloc_hint;

#define SPB (FS_BLOCK / 512)

static int blk_read(u32 b, void *buf, u32 n) { return ata_read(disk, part_lba + (u64)b * SPB, n * SPB, buf) ? FS_EIO : 0; }
static int blk_write(u32 b, const void *buf, u32 n) { return ata_write(disk, part_lba + (u64)b * SPB, n * SPB, buf) ? FS_EIO : 0; }

bool fs_mounted(void) { return mounted; }

const char *fs_error(int e) {
    switch (e) {
    case FS_OK: return "OK";
    case FS_ENOENT: return "Nie znaleziono pliku lub folderu";
    case FS_EEXIST: return "Taki plik lub folder juz istnieje";
    case FS_ENOSPC: return "Brak miejsca na dysku";
    case FS_EIO: return "Blad odczytu/zapisu dysku";
    case FS_EINVAL: return "Nieprawidlowa operacja";
    case FS_ENOTDIR: return "To nie jest folder";
    case FS_EISDIR: return "To jest folder";
    case FS_ENOTEMPTY: return "Folder nie jest pusty";
    case FS_ENAME: return "Nieprawidlowa nazwa (nie uzywaj / \\ : * ? \" < > |)";
    case FS_ENOFS: return "Brak systemu plikow";
    }
    return "Nieznany blad";
}

// ---------------- bitmapa / i-wezly ----------------
static bool bm_get(u32 b) { return bitmap[b >> 3] & (1 << (b & 7)); }
static void bm_set(u32 b, bool v) {
    if (v) bitmap[b >> 3] |= 1 << (b & 7);
    else bitmap[b >> 3] &= ~(1 << (b & 7));
    bm_dirty[b / (FS_BLOCK * 8)] = 1;
}

static void inode_dirty(u32 ino) { in_dirty[ino / (FS_BLOCK / 128)] = 1; }

void fs_sync(void) {
    if (!mounted) return;
    for (u32 i = 0; i < sb.bitmap_blocks; i++)
        if (bm_dirty[i]) { blk_write(sb.bitmap_start + i, bitmap + (size_t)i * FS_BLOCK, 1); bm_dirty[i] = 0; }
    for (u32 i = 0; i < sb.inode_blocks; i++)
        if (in_dirty[i]) { blk_write(sb.inode_start + i, (u8 *)inodes + (size_t)i * FS_BLOCK, 1); in_dirty[i] = 0; }
    static u8 sbuf[FS_BLOCK];
    memset(sbuf, 0, sizeof sbuf);
    memcpy(sbuf, &sb, sizeof sb);
    blk_write(0, sbuf, 1);
    ata_flush(disk);
}

static int alloc_blocks(u32 n, u32 *out) {
    if (n > sb.free_blocks) return FS_ENOSPC;
    u32 got = 0, b = alloc_hint;
    if (b < sb.data_start || b >= sb.total_blocks) b = sb.data_start;
    u64 scanned = 0;
    while (got < n && scanned < sb.total_blocks) {
        if (b >= sb.total_blocks) b = sb.data_start;
        if (!bm_get(b)) { bm_set(b, true); out[got++] = b; }
        b++;
        scanned++;
    }
    if (got < n) {
        for (u32 i = 0; i < got; i++) bm_set(out[i], false);
        return FS_ENOSPC;
    }
    alloc_hint = b;
    sb.free_blocks -= n;
    return 0;
}

static void free_block(u32 b) {
    if (b < sb.data_start || b >= sb.total_blocks || !bm_get(b)) return;
    bm_set(b, false);
    sb.free_blocks++;
}

static u32 alloc_inode(u16 type) {
    for (u32 i = 2; i < sb.inode_count; i++) {
        if (inodes[i].type == FT_NONE) {
            memset(&inodes[i], 0, sizeof(inode_t));
            inodes[i].type = type;
            inodes[i].ctime = inodes[i].mtime = rtc_unix();
            inode_dirty(i);
            return i;
        }
    }
    return 0;
}

// lista wszystkich blokow danych pliku (w kolejnosci)
static int inode_blocks(inode_t *in, u32 **out, u32 *count) {
    u32 n = (in->size + FS_BLOCK - 1) / FS_BLOCK;
    *count = n;
    *out = NULL;
    if (!n) return 0;
    u32 *list = kmalloc(n * 4);
    u32 *tmp = NULL;
    for (u32 i = 0; i < n && i < NDIRECT; i++) list[i] = in->direct[i];
    if (n > NDIRECT) {
        tmp = kmalloc(FS_BLOCK);
        if (blk_read(in->indirect, tmp, 1)) goto io;
        for (u32 i = NDIRECT; i < n && i < NDIRECT + PER_BLOCK; i++) list[i] = tmp[i - NDIRECT];
    }
    if (n > NDIRECT + PER_BLOCK) {
        u32 *l2 = kmalloc(FS_BLOCK);
        if (blk_read(in->dindirect, tmp, 1)) { kfree(l2); goto io; }
        for (u32 j = 0; j < PER_BLOCK && NDIRECT + PER_BLOCK + j * PER_BLOCK < n; j++) {
            if (blk_read(tmp[j], l2, 1)) { kfree(l2); goto io; }
            for (u32 k = 0; k < PER_BLOCK; k++) {
                u32 i = NDIRECT + PER_BLOCK + j * PER_BLOCK + k;
                if (i >= n) break;
                list[i] = l2[k];
            }
        }
        kfree(l2);
    }
    kfree(tmp);
    *out = list;
    return 0;
io:
    kfree(tmp);
    kfree(list);
    return FS_EIO;
}

static void inode_truncate(u32 ino) {
    inode_t *in = &inodes[ino];
    u32 *list, n;
    if (inode_blocks(in, &list, &n) == 0 && list) {
        for (u32 i = 0; i < n; i++) free_block(list[i]);
        kfree(list);
    }
    if (in->indirect) free_block(in->indirect);
    if (in->dindirect) {
        u32 *tmp = kmalloc(FS_BLOCK);
        u32 l2n = n > NDIRECT + PER_BLOCK ? (n - NDIRECT - PER_BLOCK + PER_BLOCK - 1) / PER_BLOCK : 0;
        if (!blk_read(in->dindirect, tmp, 1))
            for (u32 j = 0; j < l2n && j < PER_BLOCK; j++) free_block(tmp[j]);
        kfree(tmp);
        free_block(in->dindirect);
    }
    memset(in->direct, 0, sizeof in->direct);
    in->indirect = in->dindirect = 0;
    in->size = 0;
    inode_dirty(ino);
}

static int inode_read_all(u32 ino, u8 **data, u64 *size) {
    inode_t *in = &inodes[ino];
    u32 *list, n;
    int r = inode_blocks(in, &list, &n);
    if (r) return r;
    u8 *buf = kmalloc((size_t)n * FS_BLOCK + 1);
    // czytaj ciagle serie blokow jednym poleceniem
    for (u32 i = 0; i < n;) {
        u32 j = i + 1;
        while (j < n && list[j] == list[j - 1] + 1 && j - i < 64) j++;
        if (blk_read(list[i], buf + (size_t)i * FS_BLOCK, j - i)) { kfree(list); kfree(buf); return FS_EIO; }
        i = j;
    }
    kfree(list);
    buf[in->size] = 0;
    *data = buf;
    *size = in->size;
    return 0;
}

static int inode_write_all(u32 ino, const void *data, u64 size) {
    inode_truncate(ino);
    inode_t *in = &inodes[ino];
    u32 n = (size + FS_BLOCK - 1) / FS_BLOCK;
    u32 meta = 0;
    if (n > NDIRECT) meta++;
    if (n > NDIRECT + PER_BLOCK) meta += 1 + (n - NDIRECT - PER_BLOCK + PER_BLOCK - 1) / PER_BLOCK;
    if ((u64)n + meta > 0 && n + meta > sb.free_blocks) return FS_ENOSPC;
    u32 *list = kmalloc((n + meta + 1) * 4);
    if (n + meta && alloc_blocks(n + meta, list)) { kfree(list); return FS_ENOSPC; }
    // dane
    const u8 *src = data;
    u8 *last = NULL;
    for (u32 i = 0; i < n;) {
        u32 j = i + 1;
        while (j < n && list[j] == list[j - 1] + 1 && j - i < 64) j++;
        int r;
        if ((u64)j * FS_BLOCK <= size) {
            r = blk_write(list[i], src + (size_t)i * FS_BLOCK, j - i);
        } else {
            // ostatni blok niepelny - dopelnij zerami
            if (j - 1 > i) r = blk_write(list[i], src + (size_t)i * FS_BLOCK, j - 1 - i);
            else r = 0;
            if (!last) last = kzalloc(FS_BLOCK);
            memcpy(last, src + (size_t)(j - 1) * FS_BLOCK, size - (u64)(j - 1) * FS_BLOCK);
            if (!r) r = blk_write(list[j - 1], last, 1);
        }
        if (r) { kfree(list); kfree(last); return FS_EIO; }
        i = j;
    }
    kfree(last);
    // wskazniki
    for (u32 i = 0; i < n && i < NDIRECT; i++) in->direct[i] = list[i];
    u32 mi = n;
    u32 *tmp = kzalloc(FS_BLOCK);
    if (n > NDIRECT) {
        in->indirect = list[mi++];
        for (u32 i = NDIRECT; i < n && i < NDIRECT + PER_BLOCK; i++) tmp[i - NDIRECT] = list[i];
        blk_write(in->indirect, tmp, 1);
    }
    if (n > NDIRECT + PER_BLOCK) {
        in->dindirect = list[mi++];
        u32 *top = kzalloc(FS_BLOCK);
        u32 rest = n - NDIRECT - PER_BLOCK;
        for (u32 j = 0; j * PER_BLOCK < rest; j++) {
            memset(tmp, 0, FS_BLOCK);
            top[j] = list[mi++];
            for (u32 k = 0; k < PER_BLOCK && j * PER_BLOCK + k < rest; k++) tmp[k] = list[NDIRECT + PER_BLOCK + j * PER_BLOCK + k];
            blk_write(top[j], tmp, 1);
        }
        blk_write(in->dindirect, top, 1);
        kfree(top);
    }
    kfree(tmp);
    kfree(list);
    in->size = size;
    in->mtime = rtc_unix();
    inode_dirty(ino);
    return 0;
}

// ---------------- format / montowanie ----------------
int fs_format(ata_disk_t *d, u64 lba, u64 sectors, const char *label) {
    disk = d;
    part_lba = lba;
    mounted = false;
    memset(&sb, 0, sizeof sb);
    memcpy(sb.magic, "SXFS", 4);
    sb.version = 1;
    sb.total_blocks = sectors / SPB;
    if (sb.total_blocks < 256) return FS_ENOSPC;
    if (sb.total_blocks > 0xFFFFFFF0ull) sb.total_blocks = 0xFFFFFFF0ull;
    u32 ic = sb.total_blocks / 4;
    ic = CLAMP(ic, 1024, 32768);
    sb.inode_count = ic & ~31u;
    sb.bitmap_start = 1;
    sb.bitmap_blocks = (sb.total_blocks + FS_BLOCK * 8 - 1) / (FS_BLOCK * 8);
    sb.inode_start = sb.bitmap_start + sb.bitmap_blocks;
    sb.inode_blocks = sb.inode_count * 128 / FS_BLOCK;
    sb.data_start = sb.inode_start + sb.inode_blocks;
    sb.created = rtc_unix();
    strlcpy(sb.label, label, sizeof sb.label);

    kfree(bitmap); kfree(inodes); kfree(bm_dirty); kfree(in_dirty);
    bitmap = kzalloc((size_t)sb.bitmap_blocks * FS_BLOCK);
    inodes = kzalloc((size_t)sb.inode_blocks * FS_BLOCK);
    bm_dirty = kzalloc(sb.bitmap_blocks);
    in_dirty = kzalloc(sb.inode_blocks);
    for (u32 b = 0; b < sb.data_start; b++) bitmap[b >> 3] |= 1 << (b & 7);
    sb.free_blocks = sb.total_blocks - sb.data_start;
    inodes[1].type = FT_DIR;
    inodes[1].ctime = inodes[1].mtime = sb.created;
    mounted = true;
    // zapisz wszystko
    for (u32 i = 0; i < sb.bitmap_blocks; i++) bm_dirty[i] = 1;
    u32 chunk = 64;
    for (u32 i = 0; i < sb.inode_blocks; i += chunk) {
        u32 n = MIN(chunk, sb.inode_blocks - i);
        if (blk_write(sb.inode_start + i, (u8 *)inodes + (size_t)i * FS_BLOCK, n)) { mounted = false; return FS_EIO; }
    }
    fs_sync();
    alloc_hint = sb.data_start;
    kprintf("[fs] sformatowano: %u blokow, %u i-wezlow\n", (u32)sb.total_blocks, sb.inode_count);
    return 0;
}

int fs_mount(ata_disk_t *d, u64 lba) {
    disk = d;
    part_lba = lba;
    mounted = false;
    u8 *buf = kmalloc(FS_BLOCK);
    if (blk_read(0, buf, 1)) { kfree(buf); return FS_EIO; }
    memcpy(&sb, buf, sizeof sb);
    kfree(buf);
    if (memcmp(sb.magic, "SXFS", 4)) return FS_ENOFS;
    kfree(bitmap); kfree(inodes); kfree(bm_dirty); kfree(in_dirty);
    bitmap = kmalloc((size_t)sb.bitmap_blocks * FS_BLOCK);
    inodes = kmalloc((size_t)sb.inode_blocks * FS_BLOCK);
    bm_dirty = kzalloc(sb.bitmap_blocks);
    in_dirty = kzalloc(sb.inode_blocks);
    if (blk_read(sb.bitmap_start, bitmap, sb.bitmap_blocks)) return FS_EIO;
    for (u32 i = 0; i < sb.inode_blocks; i += 64)
        if (blk_read(sb.inode_start + i, (u8 *)inodes + (size_t)i * FS_BLOCK, MIN(64, sb.inode_blocks - i))) return FS_EIO;
    alloc_hint = sb.data_start;
    mounted = true;
    kprintf("[fs] zamontowano '%s': wolne %u MB\n", sb.label, (u32)(fs_free_bytes() >> 20));
    return 0;
}

u64 fs_free_bytes(void) { return mounted ? sb.free_blocks * FS_BLOCK : 0; }
u64 fs_total_bytes(void) { return mounted ? (sb.total_blocks - sb.data_start) * FS_BLOCK : 0; }

// ---------------- sciezki ----------------
void path_join(char *out, size_t n, const char *dir, const char *name) {
    if (!strcmp(dir, "/") || !dir[0]) snprintf(out, n, "/%s", name);
    else snprintf(out, n, "%s/%s", dir, name);
}

void path_parent(char *out, size_t n, const char *path) {
    strlcpy(out, path, n);
    char *s = strrchr(out, '/');
    if (!s || s == out) strlcpy(out, "/", n);
    else *s = 0;
}

const char *path_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

bool path_valid_name(const char *n) {
    size_t l = strlen(n);
    if (!l || l > FS_NAME_MAX) return false;
    if (!strcmp(n, ".") || !strcmp(n, "..")) return false;
    for (const char *p = n; *p; p++)
        if (strchr("/\\:*?\"<>|", *p) || (u8)*p < 32) return false;
    if (n[0] == ' ' || n[l - 1] == ' ') return false;
    return true;
}

static int dir_load(u32 ino, dirent_t **ents, u32 *count) {
    if (inodes[ino].type != FT_DIR) return FS_ENOTDIR;
    u8 *data;
    u64 size;
    int r = inode_read_all(ino, &data, &size);
    if (r) return r;
    *ents = (dirent_t *)data;
    *count = size / sizeof(dirent_t);
    return 0;
}

static int dir_find(u32 dir, const char *name, u32 *ino) {
    dirent_t *e;
    u32 n;
    int r = dir_load(dir, &e, &n);
    if (r) return r;
    size_t l = strlen(name);
    for (u32 i = 0; i < n; i++) {
        if (e[i].inode && e[i].name_len == l && !memcmp(e[i].name, name, l)) {
            *ino = e[i].inode;
            kfree(e);
            return 0;
        }
    }
    kfree(e);
    return FS_ENOENT;
}

static int lookup(const char *path, u32 *ino) {
    if (!mounted) return FS_ENOFS;
    u32 cur = 1;
    char comp[FS_NAME_MAX + 2];
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int l = 0;
        while (*p && *p != '/') {
            if (l < FS_NAME_MAX + 1) comp[l++] = *p;
            p++;
        }
        comp[l] = 0;
        if (inodes[cur].type != FT_DIR) return FS_ENOTDIR;
        int r = dir_find(cur, comp, &cur);
        if (r) return r;
    }
    *ino = cur;
    return 0;
}

// rozdziel sciezke na katalog nadrzedny (i-wezel) i nazwe
static int split(const char *path, u32 *parent, const char **name) {
    char dir[512];
    path_parent(dir, sizeof dir, path);
    *name = path_name(path);
    if (!path_valid_name(*name)) return FS_ENAME;
    int r = lookup(dir, parent);
    if (r) return r;
    if (inodes[*parent].type != FT_DIR) return FS_ENOTDIR;
    return 0;
}

static int dir_add(u32 dir, const char *name, u32 ino, u8 type) {
    dirent_t *e;
    u32 n;
    int r = dir_load(dir, &e, &n);
    if (r) return r;
    u32 slot = n;
    for (u32 i = 0; i < n; i++)
        if (!e[i].inode) { slot = i; break; }
    if (slot == n) {
        e = krealloc(e, (n + 1) * sizeof(dirent_t));
        n++;
    }
    memset(&e[slot], 0, sizeof(dirent_t));
    e[slot].inode = ino;
    e[slot].type = type;
    e[slot].name_len = strlen(name);
    memcpy(e[slot].name, name, e[slot].name_len);
    r = inode_write_all(dir, e, n * sizeof(dirent_t));
    kfree(e);
    return r;
}

static int dir_remove(u32 dir, u32 ino) {
    dirent_t *e;
    u32 n;
    int r = dir_load(dir, &e, &n);
    if (r) return r;
    for (u32 i = 0; i < n; i++)
        if (e[i].inode == ino) memset(&e[i], 0, sizeof(dirent_t));
    while (n && !e[n - 1].inode) n--;
    r = inode_write_all(dir, e, n * sizeof(dirent_t));
    kfree(e);
    return r;
}

// ---------------- API ----------------
int fs_stat(const char *path, fs_stat_t *st) {
    u32 ino;
    int r = lookup(path, &ino);
    if (r) return r;
    st->type = inodes[ino].type;
    st->size = inodes[ino].size;
    st->ctime = inodes[ino].ctime;
    st->mtime = inodes[ino].mtime;
    return 0;
}

bool fs_exists(const char *path) { u32 i; return lookup(path, &i) == 0; }
bool fs_is_dir(const char *path) { u32 i; return lookup(path, &i) == 0 && inodes[i].type == FT_DIR; }

int fs_list(const char *path, fs_entry_t **out) {
    u32 ino;
    int r = lookup(path, &ino);
    if (r) return r;
    dirent_t *e;
    u32 n;
    r = dir_load(ino, &e, &n);
    if (r) return r;
    fs_entry_t *list = kzalloc((n + 1) * sizeof(fs_entry_t));
    int c = 0;
    for (u32 i = 0; i < n; i++) {
        if (!e[i].inode) continue;
        fs_entry_t *f = &list[c++];
        memcpy(f->name, e[i].name, e[i].name_len);
        f->name[e[i].name_len] = 0;
        f->inode = e[i].inode;
        f->type = inodes[e[i].inode].type;
        f->size = inodes[e[i].inode].size;
        f->mtime = inodes[e[i].inode].mtime;
    }
    kfree(e);
    *out = list;
    return c;
}

int fs_read_file(const char *path, u8 **data, u64 *size) {
    u32 ino;
    int r = lookup(path, &ino);
    if (r) return r;
    if (inodes[ino].type != FT_FILE) return FS_EISDIR;
    return inode_read_all(ino, data, size);
}

int fs_write_file(const char *path, const void *data, u64 size) {
    u32 parent, ino;
    const char *name;
    int r = split(path, &parent, &name);
    if (r) return r;
    if (dir_find(parent, name, &ino) == 0) {
        if (inodes[ino].type != FT_FILE) return FS_EISDIR;
    } else {
        ino = alloc_inode(FT_FILE);
        if (!ino) return FS_ENOSPC;
        r = dir_add(parent, name, ino, FT_FILE);
        if (r) { inodes[ino].type = FT_NONE; return r; }
    }
    r = inode_write_all(ino, data, size);
    fs_sync();
    return r;
}

int fs_mkdir(const char *path) {
    u32 parent, ino;
    const char *name;
    int r = split(path, &parent, &name);
    if (r) return r;
    if (dir_find(parent, name, &ino) == 0) return FS_EEXIST;
    ino = alloc_inode(FT_DIR);
    if (!ino) return FS_ENOSPC;
    r = dir_add(parent, name, ino, FT_DIR);
    if (r) inodes[ino].type = FT_NONE;
    fs_sync();
    return r;
}

static int delete_inode_rec(u32 ino) {
    if (inodes[ino].type == FT_DIR) {
        dirent_t *e;
        u32 n;
        if (dir_load(ino, &e, &n) == 0) {
            for (u32 i = 0; i < n; i++)
                if (e[i].inode) delete_inode_rec(e[i].inode);
            kfree(e);
        }
    }
    inode_truncate(ino);
    inodes[ino].type = FT_NONE;
    inode_dirty(ino);
    return 0;
}

int fs_delete(const char *path) {
    u32 parent, ino;
    const char *name;
    int r = split(path, &parent, &name);
    if (r) return r;
    r = dir_find(parent, name, &ino);
    if (r) return r;
    dir_remove(parent, ino);
    delete_inode_rec(ino);
    fs_sync();
    return 0;
}

// czy 'b' lezy wewnatrz 'a' (lub jest rowne)
static bool path_inside(const char *a, const char *b) {
    size_t l = strlen(a);
    return !strncmp(a, b, l) && (b[l] == 0 || b[l] == '/' || !strcmp(a, "/"));
}

int fs_rename(const char *from, const char *to) {
    if (!strcmp(from, to)) return 0;
    if (path_inside(from, to)) return FS_EINVAL;
    u32 p1, p2, ino, tmp;
    const char *n1, *n2;
    int r = split(from, &p1, &n1);
    if (r) return r;
    r = split(to, &p2, &n2);
    if (r) return r;
    r = dir_find(p1, n1, &ino);
    if (r) return r;
    if (dir_find(p2, n2, &tmp) == 0) return FS_EEXIST;
    r = dir_add(p2, n2, ino, inodes[ino].type);
    if (r) return r;
    dir_remove(p1, ino);
    fs_sync();
    return 0;
}

int fs_copy(const char *from, const char *to) {
    if (path_inside(from, to)) return FS_EINVAL;
    fs_stat_t st;
    int r = fs_stat(from, &st);
    if (r) return r;
    if (fs_exists(to)) return FS_EEXIST;
    if (st.type == FT_FILE) {
        u8 *d;
        u64 sz;
        r = fs_read_file(from, &d, &sz);
        if (r) return r;
        r = fs_write_file(to, d, sz);
        kfree(d);
        return r;
    }
    r = fs_mkdir(to);
    if (r) return r;
    fs_entry_t *list;
    int n = fs_list(from, &list);
    if (n < 0) return n;
    for (int i = 0; i < n && !r; i++) {
        char a[512], b[512];
        path_join(a, sizeof a, from, list[i].name);
        path_join(b, sizeof b, to, list[i].name);
        r = fs_copy(a, b);
    }
    kfree(list);
    return r;
}

// ---------------- konfiguracja rozruchu ----------------
int bootcfg_read(boot_cfg_t *c) {
    u8 buf[512];
    ata_disk_t *d = disk ? disk : ata_boot_disk();
    if (!d || ata_read(d, DISK_CFG_LBA, 1, buf)) return FS_EIO;
    memcpy(c, buf, sizeof *c);
    return memcmp(c->magic, "SXCF", 4) ? FS_ENOFS : 0;
}

int bootcfg_write(const boot_cfg_t *c) {
    u8 buf[512] = { 0 };
    ata_disk_t *d = disk ? disk : ata_boot_disk();
    memcpy(buf, c, sizeof *c);
    if (!d || ata_write(d, DISK_CFG_LBA, 1, buf)) return FS_EIO;
    ata_flush(d);
    return 0;
}

// ---------------- ustawienia ----------------
#define CFG_PATH "/System/ustawienia.cfg"
#define CFG_MAX 48
static struct { char key[32]; char val[128]; } cfg[CFG_MAX];
static int ncfg;

void cfg_load(void) {
    ncfg = 0;
    u8 *d;
    u64 sz;
    if (fs_read_file(CFG_PATH, &d, &sz)) return;
    char *p = (char *)d;
    while (*p && ncfg < CFG_MAX) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            strlcpy(cfg[ncfg].key, p, sizeof cfg[0].key);
            strlcpy(cfg[ncfg].val, eq + 1, sizeof cfg[0].val);
            char *cr = strchr(cfg[ncfg].val, '\r');
            if (cr) *cr = 0;
            ncfg++;
        }
        if (!nl) break;
        p = nl + 1;
    }
    kfree(d);
}

void cfg_save(void) {
    char *buf = kmalloc(CFG_MAX * 170);
    buf[0] = 0;
    size_t pos = 0;
    for (int i = 0; i < ncfg; i++) pos += snprintf(buf + pos, CFG_MAX * 170 - pos, "%s=%s\n", cfg[i].key, cfg[i].val);
    if (!fs_exists("/System")) fs_mkdir("/System");
    fs_write_file(CFG_PATH, buf, pos);
    kfree(buf);
}

const char *cfg_get(const char *key, const char *def) {
    for (int i = 0; i < ncfg; i++)
        if (!strcmp(cfg[i].key, key)) return cfg[i].val;
    return def;
}

int cfg_get_int(const char *key, int def) {
    const char *v = cfg_get(key, NULL);
    return v ? katoi(v) : def;
}

void cfg_set(const char *key, const char *val) {
    for (int i = 0; i < ncfg; i++)
        if (!strcmp(cfg[i].key, key)) { strlcpy(cfg[i].val, val, sizeof cfg[0].val); return; }
    if (ncfg < CFG_MAX) {
        strlcpy(cfg[ncfg].key, key, sizeof cfg[0].key);
        strlcpy(cfg[ncfg].val, val, sizeof cfg[0].val);
        ncfg++;
    }
}
