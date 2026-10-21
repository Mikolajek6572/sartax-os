// Sartax OS - grafika 2D (bufor tylny + VBE)
#include "gfx.h"

surface_t g_fb;
int g_scr_w, g_scr_h;
static u8 *vram;
static u32 vram_pitch;

void gfx_init(void) {
    g_scr_w = g_boot->fb_width;
    g_scr_h = g_boot->fb_height;
    vram = (u8 *)(u64)g_boot->fb_addr;
    vram_pitch = g_boot->fb_pitch;
    g_fb.w = g_scr_w;
    g_fb.h = g_scr_h;
    g_fb.stride = g_scr_w;
    g_fb.px = kzalloc((size_t)g_scr_w * g_scr_h * 4);
    gfx_reset(&g_fb);
}

void gfx_present_rect(int x, int y, int w, int h) {
    if (x < 0) w += x, x = 0;
    if (y < 0) h += y, y = 0;
    if (x + w > g_scr_w) w = g_scr_w - x;
    if (y + h > g_scr_h) h = g_scr_h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        memcpy(vram + (size_t)(y + j) * vram_pitch + x * 4, g_fb.px + (size_t)(y + j) * g_fb.stride + x, w * 4);
}

void gfx_present_full(void) { gfx_present_rect(0, 0, g_scr_w, g_scr_h); }

void gfx_reset(surface_t *s) {
    s->ox = s->oy = 0;
    s->cx0 = s->cy0 = 0;
    s->cx1 = s->w;
    s->cy1 = s->h;
}

void gfx_set_clip(surface_t *s, int x, int y, int w, int h) {
    x += s->ox;
    y += s->oy;
    s->cx0 = MAX(s->cx0, x);
    s->cy0 = MAX(s->cy0, y);
    s->cx1 = MIN(s->cx1, x + w);
    s->cy1 = MIN(s->cy1, y + h);
}

u32 color_mix(u32 a, u32 b, int t) {
    int r = C_R(a) + (C_R(b) - (int)C_R(a)) * t / 255;
    int g = C_G(a) + (C_G(b) - (int)C_G(a)) * t / 255;
    int bb = C_B(a) + (C_B(b) - (int)C_B(a)) * t / 255;
    int al = C_A(a) + (C_A(b) - (int)C_A(a)) * t / 255;
    return RGBA(r, g, bb, al);
}

u32 color_lighten(u32 c, int amt) {
    if (amt >= 0) return (c & 0xFF000000) | (color_mix(c, 0xFFFFFFFF, amt) & 0xFFFFFF);
    return (c & 0xFF000000) | (color_mix(c, 0xFF000000, -amt) & 0xFFFFFF);
}

static inline void put(surface_t *s, int x, int y, u32 c, u32 a) {
    u32 *p = &s->px[(size_t)y * s->stride + x];
    if (a >= 255) *p = c | 0xFF000000;
    else if (a) *p = blend(*p, c, a);
}

void gfx_pixel(surface_t *s, int x, int y, u32 c) {
    x += s->ox, y += s->oy;
    if (x < s->cx0 || y < s->cy0 || x >= s->cx1 || y >= s->cy1) return;
    put(s, x, y, c, C_A(c));
}

void gfx_fill(surface_t *s, int x, int y, int w, int h, u32 c) {
    x += s->ox, y += s->oy;
    int x0 = MAX(x, s->cx0), y0 = MAX(y, s->cy0), x1 = MIN(x + w, s->cx1), y1 = MIN(y + h, s->cy1);
    if (x0 >= x1 || y0 >= y1) return;
    u32 a = C_A(c);
    if (!a) return;
    for (int j = y0; j < y1; j++) {
        u32 *p = &s->px[(size_t)j * s->stride + x0];
        int n = x1 - x0;
        if (a == 255) {
            u64 cc = c;
            __asm__ volatile("rep stosl" : "+D"(p), "+c"(n) : "a"(cc) : "memory");
        } else {
            for (int i = 0; i < n; i++) p[i] = blend(p[i], c, a);
        }
    }
}

