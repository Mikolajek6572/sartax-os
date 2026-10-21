// Sartax OS - wspolne definicje jadra
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define SARTAX_VERSION "1.0.1"
#define SARTAX_VERSION_CODE 10001

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define PACKED __attribute__((packed))

typedef struct PACKED {
    u32 magic, boot_type, boot_drive;
    u32 fb_addr, fb_pitch, fb_width, fb_height, fb_bpp;
    u32 kernel_size, kernel_slot;
    u16 e820_count, pad;
    u32 kernel_lba, prev_version;
} bootinfo_t;

typedef struct PACKED {
    u64 base, len;
    u32 type, attr;
} e820_t;

enum { BOOT_CD = 1, BOOT_HDD = 2 };

extern bootinfo_t *g_boot;
extern u8 *pristine_image;
extern char _image_end[], _kernel_end[];

// ---- porty ----
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" ::"a"(v), "Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" ::"a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }
static inline u64 rdtsc(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((u64)hi << 32) | lo; }
static inline u64 irq_save(void) { u64 f; __asm__ volatile("pushfq; pop %0; cli" : "=r"(f)::"memory"); return f; }
static inline void irq_restore(u64 f) { if (f & 0x200) sti(); }
static inline u32 mmio_r32(u64 a) { return *(volatile u32 *)a; }
static inline void mmio_w32(u64 a, u32 v) { *(volatile u32 *)a = v; }

// ---- lib ----
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp_pl(const char *a, const char *b);
char *strcpy(char *d, const char *s);
void strlcpy(char *d, const char *s, size_t n);
void strlcat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *kstrdup(const char *s);
int katoi(const char *s);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...);
bool str_ends_with_ci(const char *s, const char *suffix);
void fmt_size(char *buf, size_t n, u64 bytes);
u32 utf8_decode(const char **p);
int utf8_encode(u32 cp, char *out);
int utf8_prev(const char *s, int pos);
int utf8_next(const char *s, int pos);

// ---- pamiec ----
void heap_init(void);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void *krealloc(void *p, size_t n);
void kfree(void *p);
u64 heap_free_bytes(void);
u64 heap_total_bytes(void);

// ---- debug ----
void serial_init(void);
void kprintf(const char *fmt, ...);
void panic(const char *fmt, ...) __attribute__((noreturn));

// ---- przerwania / czas ----
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vec, err, rip, cs, rflags, rsp, ss;
} regs_t;
void idt_init(void);
void irq_set_handler(int irq, void (*h)(void));
void irq_unmask(int irq);
extern volatile u64 g_ticks;          // ms od startu
void sleep_ms(u32 ms);
static inline u64 ticks_ms(void) { return g_ticks; }

// ---- rtc ----
typedef struct { int year, month, day, hour, min, sec, wday; } datetime_t;
void rtc_read(datetime_t *dt);
u32 rtc_unix(void);
void unix_to_datetime(u32 t, datetime_t *dt);
u32 datetime_to_unix(const datetime_t *dt);

// ---- wejscie ----
enum {
    KEY_NONE = 0, KEY_ESC = 0x100, KEY_ENTER, KEY_BACKSPACE, KEY_TAB, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DEL, KEY_INS,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_LSHIFT, KEY_RSHIFT, KEY_CTRL, KEY_ALT, KEY_ALTGR, KEY_WIN, KEY_CAPS, KEY_CHAR
};
enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_ALTGR = 8 };
enum { EV_NONE, EV_KEY, EV_MOUSE };
typedef struct {
    u8 type;
    u8 pressed;     // klawisz wcisniety
    u8 mods;
    u8 buttons;     // mysz: bit0 lewy, bit1 prawy, bit2 srodkowy
    u16 key;        // KEY_* albo KEY_CHAR
    u32 ch;         // znak unicode (dla KEY_CHAR i zwyklych klawiszy)
    i32 x, y;       // mysz: pozycja bezwzgledna
    i32 wheel;
} event_t;
void input_init(void);
bool input_poll(event_t *ev);
void input_push(const event_t *ev);
void mouse_poll_absolute(void);
extern int g_mouse_x, g_mouse_y;
extern u8 g_mouse_buttons;
extern bool g_mouse_absolute;

// ---- pci ----
typedef struct {
    u8 bus, dev, fn;
    u16 vendor, device;
    u8 cls, subcls, progif, irq;
    u32 bar[6];
} pci_dev_t;
void pci_init(void);
int pci_count(void);
pci_dev_t *pci_get(int i);
pci_dev_t *pci_find(u16 vendor, u16 device);
u32 pci_read(u8 bus, u8 dev, u8 fn, u8 off);
void pci_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v);
void pci_enable_busmaster(pci_dev_t *d);

// ---- vbox ----
void vbox_init(void);
bool vbox_present(void);

// ---- zasilanie ----
void sys_reboot(void) __attribute__((noreturn));
void sys_poweroff(void) __attribute__((noreturn));

// ---- losowe ----
void rng_init(void);
void rng_bytes(void *buf, size_t n);
u32 rng_u32(void);
