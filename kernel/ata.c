// Sartax OS - sterownik dyskow ATA (PIO, LBA28/LBA48)
#include "fs.h"

static ata_disk_t disks[4];
static int ndisks;

#define REG_DATA 0
#define REG_ERR 1
#define REG_COUNT 2
#define REG_LBA0 3
#define REG_LBA1 4
#define REG_LBA2 5
#define REG_DRIVE 6
#define REG_CMD 7
#define REG_STATUS 7

#define ST_BSY 0x80
#define ST_DRDY 0x40
#define ST_DF 0x20
#define ST_DRQ 0x08
#define ST_ERR 0x01

static void delay400(ata_disk_t *d) { for (int i = 0; i < 4; i++) inb(d->ctrl); }

static int wait_bsy(ata_disk_t *d) {
    for (u32 i = 0; i < 20000000; i++) {
        u8 s = inb(d->base + REG_STATUS);
        if (!(s & ST_BSY)) return s;
    }
    return -1;
}

static int wait_drq(ata_disk_t *d) {
    for (u32 i = 0; i < 20000000; i++) {
        u8 s = inb(d->base + REG_STATUS);
        if (s & ST_BSY) continue;
        if (s & (ST_ERR | ST_DF)) return -1;
        if (s & ST_DRQ) return 0;
    }
    return -1;
}

static void probe(u16 base, u16 ctrl, u8 slave) {
    ata_disk_t *d = &disks[ndisks];
    memset(d, 0, sizeof *d);
    d->base = base, d->ctrl = ctrl, d->slave = slave;
    outb(ctrl, 0x02);                         // bez przerwan
    outb(base + REG_DRIVE, 0xA0 | (slave << 4));
    delay400(d);
    if (inb(base + REG_STATUS) == 0xFF) return;
    outb(base + REG_COUNT, 0);
    outb(base + REG_LBA0, 0);
    outb(base + REG_LBA1, 0);
    outb(base + REG_LBA2, 0);
    outb(base + REG_CMD, 0xEC);               // IDENTIFY
    delay400(d);
    u8 st = inb(base + REG_STATUS);
    if (!st) return;
    if (wait_bsy(d) < 0) return;
    u8 l1 = inb(base + REG_LBA1), l2 = inb(base + REG_LBA2);
    if ((l1 == 0x14 && l2 == 0xEB) || (l1 == 0x69 && l2 == 0x96)) {
        d->atapi = true;
        d->present = true;
        strlcpy(d->model, "Naped CD/DVD", sizeof d->model);
        d->id = ndisks++;
        return;
    }
    if (l1 || l2) return;
    if (wait_drq(d) < 0) return;
    u16 id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(base + REG_DATA);
    d->lba48 = id[83] & (1 << 10);
    if (d->lba48) d->sectors = *(u64 *)&id[100];
    else d->sectors = *(u32 *)&id[60];
    for (int i = 0; i < 20; i++) {
        d->model[i * 2] = id[27 + i] >> 8;
        d->model[i * 2 + 1] = id[27 + i] & 0xFF;
    }
    d->model[40] = 0;
    for (int i = 39; i >= 0 && d->model[i] == ' '; i--) d->model[i] = 0;
    d->present = true;
    d->id = ndisks++;
    kprintf("[ata] dysk %d: %s, %u MB%s\n", d->id, d->model, (u32)(d->sectors >> 11), d->lba48 ? " LBA48" : "");
}

void ata_init(void) {
    ndisks = 0;
    probe(0x1F0, 0x3F6, 0);
    probe(0x1F0, 0x3F6, 1);
    probe(0x170, 0x376, 0);
    probe(0x170, 0x376, 1);
}

int ata_count(void) { return ndisks; }
ata_disk_t *ata_get(int i) { return i >= 0 && i < ndisks ? &disks[i] : NULL; }

// dysk, z ktorego wystartowal system (pierwszy dysk z konfiguracja Sartax)
ata_disk_t *ata_boot_disk(void) {
    u8 buf[512];
    for (int i = 0; i < ndisks; i++) {
        if (disks[i].atapi) continue;
        if (ata_read(&disks[i], DISK_CFG_LBA, 1, buf) == 0 && !memcmp(buf, "SXCF", 4)) return &disks[i];
    }
    return NULL;
}

static int setup(ata_disk_t *d, u64 lba, u32 count, u8 cmd28, u8 cmd48, u8 *cmd) {
    if (wait_bsy(d) < 0) return -1;
    if (d->lba48 && (lba + count > 0x0FFFFFFF || count > 256)) {
        outb(d->base + REG_DRIVE, 0x40 | (d->slave << 4));
        outb(d->base + REG_COUNT, count >> 8);
        outb(d->base + REG_LBA0, lba >> 24);
        outb(d->base + REG_LBA1, lba >> 32);
        outb(d->base + REG_LBA2, lba >> 40);
        outb(d->base + REG_COUNT, count);
        outb(d->base + REG_LBA0, lba);
        outb(d->base + REG_LBA1, lba >> 8);
        outb(d->base + REG_LBA2, lba >> 16);
        *cmd = cmd48;
    } else {
        outb(d->base + REG_DRIVE, 0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F));
        outb(d->base + REG_COUNT, count == 256 ? 0 : count);
        outb(d->base + REG_LBA0, lba);
        outb(d->base + REG_LBA1, lba >> 8);
        outb(d->base + REG_LBA2, lba >> 16);
        *cmd = cmd28;
    }
    return 0;
}

int ata_read(ata_disk_t *d, u64 lba, u32 count, void *buf) {
    if (!d || d->atapi) return -1;
    u16 *p = buf;
    while (count) {
        u32 n = count > 256 ? 256 : count;
        u8 cmd;

        if (setup(d, lba, n, 0x20, 0x24, &cmd) < 0) { return -1; }
        outb(d->base + REG_CMD, cmd);
        for (u32 s = 0; s < n; s++) {
            delay400(d);
            if (wait_drq(d) < 0) { kprintf("[ata] blad odczytu LBA %u\n", (u32)lba); return -1; }
            { u64 c = 256; __asm__ volatile("rep insw" : "+D"(p), "+c"(c) : "d"(d->base + REG_DATA) : "memory"); }
        }

        lba += n;
        count -= n;
    }
    return 0;
}

int ata_write(ata_disk_t *d, u64 lba, u32 count, const void *buf) {
    if (!d || d->atapi) return -1;
    const u16 *p = buf;
    while (count) {
        u32 n = count > 256 ? 256 : count;
        u8 cmd;

        if (setup(d, lba, n, 0x30, 0x34, &cmd) < 0) { return -1; }
        outb(d->base + REG_CMD, cmd);
        for (u32 s = 0; s < n; s++) {
            delay400(d);
            if (wait_drq(d) < 0) { kprintf("[ata] blad zapisu LBA %u\n", (u32)lba); return -1; }
            { u64 c = 256; __asm__ volatile("rep outsw" : "+S"(p), "+c"(c) : "d"(d->base + REG_DATA) : "memory"); }
        }
        delay400(d);
        wait_bsy(d);

        lba += n;
        count -= n;
    }
    return 0;
}

int ata_flush(ata_disk_t *d) {
    if (!d || d->atapi) return -1;
    wait_bsy(d);
    outb(d->base + REG_DRIVE, 0xA0 | (d->slave << 4));
    outb(d->base + REG_CMD, d->lba48 ? 0xEA : 0xE7);
    delay400(d);
    return wait_bsy(d) < 0 ? -1 : 0;
}