void gfx_hline(surface_t *s, int x, int y, int w, u32 c) { gfx_fill(s, x, y, w, 1, c); }
void gfx_vline(surface_t *s, int x, int y, int h, u32 c) { gfx_fill(s, x, y, 1, h, c); }

void gfx_rect(surface_t *s, int x, int y, int w, int h, u32 c) {
    gfx_fill(s, x, y, w, 1, c);
    gfx_fill(s, x, y + h - 1, w, 1, c);
    gfx_fill(s, x, y + 1, 1, h - 2, c);
    gfx_fill(s, x + w - 1, y + 1, 1, h - 2, c);
}

void gfx_line(surface_t *s, int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_gradient_v(surface_t *s, int x, int y, int w, int h, u32 c1, u32 c2) {
    for (int j = 0; j < h; j++) gfx_fill(s, x, y + j, w, 1, color_mix(c1, c2, h > 1 ? j * 255 / (h - 1) : 0));
}

void gfx_gradient_h(surface_t *s, int x, int y, int w, int h, u32 c1, u32 c2) {
    for (int i = 0; i < w; i++) gfx_fill(s, x + i, y, 1, h, color_mix(c1, c2, w > 1 ? i * 255 / (w - 1) : 0));
}

static u32 isqrt(u32 v) {
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

// pokrycie piksela (i, j) w rogu o promieniu r (0..256)
static int corner_cov(int i, int j, int r) {
    int dx = (r << 8) - (i << 8) - 128, dy = (r << 8) - (j << 8) - 128;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    int d = isqrt((u32)(dx * dx + dy * dy));
    int cov = (r << 8) - d + 128;
    return CLAMP(cov, 0, 256);
}

static void round_rect_impl(surface_t *s, int x, int y, int w, int h, int r, u32 c1, u32 c2, bool grad) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    u32 a = C_A(c1);
    for (int j = 0; j < h; j++) {
        u32 c = grad ? color_mix(c1, c2, h > 1 ? j * 255 / (h - 1) : 0) : c1;
        if (grad) a = C_A(c);
        int jj = j < r ? j : (j >= h - r ? h - 1 - j : -1);
        if (jj < 0) { gfx_fill(s, x, y + j, w, 1, c); continue; }
        gfx_fill(s, x + r, y + j, w - 2 * r, 1, c);
        for (int i = 0; i < r; i++) {
            int cov = corner_cov(i, jj, r);
            if (!cov) continue;
            u32 cc = WITH_ALPHA(c, a * cov >> 8);
            gfx_pixel(s, x + i, y + j, cc);
            gfx_pixel(s, x + w - 1 - i, y + j, cc);
        }
    }
}

void gfx_round_rect(surface_t *s, int x, int y, int w, int h, int r, u32 c) { round_rect_impl(s, x, y, w, h, r, c, c, false); }
void gfx_round_rect_grad(surface_t *s, int x, int y, int w, int h, int r, u32 c1, u32 c2) { round_rect_impl(s, x, y, w, h, r, c1, c2, true); }

void gfx_round_outline(surface_t *s, int x, int y, int w, int h, int r, u32 c) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    u32 a = C_A(c);
    gfx_fill(s, x + r, y, w - 2 * r, 1, c);
    gfx_fill(s, x + r, y + h - 1, w - 2 * r, 1, c);
    gfx_fill(s, x, y + r, 1, h - 2 * r, c);
    gfx_fill(s, x + w - 1, y + r, 1, h - 2 * r, c);
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            int outer = corner_cov(i, j, r);
            int inner = r > 1 ? corner_cov(i - 1, j - 1, r - 1) : 0;
            if (i == 0 || j == 0) inner = 0;
            int cov = outer - inner;
            if (cov <= 0) continue;
            u32 cc = WITH_ALPHA(c, a * cov >> 8);
            gfx_pixel(s, x + i, y + j, cc);
            gfx_pixel(s, x + w - 1 - i, y + j, cc);
            gfx_pixel(s, x + i, y + h - 1 - j, cc);
            gfx_pixel(s, x + w - 1 - i, y + h - 1 - j, cc);
        }
}

