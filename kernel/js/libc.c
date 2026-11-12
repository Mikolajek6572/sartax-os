// Sartax OS - minimalna biblioteka C dla silnika JavaScript (QuickJS)
#include "../kernel.h"
#include "../fs.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>

int errno;
FILE *stdout = (FILE *)1, *stderr = (FILE *)2, *stdin = (FILE *)0;

size_t kmalloc_usable(void *p);

void *malloc(size_t n) { return kmalloc(n); }
void free(void *p) { kfree(p); }
void *realloc(void *p, size_t n) {
    if (!n) { kfree(p); return NULL; }
    return krealloc(p, n);
}
void *calloc(size_t n, size_t m) { return kzalloc(n * m); }
size_t malloc_usable_size(void *p) { return p ? kmalloc_usable(p) : 0; }

void abort(void) { panic("JavaScript: abort()"); }
void exit(int c) { panic("JavaScript: exit(%d)", c); }
void __sx_assert_fail(const char *e, const char *f, int l) { panic("JavaScript: assert %s (%s:%d)", e, f, l); }

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
int atoi(const char *s) { return katoi(s); }
char *getenv(const char *n) { return NULL; }

char *strdup(const char *s) { return kstrdup(s); }
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
size_t strspn(const char *s, const char *a) { size_t n = 0; while (s[n] && strchr(a, s[n])) n++; return n; }
size_t strcspn(const char *s, const char *r) { size_t n = 0; while (s[n] && !strchr(r, s[n])) n++; return n; }

static unsigned long long parse_ull(const char *s, char **end, int base, int *neg) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    *neg = 0;
    if (*s == '-') { *neg = 1; s++; } else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    if (!base) base = s[0] == '0' ? 8 : 10;
    unsigned long long v = 0;
    for (;; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 32) >= 'a' && (*s | 32) <= 'z' ? (*s | 32) - 'a' + 10 : 99;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char *)s;
    return v;
}
long strtol(const char *s, char **e, int b) { int n; unsigned long long v = parse_ull(s, e, b, &n); return n ? -(long)v : (long)v; }
unsigned long strtoul(const char *s, char **e, int b) { int n; unsigned long long v = parse_ull(s, e, b, &n); return n ? -v : v; }
long long strtoll(const char *s, char **e, int b) { return strtol(s, e, b); }
unsigned long long strtoull(const char *s, char **e, int b) { return strtoul(s, e, b); }

double strtod(const char *s, char **end) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    double v = 0, sc = 1;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { sc /= 10; v += (*s++ - '0') * sc; } }
    if (*s == 'e' || *s == 'E') {
        int en = 0, ex = 0;
        s++;
        if (*s == '-') { en = 1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') ex = ex * 10 + (*s++ - '0');
        while (ex--) v = en ? v / 10 : v * 10;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    char *b = base, tmp[256];
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && cmp(b + (j - 1) * sz, b + j * sz) > 0; j--) {
            for (size_t k = 0; k < sz; k += sizeof tmp) {
                size_t c = MIN(sizeof tmp, sz - k);
                memcpy(tmp, b + (j - 1) * sz + k, c);
                memcpy(b + (j - 1) * sz + k, b + j * sz + k, c);
                memcpy(b + j * sz + k, tmp, c);
            }
        }
}

// ---------------- wyjscie (port szeregowy) ----------------
int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char buf[512];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    kprintf("%s", buf);
    return n;
}
int fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(f, fmt, ap); va_end(ap); return n; }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(stdout, fmt, ap); va_end(ap); return n; }
int sprintf(char *b, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(b, 1 << 20, fmt, ap); va_end(ap); return n; }
int fputc(int c, FILE *f) { kprintf("%c", c); return c; }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *s, FILE *f) { kprintf("%s", s); return 0; }
int puts(const char *s) { kprintf("%s\n", s); return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    const char *c = p;
    for (size_t i = 0; i < sz * n; i++) kprintf("%c", c[i]);
    return n;
}
int fflush(FILE *f) { return 0; }

// ---------------- czas ----------------
static u32 boot_unix;
static u64 boot_ticks;

static u64 now_ms(void) {
    if (!boot_unix) { boot_unix = rtc_unix(); boot_ticks = ticks_ms(); }
    return (u64)boot_unix * 1000 + (ticks_ms() - boot_ticks);
}

time_t time(time_t *t) { time_t v = now_ms() / 1000; if (t) *t = v; return v; }

int gettimeofday(struct timeval *tv, void *tz) {
    u64 ms = now_ms();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}

int clock_gettime(int clk, struct timespec *ts) {
    u64 ms = clk == CLOCK_MONOTONIC ? ticks_ms() : now_ms();
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000;
    return 0;
}

static void fill_tm(long t, struct tm *r, long off) {
    datetime_t d;
    unix_to_datetime((u32)(t + off), &d);
    r->tm_sec = d.sec, r->tm_min = d.min, r->tm_hour = d.hour;
    r->tm_mday = d.day, r->tm_mon = d.month - 1, r->tm_year = d.year - 1900, r->tm_wday = d.wday;
    static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    r->tm_yday = cum[d.month - 1] + d.day - 1 + (d.month > 2 && ((d.year % 4 == 0 && d.year % 100) || d.year % 400 == 0));
    r->tm_gmtoff = off;
    r->tm_zone = off ? "CET" : "UTC";
}

struct tm *gmtime_r(const time_t *t, struct tm *r) { fill_tm(*t, r, 0); r->tm_isdst = 0; return r; }

struct tm *localtime_r(const time_t *t, struct tm *r) {
    long off = cfg_get_int("strefa", 60) * 60;
    int dst = 0;
    if (cfg_get_int("czas_letni", 1)) {
        datetime_t d;
        unix_to_datetime((u32)*t, &d);
        datetime_t a = { d.year, 3, 31, 1, 0, 0, 0 }, b = { d.year, 10, 31, 1, 0, 0, 0 }, tmp;
        u32 ta = datetime_to_unix(&a), tb = datetime_to_unix(&b);
        unix_to_datetime(ta, &tmp);
        ta -= tmp.wday * 86400;
        unix_to_datetime(tb, &tmp);
        tb -= tmp.wday * 86400;
        if ((u32)*t >= ta && (u32)*t < tb) dst = 1;
    }
    fill_tm(*t, r, off + dst * 3600);
    r->tm_isdst = dst;
    return r;
}

int fegetround(void) { return 0; }
int fesetround(int r) { return 0; }
