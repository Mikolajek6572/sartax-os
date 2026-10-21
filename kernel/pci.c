// Sartax OS - magistrala PCI + VirtualBox VMMDev (mysz bez przechwytywania)
#include "kernel.h"

static pci_dev_t devs[64];
static int ndevs;

u32 pci_read(u8 bus, u8 dev, u8 fn, u8 off) {
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

void pci_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v) {
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

void pci_init(void) {
    for (int bus = 0; bus < 8; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                u32 id = pci_read(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (fn == 0) break; continue; }
                if (ndevs >= 64) return;
                pci_dev_t *d = &devs[ndevs++];
                d->bus = bus, d->dev = dev, d->fn = fn;
                d->vendor = id & 0xFFFF;
                d->device = id >> 16;
                u32 c = pci_read(bus, dev, fn, 8);
                d->cls = c >> 24, d->subcls = c >> 16, d->progif = c >> 8;
                d->irq = pci_read(bus, dev, fn, 0x3C) & 0xFF;
                for (int i = 0; i < 6; i++) d->bar[i] = pci_read(bus, dev, fn, 0x10 + i * 4);
                kprintf("[pci] %d:%d.%d %04x:%04x klasa %02x.%02x\n", bus, dev, fn, d->vendor, d->device, d->cls, d->subcls);
                if (fn == 0 && !(pci_read(bus, dev, fn, 0x0C) & 0x800000)) break;
            }
}

int pci_count(void) { return ndevs; }
pci_dev_t *pci_get(int i) { return &devs[i]; }

pci_dev_t *pci_find(u16 v, u16 d) {
    for (int i = 0; i < ndevs; i++)
        if (devs[i].vendor == v && devs[i].device == d) return &devs[i];
    return NULL;
}

void pci_enable_busmaster(pci_dev_t *d) {
    u32 cmd = pci_read(d->bus, d->dev, d->fn, 4);
    cmd |= 0x07;
    pci_write(d->bus, d->dev, d->fn, 4, cmd);
}

// ---------------- VirtualBox VMMDev ----------------
typedef struct PACKED {
    u32 size, version, type;
    i32 rc;
    u32 r1, r2;
} vmm_hdr_t;

typedef struct PACKED {
    vmm_hdr_t h;
    u32 features;
    i32 x, y;
} vmm_mouse_t;

typedef struct PACKED {
    vmm_hdr_t h;
    u32 version, ostype;
} vmm_guest_info_t;

typedef struct PACKED {
    vmm_hdr_t h;
    u32 flags, xhot, yhot, w, h2;
    u8 data[4];
} vmm_pointer_t;

static u16 vmm_port;
static vmm_mouse_t mouse_req __attribute__((aligned(16)));
static vmm_guest_info_t info_req __attribute__((aligned(16)));
static vmm_pointer_t ptr_req __attribute__((aligned(16)));

static void vmm_hdr(vmm_hdr_t *h, u32 size, u32 type) {
    h->size = size;
    h->version = 0x10001;
    h->type = type;
    h->rc = -1;
    h->r1 = h->r2 = 0;
}

bool vbox_present(void) { return vmm_port != 0; }

bool vbox_mouse_get(int *x, int *y) {
    if (!vmm_port) return false;
    vmm_hdr(&mouse_req.h, sizeof mouse_req, 1);
    mouse_req.features = 0;
    outl(vmm_port, (u32)(u64)&mouse_req);
    if (mouse_req.h.rc < 0) return false;
    if (!(mouse_req.features & 2)) return false;   // host nie chce trybu bezwzglednego
    *x = mouse_req.x;
    *y = mouse_req.y;
    return true;
}

void vbox_init(void) {
    pci_dev_t *d = pci_find(0x80EE, 0xCAFE);
    if (!d) return;
    pci_enable_busmaster(d);
    vmm_port = d->bar[0] & 0xFFFC;
    vmm_hdr(&info_req.h, sizeof info_req, 50);
    info_req.version = 0x00010004;
    info_req.ostype = 0;
    outl(vmm_port, (u32)(u64)&info_req);
    // tryb bezwzgledny myszy
    vmm_hdr(&mouse_req.h, sizeof mouse_req, 2);
    mouse_req.features = (1 << 0) | (1 << 4);
    mouse_req.x = mouse_req.y = 0;
    outl(vmm_port, (u32)(u64)&mouse_req);
    // ukryj kursor hosta - rysujemy wlasny
    vmm_hdr(&ptr_req.h, sizeof ptr_req, 3);
    ptr_req.flags = 0;
    outl(vmm_port, (u32)(u64)&ptr_req);
    g_mouse_absolute = true;
    kprintf("[vbox] VMMDev port %x, mysz bezwzgledna\n", vmm_port);
}
