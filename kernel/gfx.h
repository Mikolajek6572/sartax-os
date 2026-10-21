// Sartax OS - grafika
#pragma once
#include "kernel.h"

#define RGB(r, g, b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define RGBA(r, g, b, a) (((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define C_A(c) ((c) >> 24)
#define C_R(c) (((c) >> 16) & 255)
#define C_G(c) (((c) >> 8) & 255)
#define C_B(c) ((c) & 255)
#define WITH_ALPHA(c, a) (((c) & 0xFFFFFF) | ((u32)(a) << 24))

typedef struct { int x, y, w, h; } rect_t;

typedef struct {
    u32 *px;
    int w, h, stride;           // stride w pikselach
    int ox, oy;                 // przesuniecie poczatku ukladu
    int cx0, cy0, cx1, cy1;     // obcinanie (wspolrzedne bezwzgledne)
} surface_t;

typedef struct {
    int w, h;
    const u32 *px;              // ARGB, alfa nie przemnozona
} image_t;

typedef struct {
    u16 cp;
    i8 xoff, yoff;
    u8 w, h, adv, pad;
    u32 off;
} glyph_t;

typedef struct {
    const glyph_t *glyphs;
    int count;
    const u8 *bitmap;
    int height, ascent;
} font_t;

enum { FONT_SMALL, FONT_UI, FONT_BOLD, FONT_TITLE, FONT_BIG, FONT_MONO, FONT_HUGE, FONT_COUNT };
extern const font_t g_fonts[FONT_COUNT];

extern surface_t g_fb;          // bufor tylny, wszystko rysujemy tutaj
extern int g_scr_w, g_scr_h;

void gfx_init(void);
void gfx_present_full(void);
void gfx_present_rect(int x, int y, int w, int h);

void gfx_set_clip(surface_t *s, int x, int y, int w, int h);   // wzgledem ox/oy, przecina z obecnym
void gfx_reset(surface_t *s);
static inline u32 blend(u32 d, u32 s, u32 a) {
    u32 ia = 255 - a;
    u32 rb = ((s & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080) >> 8 & 0xFF00FF;
    u32 g = ((s & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000) >> 8 & 0x00FF00;
    return 0xFF000000u | rb | g;
}
u32 color_mix(u32 a, u32 b, int t);        // t 0..255
u32 color_lighten(u32 c, int amt);         // amt -255..255

void gfx_pixel(surface_t *s, int x, int y, u32 c);
void gfx_fill(surface_t *s, int x, int y, int w, int h, u32 c);
void gfx_rect(surface_t *s, int x, int y, int w, int h, u32 c);
void gfx_hline(surface_t *s, int x, int y, int w, u32 c);
void gfx_vline(surface_t *s, int x, int y, int h, u32 c);
void gfx_line(surface_t *s, int x0, int y0, int x1, int y1, u32 c);
void gfx_gradient_v(surface_t *s, int x, int y, int w, int h, u32 c1, u32 c2);
void gfx_gradient_h(surface_t *s, int x, int y, int w, int h, u32 c1, u32 c2);
void gfx_round_rect(surface_t *s, int x, int y, int w, int h, int r, u32 c);
void gfx_round_rect_grad(surface_t *s, int x, int y, int w, int h, int r, u32 c1, u32 c2);
void gfx_round_outline(surface_t *s, int x, int y, int w, int h, int r, u32 c);
void gfx_circle(surface_t *s, int cx, int cy, int r, u32 c);
void gfx_shadow(surface_t *s, int x, int y, int w, int h, int r, int size, int alpha);
void gfx_blit(surface_t *s, int x, int y, const image_t *img);
void gfx_blit_alpha(surface_t *s, int x, int y, const image_t *img, int alpha);
void gfx_blit_scaled(surface_t *s, int x, int y, int w, int h, const image_t *img);
void gfx_copy(surface_t *dst, int x, int y, const u32 *src, int w, int h, int stride);

int gfx_text(surface_t *s, int x, int y, int font, const char *str, u32 c);
int gfx_text_n(surface_t *s, int x, int y, int font, const char *str, int nbytes, u32 c);
int gfx_text_width(int font, const char *str);
int gfx_text_width_n(int font, const char *str, int nbytes);
int gfx_char_width(int font, u32 cp);
int gfx_font_height(int font);
void gfx_text_center(surface_t *s, int x, int y, int w, int font, const char *str, u32 c);
void gfx_text_ellipsis(surface_t *s, int x, int y, int maxw, int font, const char *str, u32 c);
int gfx_text_wrap(surface_t *s, int x, int y, int maxw, int font, const char *str, u32 c, int line_h);

// ikony
enum {
    ICON_FOLDER, ICON_FILE, ICON_TEXT, ICON_IMAGE, ICON_APP, ICON_COMPUTER, ICON_DOWNLOAD, ICON_SETTINGS,
    ICON_NOTEPAD, ICON_VIEWER, ICON_UPDATE, ICON_DISK, ICON_LOGO, ICON_INFO, ICON_POWER, ICON_USER,
    ICON_DESKTOP, ICON_DOCS, ICON_PICTURES, ICON_PROGRAMS, ICON_BACK, ICON_FORWARD, ICON_UP, ICON_NEWFOLDER,
    ICON_REFRESH, ICON_RESTART, ICON_WARNING, ICON_TRASH, ICON_NEWFILE, ICON_SEARCH, ICON_GLOBE, ICON_HOME, ICON_COUNT
};
const image_t *icon_get(int id, int size);     // size 16, 24, 32 albo 48

// obrazy z plikow (BMP)
image_t *image_load_bmp(const u8 *data, size_t len);
void image_free(image_t *img);
