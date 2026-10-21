// Sartax OS - port szeregowy, IDT, PIC, PIT, RTC, RNG, zasilanie
#include "kernel.h"
#include "gfx.h"

// ---------------- serial (COM1, logi debug) ----------------
#define COM1 0x3F8
static bool serial_ok;

void serial_init(void) {
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 1);      // 115200
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    serial_ok = inb(COM1 + 5) != 0xFF;
}

static void serial_putc(char c) {
    if (!serial_ok) return;
    for (int i = 0; i < 10000 && !(inb(COM1 + 5) & 0x20); i++) {}
    outb(COM1, c);
}

void kprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (char *p = buf; *p; p++) {
        if (*p == '\n') serial_putc('\r');
        serial_putc(*p);
    }
}

// ---------------- panic ----------------
static __attribute__((noreturn)) void panic_screen(const char *title, const char *msg, regs_t *r) {
    cli();
    kprintf("\n*** %s: %s\n", title, msg);
    if (!g_fb.px) for (;;) hlt();
    surface_t *s = &g_fb;
    gfx_fill(s, 0, 0, s->w, s->h, RGB(20, 90, 140));
    int x = 60, y = 60;
    gfx_text(s, x, y, FONT_BIG, ":(  Sartax OS napotkal problem", RGB(255, 255, 255));
    y += 60;
    gfx_text(s, x, y, FONT_TITLE, title, RGB(255, 230, 150));
    y += 36;
    gfx_text(s, x, y, FONT_UI, msg, RGB(255, 255, 255));
    y += 30;
    if (r) {
        char line[128];
        snprintf(line, sizeof line, "RIP=%p  ERR=%lx  VEC=%lu", (void *)r->rip, r->err, r->vec);
        gfx_text(s, x, y, FONT_MONO, line, RGB(220, 240, 255)); y += 20;
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        snprintf(line, sizeof line, "RSP=%p  CR2=%p", (void *)r->rsp, (void *)cr2);
        gfx_text(s, x, y, FONT_MONO, line, RGB(220, 240, 255)); y += 20;
        snprintf(line, sizeof line, "RAX=%p RBX=%p RCX=%p", (void *)r->rax, (void *)r->rbx, (void *)r->rcx);
        gfx_text(s, x, y, FONT_MONO, line, RGB(220, 240, 255)); y += 20;
        snprintf(line, sizeof line, "RDX=%p RSI=%p RDI=%p", (void *)r->rdx, (void *)r->rsi, (void *)r->rdi);
        gfx_text(s, x, y, FONT_MONO, line, RGB(220, 240, 255)); y += 20;
    }
    y += 20;
    gfx_text(s, x, y, FONT_UI, "Uruchom komputer ponownie. Jesli to sie powtarza, przytrzymaj Shift przy starcie,", RGB(255, 255, 255));
    y += 22;
    gfx_text(s, x, y, FONT_UI, "aby uruchomic poprzednia wersje systemu.", RGB(255, 255, 255));
    gfx_present_full();
    for (;;) hlt();
}

void panic(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    panic_screen("Blad krytyczny", buf, NULL);
}

// ---------------- IDT / PIC ----------------
typedef struct PACKED {
    u16 off0, sel;
    u8 ist, flags;
    u16 off1;
    u32 off2, zero;
} idt_entry_t;

static idt_entry_t idt[256];
extern u64 isr_table[48];
static void (*irq_handlers[16])(void);
volatile u64 g_ticks;

static const char *exc_names[] = {
    "Dzielenie przez zero", "Debug", "NMI", "Breakpoint", "Przepelnienie", "Zakres", "Zla instrukcja",
    "Brak FPU", "Double fault", "Koprocesor", "Zly TSS", "Brak segmentu", "Blad stosu",
    "General protection fault", "Page fault", "?", "Blad FPU", "Alignment check", "Machine check", "SIMD",
};

void isr_dispatch(regs_t *r) {
    if (r->vec < 32) {
        char msg[96];
        snprintf(msg, sizeof msg, "Wyjatek procesora %lu: %s", r->vec, r->vec < 20 ? exc_names[r->vec] : "?");
        panic_screen("Wyjatek", msg, r);
    }
    int irq = r->vec - 32;
    if (irq == 0) g_ticks++;
    if (irq_handlers[irq]) irq_handlers[irq]();
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static void idt_set(int v, u64 h) {
    idt[v].off0 = h & 0xFFFF;
    idt[v].sel = 0x08;
    idt[v].ist = 0;
    idt[v].flags = 0x8E;
    idt[v].off1 = (h >> 16) & 0xFFFF;
    idt[v].off2 = h >> 32;
    idt[v].zero = 0;
}

static u8 pic_mask1 = 0xFB, pic_mask2 = 0xFF;

void irq_set_handler(int irq, void (*h)(void)) { irq_handlers[irq] = h; }

void irq_unmask(int irq) {
    if (irq < 8) pic_mask1 &= ~(1 << irq);
    else pic_mask2 &= ~(1 << (irq - 8));
    outb(0x21, pic_mask1);
    outb(0xA1, pic_mask2);
}

void idt_init(void) {
    for (int i = 0; i < 48; i++) idt_set(i, isr_table[i]);
    struct PACKED { u16 lim; u64 base; } d = { sizeof(idt) - 1, (u64)idt };
    __asm__ volatile("lidt %0" ::"m"(d));
    // PIC: IRQ 0-15 -> wektory 32-47
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 32); io_wait();
    outb(0xA1, 40); io_wait();
    outb(0x21, 4); io_wait();
    outb(0xA1, 2); io_wait();
    outb(0x21, 1); io_wait();
    outb(0xA1, 1); io_wait();
    outb(0x21, pic_mask1);
    outb(0xA1, pic_mask2);
    // PIT 1000 Hz
    u16 div = 1193;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
    irq_unmask(0);
    sti();
}

