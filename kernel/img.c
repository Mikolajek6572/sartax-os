// Sartax OS - dekodery obrazkow: PNG (z inflate), JPEG (baseline), GIF (pierwsza klatka)
#include "gfx.h"

#define MAX_DIM 4096

static image_t *img_new(int w, int h) {
    if (w <= 0 || h <= 0 || w > MAX_DIM || h > MAX_DIM) return NULL;
    image_t *im = kmalloc(sizeof(image_t));
    im->w = w;
    im->h = h;
    im->px = kzalloc((size_t)w * h * 4);
    return im;
}

// ================= inflate (RFC 1951) =================
typedef struct {
    const u8 *src, *end;
    u32 bitbuf;
    int bitcnt;
    u8 *out;
    size_t olen, ocap;
    bool grow, err;
} inf_t;

typedef struct { u16 counts[16]; u16 symbols[320]; } huff_t;

static int getbit(inf_t *s) {
    if (!s->bitcnt) {
        if (s->src >= s->end) { s->err = true; return 0; }
        s->bitbuf = *s->src++;
        s->bitcnt = 8;
    }
    int b = s->bitbuf & 1;
    s->bitbuf >>= 1;
    s->bitcnt--;
    return b;
}

static u32 getbits(inf_t *s, int n) {
    u32 v = 0;
    for (int i = 0; i < n; i++) v |= (u32)getbit(s) << i;
    return v;
}

static void huff_build(huff_t *h, const u8 *lens, int n) {
    u16 offs[16];
    memset(h->counts, 0, sizeof h->counts);
    for (int i = 0; i < n; i++) h->counts[lens[i]]++;
    h->counts[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->counts[i];
    for (int i = 0; i < n; i++)
        if (lens[i]) h->symbols[offs[lens[i]]++] = i;
}

static int huff_decode(inf_t *s, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= getbit(s);
        int count = h->counts[len];
        if (code - first < count) return h->symbols[index + code - first];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (s->err) return -1;
    }
    s->err = true;
    return -1;
}

static bool out_byte(inf_t *s, u8 b) {
    if (s->olen >= s->ocap) {
        if (!s->grow || s->ocap > 64 * 1024 * 1024) { s->err = true; return false; }
        s->ocap = s->ocap * 2 + 4096;
        s->out = krealloc(s->out, s->ocap);
    }
    s->out[s->olen++] = b;
    return true;
}

static const u16 len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                   4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void inflate_codes(inf_t *s, const huff_t *lc, const huff_t *dc) {
    for (;;) {
        int sym = huff_decode(s, lc);
        if (sym < 0 || s->err) return;
        if (sym < 256) { if (!out_byte(s, sym)) return; continue; }
        if (sym == 256) return;
        sym -= 257;
        if (sym >= 29) { s->err = true; return; }
        int len = len_base[sym] + getbits(s, len_extra[sym]);
        int ds = huff_decode(s, dc);
        if (ds < 0 || ds >= 30) { s->err = true; return; }
        u32 dist = dist_base[ds] + getbits(s, dist_extra[ds]);
        if (dist > s->olen) { s->err = true; return; }
        for (int i = 0; i < len; i++)
            if (!out_byte(s, s->out[s->olen - dist])) return;
    }
}