void gfx_circle(surface_t *s, int cx, int cy, int r, u32 c) {
    gfx_round_rect(s, cx - r, cy - r, r * 2, r * 2, r, c);
}

void gfx_shadow(surface_t *s, int x, int y, int w, int h, int r, int size, int alpha) {
    if (size <= 0) return;
    int oy = y;
    y += size / 3;
    int x0 = x + r, y0 = y + r, x1 = x + w - r, y1 = y + h - r;   // wewnetrzny prostokat
    int rr = r + size;
    for (int py = y - size; py < y + h + size; py++) {
        int dy = py < y0 ? y0 - py : (py >= y1 ? py - y1 + 1 : 0);
        bool inside_y = py >= oy && py < oy + h;
        for (int px = x - size; px < x + w + size; px++) {
            if (inside_y && px >= x && px < x + w) { px = x + w - 1; continue; }
            int dx = px < x0 ? x0 - px : (px >= x1 ? px - x1 + 1 : 0);
            int d2 = dx * dx + dy * dy;
            if (d2 >= rr * rr) continue;
            int d = isqrt((u32)d2 << 8) ;     // *16
            int t = (rr << 4) - d;            // 0..rr*16
            int rng = size << 4;
            if (t > rng) t = rng;
            if (t <= 0) continue;
            int a = alpha * t / rng;
            a = a * t / rng;
            gfx_pixel(s, px, py, RGBA(0, 0, 0, a));
        }
    }
}

void gfx_blit_alpha(surface_t *s, int x, int y, const image_t *img, int alpha) {
    if (!img) return;
    int ax = x + s->ox, ay = y + s->oy;
    int x0 = MAX(ax, s->cx0), y0 = MAX(ay, s->cy0), x1 = MIN(ax + img->w, s->cx1), y1 = MIN(ay + img->h, s->cy1);
    for (int j = y0; j < y1; j++) {
        const u32 *src = img->px + (size_t)(j - ay) * img->w;
        u32 *dst = &s->px[(size_t)j * s->stride];
        for (int i = x0; i < x1; i++) {
            u32 c = src[i - ax];
            u32 a = C_A(c) * alpha / 255;
            if (a >= 255) dst[i] = c;
            else if (a) dst[i] = blend(dst[i], c, a);
        }
    }
}

void gfx_blit(surface_t *s, int x, int y, const image_t *img) { gfx_blit_alpha(s, x, y, img, 255); }

void gfx_blit_scaled(surface_t *s, int x, int y, int w, int h, const image_t *img) {
    if (!img || w <= 0 || h <= 0) return;
    int ax = x + s->ox, ay = y + s->oy;
    int x0 = MAX(ax, s->cx0), y0 = MAX(ay, s->cy0), x1 = MIN(ax + w, s->cx1), y1 = MIN(ay + h, s->cy1);
    u32 sx = ((u32)img->w << 16) / w, sy = ((u32)img->h << 16) / h;
    for (int j = y0; j < y1; j++) {
        u32 v = ((u32)(j - ay) * sy) >> 16;
        if ((int)v >= img->h) v = img->h - 1;
        const u32 *src = img->px + (size_t)v * img->w;
        u32 *dst = &s->px[(size_t)j * s->stride];
        // jesli pomniejszamy mocno, usredniamy 2x2 dla gladszego obrazu
        bool avg = sx > 0x18000 && sy > 0x18000;
        for (int i = x0; i < x1; i++) {
            u32 u = ((u32)(i - ax) * sx) >> 16;
            if ((int)u >= img->w) u = img->w - 1;
            u32 c = src[u];
            if (avg && (int)u + 1 < img->w && (int)v + 1 < img->h) {
                u32 c2 = src[u + 1], c3 = src[u + img->w], c4 = src[u + img->w + 1];
                u32 rb = ((c & 0xFF00FF) + (c2 & 0xFF00FF) + (c3 & 0xFF00FF) + (c4 & 0xFF00FF)) >> 2 & 0xFF00FF;
                u32 g = ((c & 0xFF00) + (c2 & 0xFF00) + (c3 & 0xFF00) + (c4 & 0xFF00)) >> 2 & 0xFF00;
                u32 al = (C_A(c) + C_A(c2) + C_A(c3) + C_A(c4)) >> 2;
                c = (al << 24) | rb | g;
            }
            u32 a = C_A(c);
            if (a >= 255) dst[i] = c;
            else if (a) dst[i] = blend(dst[i], c, a);
        }
    }
}

