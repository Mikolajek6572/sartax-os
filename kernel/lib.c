// Sartax OS - funkcje biblioteczne
#include "kernel.h"

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n)::"memory");
    return r;
}

void *memmove(void *d, const void *s, size_t n) {
    if (d == s || n == 0) return d;
    if ((u8 *)d < (const u8 *)s || (u8 *)d >= (const u8 *)s + n) return memcpy(d, s, n);
    u8 *dp = (u8 *)d + n - 1;
    const u8 *sp = (const u8 *)s + n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(dp), "+S"(sp), "+c"(n)::"memory");
    return d;
}

void *memset(void *d, int c, size_t n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (u8)a[i] - (u8)b[i];
    }
    return 0;
}

// porownanie bez wielkosci liter, polskie litery traktowane zaraz po bazowych
static u32 fold_cp(u32 c) {
    if (c >= 'A' && c <= 'Z') return (c - 'A' + 'a') * 4;
    if (c >= 'a' && c <= 'z') return c * 4;
    switch (c) {
    case 0x104: case 0x105: return 'a' * 4 + 1;
    case 0x106: case 0x107: return 'c' * 4 + 1;
    case 0x118: case 0x119: return 'e' * 4 + 1;
    case 0x141: case 0x142: return 'l' * 4 + 1;
    case 0x143: case 0x144: return 'n' * 4 + 1;
    case 0xD3: case 0xF3: return 'o' * 4 + 1;
    case 0x15A: case 0x15B: return 's' * 4 + 1;
    case 0x179: case 0x17A: return 'z' * 4 + 1;
    case 0x17B: case 0x17C: return 'z' * 4 + 2;
    }
    return c * 4;
}

int strcasecmp_pl(const char *a, const char *b) {
    for (;;) {
        u32 ca = utf8_decode(&a), cb = utf8_decode(&b);
        u32 fa = fold_cp(ca), fb = fold_cp(cb);
        if (fa != fb) return fa < fb ? -1 : 1;
        if (!ca) return 0;
    }
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) {}
    return r;
}

void strlcpy(char *d, const char *s, size_t n) {
    if (!n) return;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

void strlcat(char *d, const char *s, size_t n) {
    size_t l = strlen(d);
    if (l < n) strlcpy(d + l, s, n - l);
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *r = NULL;
    for (; *s; s++)
        if (*s == (char)c) r = s;
    return (char *)r;
}

char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++)
        if (!strncmp(h, n, l)) return (char *)h;
    return NULL;
}

char *kstrdup(const char *s) {
    size_t l = strlen(s);
    char *r = kmalloc(l + 1);
    memcpy(r, s, l + 1);
    return r;
}

int katoi(const char *s) {
    int neg = 0, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') neg = 1, s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

bool str_ends_with_ci(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (b > a) return false;
    s += a - b;
    for (size_t i = 0; i < b; i++) {
        char x = s[i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

void fmt_size(char *buf, size_t n, u64 b) {
    if (b < 1024) snprintf(buf, n, "%u B", (u32)b);
    else if (b < 1024 * 1024) snprintf(buf, n, "%u,%u KB", (u32)(b / 1024), (u32)(b % 1024 * 10 / 1024));
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, n, "%u,%u MB", (u32)(b >> 20), (u32)((b & 0xFFFFF) * 10 >> 20));
    else snprintf(buf, n, "%u,%u GB", (u32)(b >> 30), (u32)((b & 0x3FFFFFFF) * 10 >> 30));
}

// ---- UTF-8 ----
u32 utf8_decode(const char **pp) {
    const u8 *p = (const u8 *)*pp;
    u32 c = *p;
    if (!c) return 0;
    if (c < 0x80) { *pp += 1; return c; }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { *pp += 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *pp += 3;
        return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *pp += 4;
        return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    *pp += 1;
    return '?';
}

int utf8_encode(u32 c, char *o) {
    if (c < 0x80) { o[0] = c; return 1; }
    if (c < 0x800) { o[0] = 0xC0 | (c >> 6); o[1] = 0x80 | (c & 0x3F); return 2; }
    if (c < 0x10000) { o[0] = 0xE0 | (c >> 12); o[1] = 0x80 | ((c >> 6) & 0x3F); o[2] = 0x80 | (c & 0x3F); return 3; }
    o[0] = 0xF0 | (c >> 18); o[1] = 0x80 | ((c >> 12) & 0x3F); o[2] = 0x80 | ((c >> 6) & 0x3F); o[3] = 0x80 | (c & 0x3F);
    return 4;
}

int utf8_prev(const char *s, int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((u8)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

int utf8_next(const char *s, int pos) {
    if (!s[pos]) return pos;
    pos++;
    while (s[pos] && ((u8)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

// ---- printf ----
static void out_c(char *buf, size_t n, size_t *pos, char c) {
    if (*pos + 1 < n) buf[*pos] = c;
    (*pos)++;
}

int vsnprintf(char *buf, size_t n, const char *f, va_list ap) {
    size_t pos = 0;
    for (; *f; f++) {
        if (*f != '%') { out_c(buf, n, &pos, *f); continue; }
        f++;
        int left = 0, zero = 0, width = 0, lng = 0, prec = -1;
        while (*f == '-' || *f == '0') { if (*f == '-') left = 1; else zero = 1; f++; }
        if (*f == '*') { width = va_arg(ap, int); f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { f++; prec = 0; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        while (*f == 'l') { lng++; f++; }
        if (*f == 'z') { lng = 2; f++; }
        char tmp[32];
        const char *s = tmp;
        int len = 0;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            u64 v;
            int neg = 0, base = (*f == 'x' || *f == 'X' || *f == 'p') ? 16 : 10;
            if (*f == 'p') { v = (u64)va_arg(ap, void *); lng = 2; }
            else if (*f == 'd' || *f == 'i') {
                i64 sv = lng ? va_arg(ap, i64) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (u64)(-sv); } else v = sv;
            } else v = lng ? va_arg(ap, u64) : va_arg(ap, unsigned);
            const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char rev[24];
            int r = 0;
            do { rev[r++] = dig[v % base]; v /= base; } while (v);
            if (neg) tmp[len++] = '-';
            while (r) tmp[len++] = rev[--r];
            tmp[len] = 0;
            break;
        }
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            zero = 0;
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        default:
            tmp[0] = '?';
            len = 1;
        }
        int pad = width > len ? width - len : 0;
        if (!left) {
            if (zero && s == tmp && tmp[0] == '-') { out_c(buf, n, &pos, '-'); s++; len--; }
            while (pad--) out_c(buf, n, &pos, zero ? '0' : ' ');
        }
        for (int i = 0; i < len; i++) out_c(buf, n, &pos, s[i]);
        if (left) while (pad-- > 0) out_c(buf, n, &pos, ' ');
    }
    if (n) buf[pos < n ? pos : n - 1] = 0;
    return (int)pos;
}

int snprintf(char *buf, size_t n, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(buf, n, f, ap);
    va_end(ap);
    return r;
}

void *memchr(const void *s, int c, size_t n) {
    const u8 *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (u8)c) return (void *)(p + i);
    return NULL;
}