static void inflate_run(inf_t *s) {
    static huff_t lc, dc;
    int last;
    do {
        last = getbit(s);
        int type = getbits(s, 2);
        if (s->err) return;
        if (type == 0) {
            s->bitcnt = 0;
            if (s->src + 4 > s->end) { s->err = true; return; }
            u16 len = s->src[0] | (s->src[1] << 8);
            s->src += 4;
            if (s->src + len > s->end) { s->err = true; return; }
            for (int i = 0; i < len; i++) if (!out_byte(s, s->src[i])) return;
            s->src += len;
        } else if (type == 1) {
            u8 lens[288 + 32];
            int i = 0;
            for (; i < 144; i++) lens[i] = 8;
            for (; i < 256; i++) lens[i] = 9;
            for (; i < 280; i++) lens[i] = 7;
            for (; i < 288; i++) lens[i] = 8;
            huff_build(&lc, lens, 288);
            for (i = 0; i < 30; i++) lens[i] = 5;
            huff_build(&dc, lens, 30);
            inflate_codes(s, &lc, &dc);
        } else if (type == 2) {
            static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            int hlit = getbits(s, 5) + 257, hdist = getbits(s, 5) + 1, hclen = getbits(s, 4) + 4;
            u8 lens[320];
            memset(lens, 0, sizeof lens);
            for (int i = 0; i < hclen; i++) lens[order[i]] = getbits(s, 3);
            huff_t cl;
            huff_build(&cl, lens, 19);
            int n = 0;
            while (n < hlit + hdist) {
                int sym = huff_decode(s, &cl);
                if (sym < 0 || s->err) return;
                if (sym < 16) lens[n++] = sym;
                else {
                    int rep = 0, val = 0;
                    if (sym == 16) { if (!n) { s->err = true; return; } val = lens[n - 1]; rep = 3 + getbits(s, 2); }
                    else if (sym == 17) rep = 3 + getbits(s, 3);
                    else rep = 11 + getbits(s, 7);
                    if (n + rep > hlit + hdist) { s->err = true; return; }
                    while (rep--) lens[n++] = val;
                }
            }
            huff_build(&lc, lens, hlit);
            huff_build(&dc, lens + hlit, hdist);
            inflate_codes(s, &lc, &dc);
        } else { s->err = true; return; }
        if (s->err) return;
    } while (!last);
}

// zlib: zwraca dane albo NULL; expected=0 -> bufor rosnie
u8 *zlib_inflate(const u8 *d, size_t n, size_t expected, size_t *outlen) {
    if (n < 2) return NULL;
    inf_t s = { 0 };
    s.src = d + 2;
    s.end = d + n;
    s.ocap = expected ? expected : n * 4 + 1024;
    s.grow = !expected;
    s.out = kmalloc(s.ocap);
    inflate_run(&s);
    if (s.err && !(expected && s.olen == expected)) { kfree(s.out); return NULL; }
    *outlen = s.olen;
    return s.out;
}

// gzip (Content-Encoding: gzip)
u8 *gzip_inflate(const u8 *d, size_t n, size_t *outlen) {
    if (n < 18 || d[0] != 0x1F || d[1] != 0x8B || d[2] != 8) return NULL;
    u8 fl = d[3];
    size_t p = 10;
    if (fl & 4) { if (p + 2 > n) return NULL; p += 2 + (d[p] | (d[p + 1] << 8)); }
    if (fl & 8) while (p < n && d[p++]) {}
    if (fl & 16) while (p < n && d[p++]) {}
    if (fl & 2) p += 2;
    if (p >= n) return NULL;
    inf_t s = { 0 };
    s.src = d + p;
    s.end = d + n;
    s.ocap = n * 5 + 4096;
    s.grow = true;
    s.out = kmalloc(s.ocap);
    inflate_run(&s);
    if (s.err) { kfree(s.out); return NULL; }
    *outlen = s.olen;
    return s.out;
}

// ================= PNG =================
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