void sleep_ms(u32 ms) {
    u64 end = g_ticks + ms;
    while (g_ticks < end) hlt();
}

// ---------------- RTC ----------------
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static int bcd(int v, bool is_bcd) { return is_bcd ? (v & 15) + (v >> 4) * 10 : v; }

void rtc_read(datetime_t *dt) {
    while (cmos(0x0A) & 0x80) {}
    u8 sec = cmos(0), min = cmos(2), hour = cmos(4), day = cmos(7), mon = cmos(8), year = cmos(9), cent = cmos(0x32);
    u8 b = cmos(0x0B);
    bool isb = !(b & 4);
    bool pm = hour & 0x80;
    hour &= 0x7F;
    dt->sec = bcd(sec, isb);
    dt->min = bcd(min, isb);
    dt->hour = bcd(hour, isb);
    if (!(b & 2) && pm) dt->hour = (dt->hour % 12) + 12;
    dt->day = bcd(day, isb);
    dt->month = bcd(mon, isb);
    int c = bcd(cent, isb);
    dt->year = (c >= 19 && c <= 21 ? c * 100 : 2000) + bcd(year, isb);
    dt->wday = 0;
    datetime_t t = *dt;
    unix_to_datetime(datetime_to_unix(&t), &t);
    dt->wday = t.wday;
}

static bool leap(int y) { return (y % 4 == 0 && y % 100) || y % 400 == 0; }
static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

u32 datetime_to_unix(const datetime_t *d) {
    u32 days = 0;
    for (int y = 1970; y < d->year; y++) days += leap(y) ? 366 : 365;
    for (int m = 1; m < d->month; m++) days += mdays[m - 1] + (m == 2 && leap(d->year));
    days += d->day - 1;
    return days * 86400u + d->hour * 3600 + d->min * 60 + d->sec;
}

void unix_to_datetime(u32 t, datetime_t *d) {
    u32 days = t / 86400, rem = t % 86400;
    d->hour = rem / 3600;
    d->min = rem % 3600 / 60;
    d->sec = rem % 60;
    d->wday = (days + 4) % 7;      // 1970-01-01 to czwartek
    int y = 1970;
    for (;;) {
        u32 dy = leap(y) ? 366 : 365;
        if (days < dy) break;
        days -= dy;
        y++;
    }
    d->year = y;
    int m = 0;
    for (;;) {
        u32 dm = mdays[m] + (m == 1 && leap(y));
        if (days < dm) break;
        days -= dm;
        m++;
    }
    d->month = m + 1;
    d->day = days + 1;
}

u32 rtc_unix(void) {
    datetime_t d;
    rtc_read(&d);
    return datetime_to_unix(&d);
}

// ---------------- RNG ----------------
#include "bearssl.h"
static br_hmac_drbg_context drbg;

static bool has_rdrand(void) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return c & (1u << 30);
}

void rng_init(void) {
    u64 seed[32];
    for (int i = 0; i < 32; i++) {
        u64 v = rdtsc() ^ ((u64)g_ticks << 32);
        if (has_rdrand()) {
            u64 r = 0;
            unsigned char ok = 0;
            for (int t = 0; t < 10 && !ok; t++) __asm__ volatile("rdrand %0; setc %1" : "=r"(r), "=qm"(ok));
            v ^= r;
        }
        for (volatile int k = 0; k < (int)(v & 255); k++) {}
        seed[i] = v ^ rtc_unix();
    }
    br_hmac_drbg_init(&drbg, &br_sha256_vtable, seed, sizeof seed);
}

void rng_bytes(void *buf, size_t n) {
    u64 t = rdtsc();
    br_hmac_drbg_update(&drbg, &t, sizeof t);
    br_hmac_drbg_generate(&drbg, buf, n);
}

u32 rng_u32(void) {
    u32 v;
    rng_bytes(&v, 4);
    return v;
}

// ---------------- zasilanie ----------------
void sys_reboot(void) {
    cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x64, 0xFE);
    for (volatile int i = 0; i < 1000000; i++) {}
    struct PACKED { u16 lim; u64 base; } d = { 0, 0 };
    __asm__ volatile("lidt %0; int $3" ::"m"(d));
    for (;;) hlt();
}

void sys_poweroff(void) {
    cli();
    outw(0x4004, 0x3400);   // VirtualBox
    outw(0x604, 0x2000);    // QEMU
    outw(0xB004, 0x2000);   // Bochs / stary QEMU
    for (;;) hlt();
}