void gfx_copy(surface_t *s, int x, int y, const u32 *src, int w, int h, int stride) {
    int ax = x + s->ox, ay = y + s->oy;
    int x0 = MAX(ax, s->cx0), y0 = MAX(ay, s->cy0), x1 = MIN(ax + w, s->cx1), y1 = MIN(ay + h, s->cy1);
    if (x0 >= x1) return;
    for (int j = y0; j < y1; j++)
        memcpy(&s->px[(size_t)j * s->stride + x0], src + (size_t)(j - ay) * stride + (x0 - ax), (x1 - x0) * 4);
}

// ---------------- tekst ----------------
static const glyph_t *find_glyph(const font_t *f, u32 cp) {
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->glyphs[m].cp == cp) return &f->glyphs[m];
        if (f->glyphs[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

static const glyph_t *glyph_or_fallback(const font_t *f, u32 cp) {
    const glyph_t *g = find_glyph(f, cp);
    if (!g) g = find_glyph(f, '?');
    return g;
}

int gfx_font_height(int font) { return g_fonts[font].height; }

int gfx_char_width(int font, u32 cp) {
    const glyph_t *g = glyph_or_fallback(&g_fonts[font], cp);
    return g ? g->adv : 0;
}

int gfx_text_width_n(int font, const char *str, int nbytes) {
    const font_t *f = &g_fonts[font];
    const char *p = str, *end = nbytes < 0 ? NULL : str + nbytes;
    int w = 0;
    while ((!end || p < end) && *p) {
        u32 cp = utf8_decode(&p);
        const glyph_t *g = glyph_or_fallback(f, cp);
        if (g) w += g->adv;
    }
    return w;
}

int gfx_text_width(int font, const char *str) { return gfx_text_width_n(font, str, -1); }

int gfx_text_n(surface_t *s, int x, int y, int font, const char *str, int nbytes, u32 c) {
    const font_t *f = &g_fonts[font];
    const char *p = str, *end = nbytes < 0 ? NULL : str + nbytes;
    int start = x;
    u32 ca = C_A(c);
    while ((!end || p < end) && *p) {
        u32 cp = utf8_decode(&p);
        const glyph_t *g = glyph_or_fallback(f, cp);
        if (!g) continue;
        int gx = x + g->xoff + s->ox, gy = y + g->yoff + s->oy;
        if (gx + g->w > s->cx0 && gx < s->cx1 && gy + g->h > s->cy0 && gy < s->cy1) {
            const u8 *bm = f->bitmap + g->off;
            for (int j = 0; j < g->h; j++) {
                int py = gy + j;
                if (py < s->cy0 || py >= s->cy1) continue;
                u32 *row = &s->px[(size_t)py * s->stride];
                for (int i = 0; i < g->w; i++) {
                    int px = gx + i;
                    if (px < s->cx0 || px >= s->cx1) continue;
                    u32 a = bm[j * g->w + i];
                    if (!a) continue;
                    a = a * ca / 255;
                    row[px] = a >= 255 ? (c | 0xFF000000) : blend(row[px], c, a);
                }
            }
        }
        x += g->adv;
    }
    return x - start;
}

int gfx_text(surface_t *s, int x, int y, int font, const char *str, u32 c) { return gfx_text_n(s, x, y, font, str, -1, c); }

void gfx_text_center(surface_t *s, int x, int y, int w, int font, const char *str, u32 c) {
    gfx_text(s, x + (w - gfx_text_width(font, str)) / 2, y, font, str, c);
}

void gfx_text_ellipsis(surface_t *s, int x, int y, int maxw, int font, const char *str, u32 c) {
    if (gfx_text_width(font, str) <= maxw) { gfx_text(s, x, y, font, str, c); return; }
    int ew = gfx_text_width(font, "…");
    const char *p = str;
    int w = 0;
    while (*p) {
        const char *q = p;
        u32 cp = utf8_decode(&q);
        int cw = gfx_char_width(font, cp);
        if (w + cw + ew > maxw) break;
        w += cw;
        p = q;
    }
    gfx_text_n(s, x, y, font, str, p - str, c);
    gfx_text(s, x + w, y, font, "…", c);
}

// zawijanie po slowach, zwraca wysokosc
int gfx_text_wrap(surface_t *s, int x, int y, int maxw, int font, const char *str, u32 c, int lh) {
    if (!lh) lh = gfx_font_height(font) + 3;
    int cy = y;
    const char *p = str;
    while (*p) {
        const char *line = p, *last_break = NULL;
        int w = 0;
        while (*p && *p != '\n') {
            const char *q = p;
            u32 cp = utf8_decode(&q);
            int cw = gfx_char_width(font, cp);
            if (w + cw > maxw && p != line) break;
            if (cp == ' ') last_break = p;
            w += cw;
            p = q;
        }
        const char *end = p;
        if (*p && *p != '\n' && last_break) { end = last_break; p = last_break + 1; }
        if (s) gfx_text_n(s, x, cy, font, line, end - line, c);
        cy += lh;
        if (*p == '\n') p++;
    }
    return cy - y;
}

// ---------------- BMP ----------------
image_t *image_load_bmp(const u8 *d, size_t len) {
    if (len < 54 || d[0] != 'B' || d[1] != 'M') return NULL;
    u32 off = *(u32 *)(d + 10);
    i32 w = *(i32 *)(d + 18), h = *(i32 *)(d + 22);
    u16 bpp = *(u16 *)(d + 28);
    u32 comp = *(u32 *)(d + 30);
    if (w <= 0 || w > 8192 || h == 0 || h > 8192 || h < -8192) return NULL;
    if (!(bpp == 24 || bpp == 32 || bpp == 8)) return NULL;
    if (comp != 0 && comp != 3) return NULL;
    bool flip = h > 0;
    if (h < 0) h = -h;
    u32 rowsz = ((u32)w * bpp + 31) / 32 * 4;
    if (off + (size_t)rowsz * h > len) return NULL;
    image_t *img = kmalloc(sizeof(image_t));
    u32 *px = kmalloc((size_t)w * h * 4);
    const u8 *pal = d + 14 + *(u32 *)(d + 14);
    for (int j = 0; j < h; j++) {
        const u8 *row = d + off + (size_t)(flip ? h - 1 - j : j) * rowsz;
        u32 *o = px + (size_t)j * w;
        for (int i = 0; i < w; i++) {
            if (bpp == 24) o[i] = RGB(row[i * 3 + 2], row[i * 3 + 1], row[i * 3]);
            else if (bpp == 32) o[i] = RGBA(row[i * 4 + 2], row[i * 4 + 1], row[i * 4], comp == 3 || row[i * 4 + 3] ? row[i * 4 + 3] : 255);
            else { const u8 *c = pal + row[i] * 4; o[i] = RGB(c[2], c[1], c[0]); }
        }
    }
    // 32-bit bez alfy (wszystko 0) -> nieprzezroczyste
    if (bpp == 32) {
        bool any = false;
        for (size_t i = 0; i < (size_t)w * h && !any; i++) any = C_A(px[i]) != 0;
        if (!any) for (size_t i = 0; i < (size_t)w * h; i++) px[i] |= 0xFF000000;
    }
    img->w = w;
    img->h = h;
    img->px = px;
    return img;
}

void image_free(image_t *img) {
    if (!img) return;
    kfree((void *)img->px);
    kfree(img);
}