static bool unfilter(u8 *data, int rows, int rowbytes, int bpp) {
    u8 *prev = NULL;
    for (int y = 0; y < rows; y++) {
        u8 *row = data + (size_t)y * (rowbytes + 1);
        int ft = row[0];
        u8 *r = row + 1;
        for (int i = 0; i < rowbytes; i++) {
            int a = i >= bpp ? r[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            switch (ft) {
            case 0: break;
            case 1: r[i] += a; break;
            case 2: r[i] += b; break;
            case 3: r[i] += (a + b) >> 1; break;
            case 4: r[i] += paeth(a, b, c); break;
            default: return false;
            }
        }
        prev = r;
    }
    return true;
}

typedef struct { int depth, ctype, ch; u32 pal[256]; int npal; int trns[3]; bool has_trns; } png_info_t;

static u32 png_pixel(const png_info_t *pi, const u8 *r, int x) {
    int d = pi->depth;
    if (pi->ctype == 3 || (pi->ctype == 0 && d < 8)) {
        int bits = d, idx = (r[(x * bits) >> 3] >> (8 - bits - ((x * bits) & 7))) & ((1 << bits) - 1);
        if (pi->ctype == 3) return idx < pi->npal ? pi->pal[idx] : 0xFF000000;
        int g = idx * 255 / ((1 << bits) - 1);
        u32 a = (pi->has_trns && idx == pi->trns[0]) ? 0 : 255;
        return RGBA(g, g, g, a);
    }
    int step = d / 8;   // 1 albo 2 (16 bit -> starszy bajt)
    const u8 *p = r + x * pi->ch * step;
    switch (pi->ctype) {
    case 0: { int g = p[0]; u32 a = (pi->has_trns && (d == 16 ? (p[0] << 8 | p[1]) : g) == pi->trns[0]) ? 0 : 255; return RGBA(g, g, g, a); }
    case 2: {
        u32 a = 255;
        if (pi->has_trns && d == 8 && p[0] == pi->trns[0] && p[1] == pi->trns[1] && p[2] == pi->trns[2]) a = 0;
        return RGBA(p[0], p[step], p[2 * step], a);
    }
    case 4: return RGBA(p[0], p[0], p[0], p[step]);
    case 6: return RGBA(p[0], p[step], p[2 * step], p[3 * step]);
    }
    return 0;
}

image_t *image_load_png(const u8 *d, size_t n) {
    if (n < 33 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return NULL;
    size_t p = 8;
    int w = 0, h = 0, interlace = 0;
    png_info_t pi;
    memset(&pi, 0, sizeof pi);
    u8 *idat = NULL;
    size_t ilen = 0, icap = 0;
    while (p + 12 <= n) {
        u32 len = be32(d + p);
        const u8 *type = d + p + 4, *c = d + p + 8;
        if (p + 12 + len > n) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(c), h = be32(c + 4);
            pi.depth = c[8], pi.ctype = c[9], interlace = c[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            pi.npal = MIN(256, (int)len / 3);
            for (int i = 0; i < pi.npal; i++) pi.pal[i] = RGB(c[i * 3], c[i * 3 + 1], c[i * 3 + 2]);
        } else if (!memcmp(type, "tRNS", 4)) {
            pi.has_trns = true;
            if (pi.ctype == 3) for (u32 i = 0; i < len && i < 256; i++) pi.pal[i] = (pi.pal[i] & 0xFFFFFF) | ((u32)c[i] << 24);
            else if (pi.ctype == 0 && len >= 2) pi.trns[0] = pi.depth == 16 ? (c[0] << 8 | c[1]) : c[1];
            else if (pi.ctype == 2 && len >= 6) { pi.trns[0] = c[1]; pi.trns[1] = c[3]; pi.trns[2] = c[5]; }
        } else if (!memcmp(type, "IDAT", 4)) {
            if (ilen + len > icap) { icap = (ilen + len) * 2; idat = krealloc(idat, icap); }
            memcpy(idat + ilen, c, len);
            ilen += len;
        } else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + len;
    }
    static const int chs[7] = { 1, 0, 3, 1, 2, 0, 4 };
    if (!idat || w <= 0 || h <= 0 || w > MAX_DIM || h > MAX_DIM || pi.ctype > 6 || !chs[pi.ctype] ||
        !(pi.depth == 1 || pi.depth == 2 || pi.depth == 4 || pi.depth == 8 || pi.depth == 16)) { kfree(idat); return NULL; }
    pi.ch = chs[pi.ctype];
    int bitspp = pi.ch * pi.depth, bpp = MAX(1, bitspp / 8);
    // rozmiar danych po rozpakowaniu
    static const int ax[7] = { 0, 4, 0, 2, 0, 1, 0 }, ay[7] = { 0, 0, 4, 0, 2, 0, 1 }, sx[7] = { 8, 8, 4, 4, 2, 2, 1 }, sy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    size_t need = 0;
    if (interlace) {
        for (int k = 0; k < 7; k++) {
            int pw = (w - ax[k] + sx[k] - 1) / sx[k], ph = (h - ay[k] + sy[k] - 1) / sy[k];
            if (pw > 0 && ph > 0) need += (size_t)ph * (((size_t)pw * bitspp + 7) / 8 + 1);
        }
    } else need = (size_t)h * (((size_t)w * bitspp + 7) / 8 + 1);
    size_t got;
    u8 *raw = zlib_inflate(idat, ilen, need, &got);
    kfree(idat);
    if (!raw || got < need) { kfree(raw); return NULL; }
    image_t *im = img_new(w, h);
    if (!im) { kfree(raw); return NULL; }
    u32 *px = (u32 *)im->px;
    if (!interlace) {
        int rb = ((size_t)w * bitspp + 7) / 8;
        if (!unfilter(raw, h, rb, bpp)) { kfree(raw); image_free(im); return NULL; }
        for (int y = 0; y < h; y++) {
            const u8 *r = raw + (size_t)y * (rb + 1) + 1;
            for (int x = 0; x < w; x++) px[(size_t)y * w + x] = png_pixel(&pi, r, x);
        }
    } else {
        u8 *q = raw;
        for (int k = 0; k < 7; k++) {
            int pw = (w - ax[k] + sx[k] - 1) / sx[k], ph = (h - ay[k] + sy[k] - 1) / sy[k];
            if (pw <= 0 || ph <= 0) continue;
            int rb = ((size_t)pw * bitspp + 7) / 8;
            if (!unfilter(q, ph, rb, bpp)) { kfree(raw); image_free(im); return NULL; }
            for (int y = 0; y < ph; y++) {
                const u8 *r = q + (size_t)y * (rb + 1) + 1;
                for (int x = 0; x < pw; x++) px[(size_t)(ay[k] + y * sy[k]) * w + ax[k] + x * sx[k]] = png_pixel(&pi, r, x);
            }
            q += (size_t)ph * (rb + 1);
        }
    }
    kfree(raw);
    return im;
}

// ================= JPEG (baseline) =================
typedef struct { u8 bits[17]; u8 vals[256]; int mincode[17], maxcode[18], valptr[17]; } jhuff_t;
typedef struct { int id, hx, vy, tq, td, ta, dcpred; u8 *plane; int stride; } jcomp_t;

typedef struct {
    const u8 *p, *end;
    u32 buf;
    int cnt;
    bool err, marker;
    u16 qt[4][64];
    jhuff_t dc[4], ac[4];
    jcomp_t c[3];
    int nc, w, h, hmax, vmax, restart;
} jpg_t;

static const u8 zz[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
                           35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

static int jbit(jpg_t *j) {
    if (!j->cnt) {
        if (j->p >= j->end || j->marker) { j->buf = 0; j->cnt = 8; }
        else {
            u8 b = *j->p++;
            if (b == 0xFF) {
                u8 n = j->p < j->end ? *j->p : 0;
                if (n == 0) j->p++;
                else { j->marker = true; j->p--; b = 0; }
            }
            j->buf = b;
            j->cnt = 8;
        }
    }
    j->cnt--;
    return (j->buf >> j->cnt) & 1;
}

static int jbits(jpg_t *j, int n) {
    int v = 0;
    while (n--) v = (v << 1) | jbit(j);
    return v;
}

static int jextend(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

static void jhuff_build(jhuff_t *h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
}

static int jdecode(jpg_t *j, const jhuff_t *h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | jbit(j);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l]) return h->vals[h->valptr[l] + code - h->mincode[l]];
    }
    j->err = true;
    return 0;
}

#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565
static u8 clip(int x) { return x < 0 ? 0 : x > 255 ? 255 : x; }

static void row_idct(int *b) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = b[4] << 11) | (x2 = b[6]) | (x3 = b[2]) | (x4 = b[1]) | (x5 = b[7]) | (x6 = b[5]) | (x7 = b[3]))) {
        b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = b[0] << 3;
        return;
    }
    x0 = (b[0] << 11) + 128;
    x8 = W7 * (x4 + x5); x4 = x8 + (W1 - W7) * x4; x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7); x6 = x8 - (W3 - W5) * x6; x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2); x2 = x1 - (W2 + W6) * x2; x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    b[0] = (x7 + x1) >> 8; b[1] = (x3 + x2) >> 8; b[2] = (x0 + x4) >> 8; b[3] = (x8 + x6) >> 8;
    b[4] = (x8 - x6) >> 8; b[5] = (x0 - x4) >> 8; b[6] = (x3 - x2) >> 8; b[7] = (x7 - x1) >> 8;
}

static void col_idct(const int *b, u8 *o, int st) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = b[32] << 8) | (x2 = b[48]) | (x3 = b[16]) | (x4 = b[8]) | (x5 = b[56]) | (x6 = b[40]) | (x7 = b[24]))) {
        u8 v = clip(((b[0] + 32) >> 6) + 128);
        for (int i = 0; i < 8; i++) { *o = v; o += st; }
        return;
    }
    x0 = (b[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4; x4 = (x8 + (W1 - W7) * x4) >> 3; x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4; x6 = (x8 - (W3 - W5) * x6) >> 3; x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2) + 4; x2 = (x1 - (W2 + W6) * x2) >> 3; x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    *o = clip(((x7 + x1) >> 14) + 128); o += st;
    *o = clip(((x3 + x2) >> 14) + 128); o += st;
    *o = clip(((x0 + x4) >> 14) + 128); o += st;
    *o = clip(((x8 + x6) >> 14) + 128); o += st;
    *o = clip(((x8 - x6) >> 14) + 128); o += st;
    *o = clip(((x0 - x4) >> 14) + 128); o += st;
    *o = clip(((x3 - x2) >> 14) + 128); o += st;
    *o = clip(((x7 - x1) >> 14) + 128);
}

static void jblock(jpg_t *j, jcomp_t *c, u8 *out) {
    int blk[64];
    memset(blk, 0, sizeof blk);
    int s = jdecode(j, &j->dc[c->td]);
    int diff = s ? jextend(jbits(j, s), s) : 0;
    c->dcpred += diff;
    const u16 *q = j->qt[c->tq];
    blk[0] = c->dcpred * q[0];
    for (int k = 1; k < 64 && !j->err;) {
        int rs = jdecode(j, &j->ac[c->ta]);
        int r = rs >> 4;
        s = rs & 15;
        if (!s) {
            if (r != 15) break;
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) break;
        blk[zz[k]] = jextend(jbits(j, s), s) * q[k];
        k++;
    }
    for (int i = 0; i < 64; i += 8) row_idct(blk + i);
    for (int i = 0; i < 8; i++) col_idct(blk + i, out + i, c->stride);
}

image_t *image_load_jpeg(const u8 *d, size_t n) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return NULL;
    jpg_t *j = kzalloc(sizeof(jpg_t));
    const u8 *p = d + 2, *end = d + n;
    image_t *im = NULL;
    bool sof = false;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) { p++; continue; }
        u8 m = p[1];
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) { p += m == 0xFF ? 1 : 2; continue; }
        if (m == 0xD9) break;
        u16 len = (p[2] << 8) | p[3];
        const u8 *s = p + 4, *se = p + 2 + len;
        if (se > end) break;
        if (m == 0xDB) {
            while (s < se) {
                int pq = s[0] >> 4, tq = s[0] & 3;
                s++;
                for (int i = 0; i < 64; i++) { j->qt[tq][i] = pq ? (s[0] << 8 | s[1]) : s[0]; s += pq ? 2 : 1; }
            }
        } else if (m == 0xC4) {
            while (s + 17 <= se) {
                int tc = s[0] >> 4, th = s[0] & 3;
                jhuff_t *h = tc ? &j->ac[th] : &j->dc[th];
                int tot = 0;
                for (int i = 1; i <= 16; i++) { h->bits[i] = s[i]; tot += s[i]; }
                s += 17;
                if (tot > 256 || s + tot > se) break;
                memcpy(h->vals, s, tot);
                s += tot;
                jhuff_build(h);
            }
        } else if (m == 0xC0 || m == 0xC1) {
            j->h = (s[1] << 8) | s[2];
            j->w = (s[3] << 8) | s[4];
            j->nc = s[5];
            if (j->nc != 1 && j->nc != 3) break;
            for (int i = 0; i < j->nc; i++) {
                j->c[i].id = s[6 + i * 3];
                j->c[i].hx = s[7 + i * 3] >> 4;
                j->c[i].vy = s[7 + i * 3] & 15;
                j->c[i].tq = s[8 + i * 3] & 3;
                if (!j->c[i].hx || !j->c[i].vy) goto done;
                j->hmax = MAX(j->hmax, j->c[i].hx);
                j->vmax = MAX(j->vmax, j->c[i].vy);
            }
            sof = true;
        } else if (m == 0xC2 || m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) {
            goto done;   // progresywny / inny - nieobslugiwany
        } else if (m == 0xDD) {
            j->restart = (s[0] << 8) | s[1];
        } else if (m == 0xDA) {
            if (!sof || j->w <= 0 || j->h <= 0 || j->w > MAX_DIM || j->h > MAX_DIM) goto done;
            int ns = s[0];
            for (int i = 0; i < ns; i++) {
                for (int k = 0; k < j->nc; k++)
                    if (j->c[k].id == s[1 + i * 2]) { j->c[k].td = s[2 + i * 2] >> 4 & 3; j->c[k].ta = s[2 + i * 2] & 3; }
            }
            int mw = 8 * j->hmax, mh = 8 * j->vmax;
            int mx = (j->w + mw - 1) / mw, my = (j->h + mh - 1) / mh;
            for (int k = 0; k < j->nc; k++) {
                j->c[k].stride = mx * j->c[k].hx * 8;
                j->c[k].plane = kzalloc((size_t)j->c[k].stride * my * j->c[k].vy * 8);
                j->c[k].dcpred = 0;
            }
            j->p = se;
            j->end = end;
            int mcu = 0;
            for (int by = 0; by < my && !j->err; by++)
                for (int bx = 0; bx < mx && !j->err; bx++) {
                    if (j->restart && mcu && mcu % j->restart == 0) {
                        j->cnt = 0;
                        j->marker = false;
                        while (j->p + 1 < end && !(j->p[0] == 0xFF && j->p[1] >= 0xD0 && j->p[1] <= 0xD7)) j->p++;
                        j->p += 2;
                        for (int k = 0; k < j->nc; k++) j->c[k].dcpred = 0;
                    }
                    for (int k = 0; k < j->nc; k++) {
                        jcomp_t *c = &j->c[k];
                        for (int v = 0; v < c->vy; v++)
                            for (int h = 0; h < c->hx; h++)
                                jblock(j, c, c->plane + (size_t)((by * c->vy + v) * 8) * c->stride + (bx * c->hx + h) * 8);
                    }
                    mcu++;
                }
            im = img_new(j->w, j->h);
            if (!im) goto done;
            u32 *px = (u32 *)im->px;
            for (int y = 0; y < j->h; y++)
                for (int x = 0; x < j->w; x++) {
                    int Y = j->c[0].plane[(size_t)(y * j->c[0].vy / j->vmax) * j->c[0].stride + x * j->c[0].hx / j->hmax];
                    if (j->nc == 1) { px[(size_t)y * j->w + x] = RGB(Y, Y, Y); continue; }
                    int cb = j->c[1].plane[(size_t)(y * j->c[1].vy / j->vmax) * j->c[1].stride + x * j->c[1].hx / j->hmax] - 128;
                    int cr = j->c[2].plane[(size_t)(y * j->c[2].vy / j->vmax) * j->c[2].stride + x * j->c[2].hx / j->hmax] - 128;
                    px[(size_t)y * j->w + x] = RGB(clip(Y + ((91881 * cr) >> 16)), clip(Y - ((22554 * cb + 46802 * cr) >> 16)),
                                                   clip(Y + ((116130 * cb) >> 16)));
                }
            goto done;
        }
        p = se;
    }
done:
    for (int k = 0; k < 3; k++) kfree(j->c[k].plane);
    kfree(j);
    return im;
}

// ================= GIF (pierwsza klatka) =================
image_t *image_load_gif(const u8 *d, size_t n) {
    if (n < 13 || memcmp(d, "GIF8", 4)) return NULL;
    int w = d[6] | (d[7] << 8), h = d[8] | (d[9] << 8);
    u8 fl = d[10];
    u32 gpal[256], lpal[256];
    int gsize = (fl & 0x80) ? 2 << (fl & 7) : 0;
    size_t p = 13;
    for (int i = 0; i < gsize && p + 3 <= n; i++, p += 3) gpal[i] = RGB(d[p], d[p + 1], d[p + 2]);
    int transp = -1;
    while (p < n) {
        u8 b = d[p++];
        if (b == 0x21) {
            if (p >= n) return NULL;
            u8 label = d[p++];
            if (label == 0xF9 && p + 5 < n && d[p] == 4) { if (d[p + 1] & 1) transp = d[p + 4]; }
            while (p < n && d[p]) p += d[p] + 1;
            p++;
        } else if (b == 0x2C) {
            if (p + 9 > n) return NULL;
            int fx = d[p] | (d[p + 1] << 8), fy = d[p + 2] | (d[p + 3] << 8);
            int fw = d[p + 4] | (d[p + 5] << 8), fh = d[p + 6] | (d[p + 7] << 8);
            u8 lf = d[p + 8];
            p += 9;
            u32 *pal = gpal;
            if (lf & 0x80) {
                int ls = 2 << (lf & 7);
                for (int i = 0; i < ls && p + 3 <= n; i++, p += 3) lpal[i] = RGB(d[p], d[p + 1], d[p + 2]);
                pal = lpal;
            }
            bool inter = lf & 0x40;
            if (p >= n) return NULL;
            int mcs = d[p++];
            if (mcs < 2 || mcs > 8) return NULL;
            // zbierz bloki danych
            size_t dl = 0, q = p;
            while (q < n && d[q]) { dl += d[q]; q += d[q] + 1; }
            u8 *data = kmalloc(dl + 1);
            dl = 0;
            while (p < n && d[p]) { memcpy(data + dl, d + p + 1, MIN((size_t)d[p], n - p - 1)); dl += d[p]; p += d[p] + 1; }
            image_t *im = img_new(w, h);
            if (!im) { kfree(data); return NULL; }
            // LZW
            u16 *prefix = kmalloc(4096 * 2);
            u8 *suffix = kmalloc(4096), *stack = kmalloc(4097);
            int clear = 1 << mcs, eoi = clear + 1, avail = clear + 2, csize = mcs + 1, cmask = (1 << csize) - 1, old = -1;
            u32 bits = 0;
            int nbits = 0;
            size_t ip = 0;
            u8 first = 0;
            long pix = 0, total = (long)fw * fh;
            int row = 0, pass = 0, col = 0;
            static const int istart[4] = { 0, 4, 2, 1 }, istep[4] = { 8, 8, 4, 2 };
            for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = i; }
            u32 *px = (u32 *)im->px;
            while (pix < total) {
                while (nbits < csize && ip < dl) { bits |= (u32)data[ip++] << nbits; nbits += 8; }
                if (nbits < csize) break;
                int code = bits & cmask;
                bits >>= csize;
                nbits -= csize;
                if (code == clear) { csize = mcs + 1; cmask = (1 << csize) - 1; avail = clear + 2; old = -1; continue; }
                if (code == eoi) break;
                int sp = 0, in = code;
                if (old == -1) { stack[sp++] = suffix[code]; first = suffix[code]; old = code; }
                else {
                    if (code >= avail) { stack[sp++] = first; code = old; }
                    while (code >= clear && sp < 4096) { stack[sp++] = suffix[code]; code = prefix[code]; }
                    first = suffix[code];
                    stack[sp++] = first;
                    if (avail < 4096) {
                        prefix[avail] = old;
                        suffix[avail] = first;
                        avail++;
                        if ((avail & cmask) == 0 && avail < 4096) { csize++; cmask = (1 << csize) - 1; }
                    }
                    old = in;
                }
                while (sp && pix < total) {
                    u8 idx = stack[--sp];
                    int x = fx + col, y = fy + row;
                    if (x < w && y < h) px[(size_t)y * w + x] = idx == transp ? 0 : pal[idx];
                    pix++;
                    if (++col >= fw) {
                        col = 0;
                        if (inter) {
                            row += istep[pass];
                            while (row >= fh && pass < 3) { pass++; row = istart[pass]; }
                        } else row++;
                    }
                }
            }
            kfree(prefix);
            kfree(suffix);
            kfree(stack);
            kfree(data);
            return im;
        } else break;
    }
    return NULL;
}

image_t *image_load_any(const u8 *d, size_t n) {
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P') return image_load_png(d, n);
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return image_load_jpeg(d, n);
    if (n >= 6 && !memcmp(d, "GIF8", 4)) return image_load_gif(d, n);
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return image_load_bmp(d, n);
    return NULL;
}
